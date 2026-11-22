/* Unit tests for src/core/sftp_proto.c and src/core/sftp_client.c: the
 * client talks to a small SFTP v3 server, in a thread at the other end of a
 * socket pair, working on the files of a temporary folder (POSIX calls, as
 * OpenSSH's sftp-server). */
#include "core/sftp_client.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* The server                                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    int fd;
    gboolean short_reads;  /* DATA of at most 1000 bytes */
    gboolean posix_rename; /* announce posix-rename@openssh.com */
    gint die_after;        /* > 0: hang up after that many packets */
    GThread* thread;
} Server;

typedef struct {
    int fd;
    DIR* dir;
    gchar* path;
} Handle;

static gboolean read_full(int fd, void* buf, gsize n) {
    guint8* p = buf;
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return FALSE;
        p += r;
        n -= (gsize)r;
    }
    return TRUE;
}

static void write_bytes(int fd, GBytes* b) {
    gsize n = 0;
    const guint8* p = g_bytes_get_data(b, &n);
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        p += w;
        n -= (gsize)w;
    }
    g_bytes_unref(b);
}

static guint32 errno_status(int e) {
    if (e == ENOENT || e == ENOTDIR) return SFTP_NO_SUCH_FILE;
    if (e == EACCES || e == EPERM) return SFTP_PERMISSION_DENIED;
    return SFTP_FAILURE;
}

static GBytes* status(guint32 id, guint32 code) {
    GByteArray* b = sftp_packet_begin(SFTP_STATUS);
    sftp_put_u32(b, id);
    sftp_put_u32(b, code);
    sftp_put_str(b, code == SFTP_OK ? "Success" : "Failure");
    sftp_put_str(b, "");
    return sftp_packet_finish(b);
}

static void stat_attrs(const struct stat* st, SftpAttrs* a) {
    a->flags = SFTP_ATTR_SIZE | SFTP_ATTR_UIDGID | SFTP_ATTR_PERMISSIONS | SFTP_ATTR_ACMODTIME;
    a->size = (guint64)st->st_size;
    a->uid = st->st_uid;
    a->gid = st->st_gid;
    a->permissions = st->st_mode;
    a->atime = (guint32)st->st_atime;
    a->mtime = (guint32)st->st_mtime;
}

static GBytes* attrs_reply(guint32 id, int rc, const struct stat* st) {
    if (rc != 0) return status(id, errno_status(errno));
    SftpAttrs a;
    stat_attrs(st, &a);
    GByteArray* b = sftp_packet_begin(SFTP_ATTRS);
    sftp_put_u32(b, id);
    sftp_put_attrs(b, &a);
    return sftp_packet_finish(b);
}

static GBytes* handle_reply_of(guint32 id, GPtrArray* handles, Handle* h) {
    g_ptr_array_add(handles, h);
    guint32 index = handles->len - 1;
    GByteArray* b = sftp_packet_begin(SFTP_HANDLE);
    sftp_put_u32(b, id);
    sftp_put_data(b, &index, sizeof index);
    return sftp_packet_finish(b);
}

static Handle* get_handle(SftpReader* r, GPtrArray* handles) {
    const guint8* d = NULL;
    guint32 n = 0;
    guint32 index = 0;
    if (!sftp_get_data(r, &d, &n) || n != sizeof index) return NULL;
    memcpy(&index, d, sizeof index);
    return index < handles->len ? g_ptr_array_index(handles, index) : NULL;
}

