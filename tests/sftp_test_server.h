/* A small SFTP v3 server for the tests (tests/test_sftp_client.c,
 * tests/test_vfs.c): POSIX calls on the paths asked, as OpenSSH's
 * sftp-server; server_main() serves until its input closes. */
#ifndef SFTP_TEST_SERVER_H
#define SFTP_TEST_SERVER_H

#include "core/sftp_proto.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* The server                                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    int fd;     /* requests come in there */
    int out_fd; /* replies go there (fd for a socket) */
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

G_GNUC_UNUSED static gpointer server_main(gpointer data) {
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
            write_bytes(s->out_fd, sftp_packet_finish(b));
        } else {
            guint32 id = 0;
            sftp_get_u32(&r, &id);
            write_bytes(s->out_fd, serve_one(s, type, id, &r, handles));
        }
        g_free(body);
    }
    close(s->fd);
    if (s->out_fd != s->fd) close(s->out_fd);
    g_ptr_array_unref(handles);
    return NULL;
}

#endif /* SFTP_TEST_SERVER_H */