static GBytes* serve_one(Server* s, guint8 type, guint32 id, SftpReader* r, GPtrArray* handles) {
    struct stat st;
    gchar* path = NULL;
    GBytes* out = NULL;
    switch (type) {
    case SFTP_OPEN: {
        path = sftp_get_str(r);
        guint32 pflags = 0;
        SftpAttrs a;
        sftp_get_u32(r, &pflags);
        sftp_get_attrs(r, &a);
        int flags = (pflags & SFTP_OPEN_READ) && (pflags & SFTP_OPEN_WRITE) ? O_RDWR
                    : (pflags & SFTP_OPEN_WRITE)                            ? O_WRONLY
                                                                            : O_RDONLY;
        if (pflags & SFTP_OPEN_CREAT) flags |= O_CREAT;
        if (pflags & SFTP_OPEN_TRUNC) flags |= O_TRUNC;
        if (pflags & SFTP_OPEN_EXCL) flags |= O_EXCL;
        if (pflags & SFTP_OPEN_APPEND) flags |= O_APPEND;
        int fd = open(path, flags, (a.flags & SFTP_ATTR_PERMISSIONS) ? (a.permissions & 07777) : 0666);
        if (fd < 0) {
            out = status(id, errno_status(errno));
        } else {
            Handle* h = g_new0(Handle, 1);
            h->fd = fd;
            out = handle_reply_of(id, handles, h);
        }
        break;
    }
    case SFTP_OPENDIR: {
        path = sftp_get_str(r);
        DIR* d = opendir(path);
        if (d == NULL) {
            out = status(id, errno_status(errno));
        } else {
            Handle* h = g_new0(Handle, 1);
            h->fd = -1;
            h->dir = d;
            h->path = g_strdup(path);
            out = handle_reply_of(id, handles, h);
        }
        break;
    }
    case SFTP_CLOSE: {
        Handle* h = get_handle(r, handles);
        if (h == NULL) return status(id, SFTP_FAILURE);
        if (h->fd >= 0) close(h->fd);
        if (h->dir != NULL) closedir(h->dir);
        h->fd = -1;
        h->dir = NULL;
        out = status(id, SFTP_OK);
        break;
    }
    case SFTP_READ: {
        Handle* h = get_handle(r, handles);
        guint64 off = 0;
        guint32 len = 0;
        sftp_get_u64(r, &off);
        sftp_get_u32(r, &len);
        if (h == NULL || h->fd < 0) return status(id, SFTP_FAILURE);
        if (s->short_reads) len = MIN(len, 1000);
        guint8* buf = g_malloc(len);
        ssize_t n = pread(h->fd, buf, len, (off_t)off);
        if (n < 0) {
            out = status(id, errno_status(errno));
        } else if (n == 0) {
            out = status(id, SFTP_EOF);
        } else {
            GByteArray* b = sftp_packet_begin(SFTP_DATA);
            sftp_put_u32(b, id);
            sftp_put_data(b, buf, (gsize)n);
            out = sftp_packet_finish(b);
        }
        g_free(buf);
        break;
    }
    case SFTP_WRITE: {
        Handle* h = get_handle(r, handles);
        guint64 off = 0;
        const guint8* d = NULL;
        guint32 n = 0;
        sftp_get_u64(r, &off);
        sftp_get_data(r, &d, &n);
        if (h == NULL || h->fd < 0) return status(id, SFTP_FAILURE);
        out = status(id, pwrite(h->fd, d, n, (off_t)off) == (ssize_t)n ? SFTP_OK : errno_status(errno));
        break;
    }
    case SFTP_FSTAT: {
        Handle* h = get_handle(r, handles);
        if (h == NULL || h->fd < 0) return status(id, SFTP_FAILURE);
        out = attrs_reply(id, fstat(h->fd, &st), &st);
        break;
    }
    case SFTP_FSETSTAT: {
        Handle* h = get_handle(r, handles);
        SftpAttrs a;
        sftp_get_attrs(r, &a);
        if (h == NULL || h->fd < 0) return status(id, SFTP_FAILURE);
        int rc = (a.flags & SFTP_ATTR_PERMISSIONS) ? fchmod(h->fd, a.permissions & 07777) : 0;
        out = status(id, rc == 0 ? SFTP_OK : errno_status(errno));
        break;
    }
    case SFTP_READDIR: {
        Handle* h = get_handle(r, handles);
        if (h == NULL || h->dir == NULL) return status(id, SFTP_FAILURE);
        GByteArray* names = g_byte_array_new();
        guint32 count = 0;
        struct dirent* de = NULL;
        while (count < 3 && (de = readdir(h->dir)) != NULL) { /* a few a time: several READDIRs */
            gchar* full = g_build_filename(h->path, de->d_name, NULL);
            SftpAttrs a = { 0 };
            if (lstat(full, &st) == 0) stat_attrs(&st, &a);
            sftp_put_str(names, de->d_name);
            sftp_put_str(names, de->d_name); /* longname */
            sftp_put_attrs(names, &a);
            g_free(full);
            count++;
        }
        if (count == 0) {
            out = status(id, SFTP_EOF);
        } else {
            GByteArray* b = sftp_packet_begin(SFTP_NAME);
            sftp_put_u32(b, id);
            sftp_put_u32(b, count);
            g_byte_array_append(b, names->data, names->len);
            out = sftp_packet_finish(b);
        }
        g_byte_array_unref(names);
        break;
    }
    case SFTP_LSTAT:
    case SFTP_STAT:
        path = sftp_get_str(r);
        out = attrs_reply(id, type == SFTP_STAT ? stat(path, &st) : lstat(path, &st), &st);
        break;
    case SFTP_REMOVE:
        path = sftp_get_str(r);
        out = status(id, unlink(path) == 0 ? SFTP_OK : errno_status(errno));
        break;
    case SFTP_RMDIR:
        path = sftp_get_str(r);
        out = status(id, rmdir(path) == 0 ? SFTP_OK : errno_status(errno));
        break;
    case SFTP_MKDIR: {
        path = sftp_get_str(r);
        SftpAttrs a;
        sftp_get_attrs(r, &a);
        mode_t mode = (a.flags & SFTP_ATTR_PERMISSIONS) ? (a.permissions & 07777) : 0777;
        out = status(id, mkdir(path, mode) == 0 ? SFTP_OK : errno_status(errno));
        break;
    }
    case SFTP_REALPATH: {
        path = sftp_get_str(r);
        char resolved[PATH_MAX];
        if (realpath(path, resolved) == NULL) {
            out = status(id, errno_status(errno));
        } else {
            GByteArray* b = sftp_packet_begin(SFTP_NAME);
            sftp_put_u32(b, id);
            sftp_put_u32(b, 1);
            sftp_put_str(b, resolved);
            sftp_put_str(b, resolved);
            sftp_put_attrs(b, NULL);
            out = sftp_packet_finish(b);
        }
        break;
    }
    case SFTP_RENAME: { /* as sftp-server: never over an existing file */
        path = sftp_get_str(r);
        gchar* to = sftp_get_str(r);
        if (lstat(to, &st) == 0) {
            out = status(id, SFTP_FAILURE);
        } else {
            out = status(id, rename(path, to) == 0 ? SFTP_OK : errno_status(errno));
        }
        g_free(to);
        break;
    }
    case SFTP_EXTENDED: {
        gchar* name = sftp_get_str(r);
        if (s->posix_rename && g_strcmp0(name, "posix-rename@openssh.com") == 0) {
            path = sftp_get_str(r);
            gchar* to = sftp_get_str(r);
            out = status(id, rename(path, to) == 0 ? SFTP_OK : errno_status(errno));
            g_free(to);
        } else {
            out = status(id, SFTP_OP_UNSUPPORTED);
        }
        g_free(name);
        break;
    }
    default:
        out = status(id, SFTP_OP_UNSUPPORTED);
        break;
    }
    g_free(path);
    return out;
}

static void handle_free(gpointer data) {
    Handle* h = data;
    if (h->fd >= 0) close(h->fd);
    if (h->dir != NULL) closedir(h->dir);
    g_free(h->path);
    g_free(h);
}

static gpointer server_main(gpointer data) {
    Server* s = data;
    GPtrArray* handles = g_ptr_array_new_with_free_func(handle_free);
    gint count = 0;
    for (;;) {
        guint8 head[4];
        if (!read_full(s->fd, head, 4)) break;
        guint32 len = ((guint32)head[0] << 24) | ((guint32)head[1] << 16) | ((guint32)head[2] << 8) | head[3];
        guint8* body = g_malloc(len);
        if (!read_full(s->fd, body, len)) {
            g_free(body);
            break;
        }
        if (s->die_after > 0 && ++count > s->die_after) {
            g_free(body);
            break;
        }
        SftpReader r;
        sftp_reader_init(&r, body, len);
        guint8 type = 0;
        sftp_get_u8(&r, &type);
        if (type == SFTP_INIT) {
            GByteArray* b = sftp_packet_begin(SFTP_VERSION);
            sftp_put_u32(b, 3);
            if (s->posix_rename) {
                sftp_put_str(b, "posix-rename@openssh.com");
                sftp_put_str(b, "1");
            }
            write_bytes(s->fd, sftp_packet_finish(b));
        } else {
            guint32 id = 0;
            sftp_get_u32(&r, &id);
            write_bytes(s->fd, serve_one(s, type, id, &r, handles));
        }
        g_free(body);
    }
    close(s->fd);
    g_ptr_array_unref(handles);
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* The client side                                                           */
/* ------------------------------------------------------------------------- */

typedef struct {
    Server server;
    GSocketConnection* conn;
    SftpClient* client;
} Pair;

static Pair* pair_new(gboolean short_reads, gboolean posix_rename, gint die_after) {
    int fds[2];
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), ==, 0);
    Pair* p = g_new0(Pair, 1);
    p->server.fd = fds[1];
    p->server.short_reads = short_reads;
    p->server.posix_rename = posix_rename;
    p->server.die_after = die_after;
    p->server.thread = g_thread_new("sftp-server", server_main, &p->server);
    GSocket* sock = g_socket_new_from_fd(fds[0], NULL);
    g_assert_nonnull(sock);
    p->conn = g_socket_connection_factory_create_connection(sock);
    g_object_unref(sock);
    p->client = sftp_client_new(g_io_stream_get_input_stream(G_IO_STREAM(p->conn)),
                                g_io_stream_get_output_stream(G_IO_STREAM(p->conn)));
    return p;
}

static void pair_free(Pair* p) {
    sftp_client_free(p->client);
    for (gint i = 0; i < 20; i++) g_main_context_iteration(NULL, FALSE);
    g_io_stream_close(G_IO_STREAM(p->conn), NULL, NULL);
    g_object_unref(p->conn);
    g_thread_join(p->server.thread);
    g_free(p);
}

static void on_done(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    *(GAsyncResult**)data = g_object_ref(res);
}

static GAsyncResult* wait_for(GAsyncResult** slot) {
    while (*slot == NULL) g_main_context_iteration(NULL, TRUE);
    GAsyncResult* r = *slot;
    *slot = NULL;
    return r;
}

static GAsyncResult* result;

static gboolean finish_plain(GError** err) {
    GAsyncResult* r = wait_for(&result);
    gboolean ok = sftp_client_finish(r, err);
    g_object_unref(r);
    return ok;
}

static gboolean do_mkdir(SftpClient* c, const gchar* path, GError** err) {
    sftp_client_mkdir_async(c, path, 0755, NULL, on_done, &result);
    return finish_plain(err);
}

static gboolean do_write(SftpClient* c, const gchar* path, GBytes* data, guint32 mode, GError** err) {
    sftp_client_write_async(c, path, data, mode, NULL, on_done, &result);
    return finish_plain(err);
}

static gboolean do_rename(SftpClient* c, const gchar* from, const gchar* to, gboolean replace, GError** err) {
    sftp_client_rename_async(c, from, to, replace, NULL, on_done, &result);
    return finish_plain(err);
}

static GBytes* do_read(SftpClient* c, const gchar* path, guint64 max, guint64* size, GError** err) {
    sftp_client_read_async(c, path, max, NULL, on_done, &result);
    GAsyncResult* r = wait_for(&result);
    GBytes* b = sftp_client_read_finish(r, size, err);
    g_object_unref(r);
    return b;
}

static gboolean do_stat(SftpClient* c, const gchar* path, gboolean follow, SftpAttrs* a, GError** err) {
    sftp_client_stat_async(c, path, follow, NULL, on_done, &result);
    GAsyncResult* r = wait_for(&result);
    gboolean ok = sftp_client_stat_finish(r, a, err);
    g_object_unref(r);
    return ok;
}

static GPtrArray* do_list(SftpClient* c, const gchar* path, GError** err) {
    sftp_client_list_async(c, path, NULL, on_done, &result);
    GAsyncResult* r = wait_for(&result);
    GPtrArray* list = sftp_client_list_finish(r, err);
    g_object_unref(r);
    return list;
}

static GBytes* pattern(gsize n) {
    guint8* d = g_malloc(n);
    for (gsize i = 0; i < n; i++) d[i] = (guint8)((i * 7 + i / 251) & 0xff);
    return g_bytes_new_take(d, n);
}

static gchar* tmp_dir(void) {
    gchar* dir = g_dir_make_tmp("sftp-XXXXXX", NULL);
    g_assert_nonnull(dir);
    return dir;
}

static void rm_rf(const gchar* path) {
    const gchar* argv[] = { "rm", "-rf", path, NULL };
    g_spawn_sync(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */

static void test_proto(void) {
    GByteArray* b = sftp_packet_begin(SFTP_STAT);
    sftp_put_u32(b, 7);
    sftp_put_str(b, "/a b");
    sftp_put_u64(b, G_GUINT64_CONSTANT(0x0102030405060708));
    SftpAttrs a = { .flags = SFTP_ATTR_SIZE | SFTP_ATTR_PERMISSIONS, .size = 42, .permissions = 0100644 };
    sftp_put_attrs(b, &a);
    GBytes* pkt = sftp_packet_finish(b);
    gsize n = 0;
    const guint8* d = g_bytes_get_data(pkt, &n);
    g_assert_cmpuint(((guint32)d[0] << 24 | (guint32)d[1] << 16 | (guint32)d[2] << 8 | d[3]), ==, n - 4);

    SftpReader r;
    sftp_reader_init(&r, d + 4, n - 4);
    guint8 type = 0;
    guint32 id = 0;
    guint64 big = 0;
    g_assert_true(sftp_get_u8(&r, &type));
    g_assert_cmpuint(type, ==, SFTP_STAT);
    g_assert_true(sftp_get_u32(&r, &id));
    g_assert_cmpuint(id, ==, 7);
    gchar* s = sftp_get_str(&r);
    g_assert_cmpstr(s, ==, "/a b");
    g_free(s);
    g_assert_true(sftp_get_u64(&r, &big));
    g_assert_cmpuint(big, ==, G_GUINT64_CONSTANT(0x0102030405060708));
    SftpAttrs back;
    g_assert_true(sftp_get_attrs(&r, &back));
    g_assert_cmpuint(back.size, ==, 42);
    g_assert_true(sftp_attrs_is_reg(&back));
    g_assert_false(sftp_attrs_is_dir(&back));
    g_assert_cmpuint(r.left, ==, 0);
    g_assert_false(sftp_get_u8(&r, &type)); /* past the end */
    g_assert_false(r.ok);
    g_bytes_unref(pkt);

    /* A string longer than the packet */
    guint8 bad[] = { 0, 0, 0, 9, 'a' };
    sftp_reader_init(&r, bad, sizeof bad);
    g_assert_null(sftp_get_str(&r));

    GError* e = sftp_status_error(SFTP_NO_SUCH_FILE, "No such file");
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_error_free(e);
    e = sftp_status_error(SFTP_FAILURE, "disk on fire");
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_assert_cmpstr(e->message, ==, "disk on fire");
    g_error_free(e);
}

static void test_files(gconstpointer data) {
    gboolean posix_rename = GPOINTER_TO_INT(data);
    gchar* root = tmp_dir();
    Pair* p = pair_new(FALSE, posix_rename, 0);
    SftpClient* c = p->client;
    GError* err = NULL;

    /* Realpath */
    sftp_client_realpath_async(c, root, NULL, on_done, &result);
    GAsyncResult* r = wait_for(&result);
    gchar* real = sftp_client_realpath_finish(r, &err);
    g_object_unref(r);
    g_assert_no_error(err);
    g_assert_true(g_path_is_absolute(real));
    g_free(real);
    g_assert_cmpint(sftp_client_has_extension(c, "posix-rename@openssh.com"), ==, posix_rename);

    /* A folder, files in it */
    gchar* dir = g_build_filename(root, "sub", NULL);
    g_assert_true(do_mkdir(c, dir, &err));
    g_assert_false(do_mkdir(c, dir, &err)); /* already there */
    g_clear_error(&err);
    gchar* big = g_build_filename(dir, "big.bin", NULL);
    GBytes* contents = pattern(1200 * 1000 + 17); /* more than a window of chunks */
    g_assert_true(do_write(c, big, contents, 0644, &err));
    g_assert_no_error(err);
    gchar* disk = NULL;
    gsize disk_len = 0;
    g_assert_true(g_file_get_contents(big, &disk, &disk_len, NULL));
    GBytes* on_disk = g_bytes_new_take(disk, disk_len);
    g_assert_true(g_bytes_equal(contents, on_disk));
    g_bytes_unref(on_disk);

    guint64 size = 0;
    GBytes* back = do_read(c, big, G_MAXUINT64, &size, &err);
    g_assert_no_error(err);
    g_assert_true(g_bytes_equal(back, contents));
    g_assert_cmpuint(size, ==, g_bytes_get_size(contents));
    g_bytes_unref(back);
    back = do_read(c, big, 1000, &size, &err); /* the start only */
    g_assert_cmpuint(g_bytes_get_size(back), ==, 1000);
    g_assert_cmpuint(size, ==, g_bytes_get_size(contents));
    g_assert_cmpmem(g_bytes_get_data(back, NULL), 1000, g_bytes_get_data(contents, NULL), 1000);
    g_bytes_unref(back);

    gchar* empty = g_build_filename(dir, "empty", NULL);
    GBytes* none = g_bytes_new(NULL, 0);
    g_assert_true(do_write(c, empty, none, 0600, &err));
    back = do_read(c, empty, G_MAXUINT64, &size, &err);
    g_assert_no_error(err);
    g_assert_cmpuint(g_bytes_get_size(back), ==, 0);
    g_bytes_unref(back);
    g_bytes_unref(none);

    /* Stat, list */
    SftpAttrs a;
    g_assert_true(do_stat(c, dir, TRUE, &a, &err));
    g_assert_true(sftp_attrs_is_dir(&a));
    g_assert_true(do_stat(c, big, FALSE, &a, &err));
    g_assert_cmpuint(a.size, ==, g_bytes_get_size(contents));
    g_assert_false(do_stat(c, "/no/such/file", TRUE, &a, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error(&err);

    for (gint i = 0; i < 7; i++) { /* several READDIR replies */
        gchar* f = g_strdup_printf("%s/f%d", dir, i);
        g_assert_true(g_file_set_contents(f, "x", 1, NULL));
        g_free(f);
    }
    GPtrArray* list = do_list(c, dir, &err);
    g_assert_no_error(err);
    g_assert_cmpuint(list->len, ==, 9); /* big.bin, empty, f0..f6; no "." nor ".." */
    gboolean saw_big = FALSE;
    for (guint i = 0; i < list->len; i++) {
        SftpEntry* e = g_ptr_array_index(list, i);
        g_assert_true(strcmp(e->name, ".") != 0 && strcmp(e->name, "..") != 0);
        if (strcmp(e->name, "big.bin") == 0) {
            saw_big = TRUE;
            g_assert_true(sftp_attrs_is_reg(&e->attrs));
            g_assert_cmpuint(e->attrs.size, ==, g_bytes_get_size(contents));
        }
    }
    g_assert_true(saw_big);
    g_ptr_array_unref(list);
    g_assert_null(do_list(c, "/no/such/folder", &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error(&err);

    /* Rewrite: the mode stays, no temporary file is left */
    g_assert_cmpint(g_chmod(big, 0640), ==, 0);
    GBytes* small = g_bytes_new_static("hello\n", 6);
    g_assert_true(do_write(c, big, small, 0644, &err));
    g_assert_no_error(err);
    struct stat st;
    g_assert_cmpint(g_stat(big, &st), ==, 0);
    g_assert_cmpint(st.st_mode & 0777, ==, 0640);
    g_assert_cmpint(st.st_size, ==, 6);
    list = do_list(c, dir, &err);
    g_assert_cmpuint(list->len, ==, 9);
    g_ptr_array_unref(list);

    /* Through a link: the link stays a link */
    gchar* link = g_build_filename(dir, "link", NULL);
    g_assert_cmpint(symlink(big, link), ==, 0);
    GBytes* via = g_bytes_new_static("via link\n", 9);
    g_assert_true(do_write(c, link, via, 0644, &err));
    g_assert_no_error(err);
    g_assert_cmpint(lstat(link, &st), ==, 0);
    g_assert_true(S_ISLNK(st.st_mode));
    gchar* text = NULL;
    g_assert_true(g_file_get_contents(big, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "via link\n");
    g_free(text);
    g_bytes_unref(via);

    /* Rename: not over a file unless asked */
    gchar* moved = g_build_filename(dir, "moved", NULL);
    g_assert_true(do_rename(c, empty, moved, FALSE, &err));
    g_assert_false(g_file_test(empty, G_FILE_TEST_EXISTS));
    gchar* f0 = g_build_filename(dir, "f0", NULL);
    g_assert_false(do_rename(c, moved, f0, FALSE, &err));
    g_clear_error(&err);
    g_assert_true(do_rename(c, moved, f0, TRUE, &err));
    g_assert_no_error(err);
    g_assert_cmpint(g_stat(f0, &st), ==, 0);
    g_assert_cmpint(st.st_size, ==, 0);

    /* Remove */
    sftp_client_remove_async(c, f0, NULL, on_done, &result);
    g_assert_true(finish_plain(&err));
    g_assert_false(g_file_test(f0, G_FILE_TEST_EXISTS));
    sftp_client_rmdir_async(c, dir, NULL, on_done, &result);
    g_assert_false(finish_plain(&err)); /* not empty */
    g_clear_error(&err);

    g_bytes_unref(small);
    g_bytes_unref(contents);
    g_free(f0);
    g_free(moved);
    g_free(link);
    g_free(empty);
    g_free(big);
    g_free(dir);
    pair_free(p);
    rm_rf(root);
    g_free(root);
}

/* A server that sends less than asked: the client asks for the rest */
static void test_short_reads(void) {
    gchar* root = tmp_dir();
    Pair* p = pair_new(TRUE, TRUE, 0);
    GError* err = NULL;
    gchar* path = g_build_filename(root, "f", NULL);
    GBytes* contents = pattern(100 * 1000 + 3);
    gsize n = 0;
    const gchar* d = g_bytes_get_data(contents, &n);
    g_assert_true(g_file_set_contents(path, d, (gssize)n, NULL));
    GBytes* back = do_read(p->client, path, G_MAXUINT64, NULL, &err);
    g_assert_no_error(err);
    g_assert_true(g_bytes_equal(back, contents));
    g_bytes_unref(back);
    back = do_read(p->client, path, 70 * 1000 + 1, NULL, &err);
    g_assert_cmpuint(g_bytes_get_size(back), ==, 70 * 1000 + 1);
    g_assert_cmpmem(g_bytes_get_data(back, NULL), 70 * 1000 + 1, d, 70 * 1000 + 1);
    g_bytes_unref(back);
    g_bytes_unref(contents);
    g_free(path);
    pair_free(p);
    rm_rf(root);
    g_free(root);
}

/* The server hangs up: what was asked fails, and what comes after */
static void test_connection_lost(void) {
    gchar* root = tmp_dir();
    Pair* p = pair_new(FALSE, FALSE, 2); /* INIT, one request, then gone */
    GError* err = NULL;
    SftpAttrs a;
    g_assert_true(do_stat(p->client, root, TRUE, &a, &err));
    g_assert_true(sftp_client_is_open(p->client));
    g_assert_false(do_stat(p->client, root, TRUE, &a, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED);
    g_clear_error(&err);
    g_assert_false(sftp_client_is_open(p->client));
    g_assert_nonnull(sftp_client_error(p->client));
    g_assert_null(do_list(p->client, root, &err)); /* after: at once */
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED);
    g_clear_error(&err);
    pair_free(p);

    /* Freed with a request running: it fails with G_IO_ERROR_CLOSED */
    p = pair_new(FALSE, FALSE, 0);
    sftp_client_stat_async(p->client, root, TRUE, NULL, on_done, &result);
    sftp_client_free(p->client);
    p->client = NULL;
    GAsyncResult* r = wait_for(&result);
    g_assert_false(sftp_client_stat_finish(r, &a, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_CLOSED);
    g_clear_error(&err);
    g_object_unref(r);
    pair_free(p);
    rm_rf(root);
    g_free(root);
}

/* A cancelled read still closes its file and reports the cancel */
static void test_cancel(void) {
    gchar* root = tmp_dir();
    Pair* p = pair_new(FALSE, FALSE, 0);
    gchar* path = g_build_filename(root, "f", NULL);
    GBytes* contents = pattern(2 * 1000 * 1000);
    gsize n = 0;
    const gchar* d = g_bytes_get_data(contents, &n);
    g_assert_true(g_file_set_contents(path, d, (gssize)n, NULL));
    GCancellable* cancel = g_cancellable_new();
    sftp_client_read_async(p->client, path, G_MAXUINT64, cancel, on_done, &result);
    g_cancellable_cancel(cancel);
    GAsyncResult* r = wait_for(&result);
    GError* err = NULL;
    g_assert_null(sftp_client_read_finish(r, NULL, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&err);
    g_object_unref(r);
    g_assert_true(sftp_client_is_open(p->client)); /* the connection goes on */
    GBytes* back = do_read(p->client, path, 10, NULL, &err);
    g_assert_cmpuint(g_bytes_get_size(back), ==, 10);
    g_bytes_unref(back);
    g_object_unref(cancel);
    g_bytes_unref(contents);
    g_free(path);
    pair_free(p);
    rm_rf(root);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/sftp/proto", test_proto);
    g_test_add_data_func("/sftp/files", GINT_TO_POINTER(FALSE), test_files);
    g_test_add_data_func("/sftp/files_posix_rename", GINT_TO_POINTER(TRUE), test_files);
    g_test_add_func("/sftp/short_reads", test_short_reads);
    g_test_add_func("/sftp/connection_lost", test_connection_lost);
    g_test_add_func("/sftp/cancel", test_cancel);
    return g_test_run();
}
