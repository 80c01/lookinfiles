/* Local copies of the .git folder of remote repositories (see git_mirror.h) */
#include "git/git_mirror.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include "core/ssh_process.h"
#include "core/vfs.h"
#include "git/git_remote.h"
#include "git/git_utils.h"

#include <glib/gstdio.h>
#include <string.h>

#define MANIFEST_FILE ".lookin-manifest" /* the listing the copy was made from, beside .git */
#define TAR_BLOCK 512

GHashTable* git_mirror_parse_listing(const gchar* data, gsize len) {
    GHashTable* out = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const gchar* end = data + len;
    for (const gchar* p = data; p < end;) {
        const gchar* nul = memchr(p, '\0', (gsize)(end - p));
        if (nul == NULL) break; /* cut short */
        const gchar* rec = p;
        p = nul + 1;
        const gchar* s1 = strchr(rec, ' ');
        const gchar* s2 = s1 != NULL ? strchr(s1 + 1, ' ') : NULL;
        if (s1 == NULL || s2 == NULL || s2[1] == '\0') continue;
        g_hash_table_replace(out, g_strdup(s2 + 1), g_strndup(rec, (gsize)(s2 - rec)));
    }
    return out;
}

/* A path of the archive that stays below its folder */
static gboolean safe_path(const gchar* rel) {
    if (rel[0] == '\0' || rel[0] == '/') return FALSE;
    gchar** parts = g_strsplit(rel, "/", -1);
    gboolean ok = TRUE;
    for (gchar** q = parts; *q != NULL && ok; q++) ok = strcmp(*q, "..") != 0;
    g_strfreev(parts);
    return ok;
}

/* The octal number of a header field */
static guint64 octal(const guchar* field, gsize len) {
    guint64 v = 0;
    for (gsize i = 0; i < len && field[i] != '\0' && field[i] != ' '; i++) {
        if (field[i] < '0' || field[i] > '7') break;
        v = v * 8 + (guint64)(field[i] - '0');
    }
    return v;
}

static gboolean read_block(GInputStream* in, guchar* buf, gsize n, GCancellable* cancel, GError** error) {
    gsize got = 0;
    if (!g_input_stream_read_all(in, buf, n, &got, cancel, error)) return FALSE;
    if (got != n) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT, _("the copy of the repository was cut short"));
        return FALSE;
    }
    return TRUE;
}

/* Write size bytes of in to path (beside, then renamed), the padding read */
static gboolean extract_file(GInputStream* in, const gchar* path, guint64 size, GCancellable* cancel, GError** error) {
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    gchar* part = g_strconcat(path, ".part", NULL);
    FILE* f = g_fopen(part, "wb");
    gboolean ok = f != NULL;
    if (!ok) g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("the copy of the repository cannot be written"));
    guchar buf[64 * TAR_BLOCK];
    guint64 left = size;
    while (ok && left > 0) {
        gsize n = (gsize)MIN(left, (guint64)sizeof buf);
        ok = read_block(in, buf, n, cancel, error);
        if (ok && fwrite(buf, 1, n, f) != n) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("the copy of the repository cannot be written"));
            ok = FALSE;
        }
        left -= n;
    }
    if (f != NULL && fclose(f) != 0) ok = FALSE;
    gsize pad = (gsize)((TAR_BLOCK - size % TAR_BLOCK) % TAR_BLOCK);
    if (ok && pad > 0) ok = read_block(in, buf, pad, cancel, error);
    if (ok && g_rename(part, path) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("the copy of the repository cannot be written"));
        ok = FALSE;
    }
    if (!ok) g_unlink(part);
    g_free(part);
    return ok;
}

/* Skip size bytes of in and their padding */
static gboolean skip_data(GInputStream* in, guint64 size, GCancellable* cancel, GError** error) {
    guint64 total = (size + TAR_BLOCK - 1) / TAR_BLOCK * TAR_BLOCK;
    guchar buf[TAR_BLOCK];
    for (guint64 done = 0; done < total; done += TAR_BLOCK) {
        if (!read_block(in, buf, TAR_BLOCK, cancel, error)) return FALSE;
    }
    return TRUE;
}

gboolean git_mirror_extract(GInputStream* in, const gchar* dir, GCancellable* cancel, guint* files, GError** error) {
    if (files != NULL) *files = 0;
    guchar h[TAR_BLOCK];
    gchar* long_name = NULL; /* a GNU 'L' entry: the name of the next one */
    for (;;) {
        gsize got = 0;
        if (!g_input_stream_read_all(in, h, TAR_BLOCK, &got, cancel, error)) {
            g_free(long_name);
            return FALSE;
        }
        gboolean zero = got == TAR_BLOCK;
        for (gsize i = 0; zero && i < TAR_BLOCK; i++) zero = h[i] == 0;
        if (got == 0 || zero) break; /* the end */
        if (got != TAR_BLOCK) {
            g_free(long_name);
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT,
                                _("the copy of the repository was cut short"));
            return FALSE;
        }
        guint64 size = octal(h + 124, 12);
        gchar type = (gchar)h[156];
        if (type == 'L') { /* the long name, in the data */
            g_free(long_name);
            long_name = g_malloc0(size + 1);
            guint64 total = (size + TAR_BLOCK - 1) / TAR_BLOCK * TAR_BLOCK;
            guchar* data = g_malloc(total > 0 ? total : 1);
            gboolean ok = total == 0 || read_block(in, data, (gsize)total, cancel, error);
            if (ok) memcpy(long_name, data, size);
            g_free(data);
            if (!ok) {
                g_free(long_name);
                return FALSE;
            }
            continue;
        }
        gchar* name;
        if (long_name != NULL) {
            name = g_steal_pointer(&long_name);
        } else {
            gchar n[101], prefix[156];
            memcpy(n, h, 100);
            n[100] = '\0';
            memcpy(prefix, h + 345, 155);
            prefix[155] = '\0';
            name = prefix[0] != '\0' ? g_strconcat(prefix, "/", n, NULL) : g_strdup(n);
        }
        const gchar* rel = name;
        while (rel[0] == '.' && rel[1] == '/') rel += 2;
        gboolean ok;
        if ((type == '0' || type == '\0') && safe_path(rel)) {
            gchar* path = g_build_filename(dir, rel, NULL);
            ok = extract_file(in, path, size, cancel, error);
            g_free(path);
            if (ok && files != NULL) (*files)++;
        } else {
            ok = skip_data(in, type == '5' ? 0 : size, cancel, error); /* folders, links, others: nothing */
        }
        g_free(name);
        if (!ok) return FALSE;
    }
    g_free(long_name);
    return TRUE;
}

gchar* git_mirror_root(const gchar* top) {
    gchar* id = g_compute_checksum_for_string(G_CHECKSUM_SHA1, top, -1);
    id[16] = '\0';
    gchar* cache = app_cache_dir();
    gchar* root = g_build_filename(cache, "git-mirrors", id, NULL);
    g_free(cache);
    g_free(id);
    return root;
}

/* One sync of a repository at a time */
static GMutex busy_lock;
static GCond busy_cond;
static GHashTable* busy;

static void busy_enter(const gchar* root) {
    g_mutex_lock(&busy_lock);
    if (busy == NULL) busy = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    while (g_hash_table_contains(busy, root)) g_cond_wait(&busy_cond, &busy_lock);
    g_hash_table_add(busy, g_strdup(root));
    g_mutex_unlock(&busy_lock);
}

static void busy_leave(const gchar* root) {
    g_mutex_lock(&busy_lock);
    g_hash_table_remove(busy, root);
    g_cond_broadcast(&busy_cond);
    g_mutex_unlock(&busy_lock);
}

typedef struct {
    GOutputStream* out;
    GBytes* names;
} Feed;

/* The names to tar, written while its output is read (both pipes can fill) */
static gpointer feed_names(gpointer data) {
    Feed* f = data;
    gsize len = 0;
    const gchar* bytes = g_bytes_get_data(f->names, &len);
    g_output_stream_write_all(f->out, bytes, len, NULL, NULL, NULL);
    g_output_stream_close(f->out, NULL, NULL);
    return NULL;
}

/* Copy the files named (NUL-separated) from top's .git into gitdir */
static gboolean copy_files(const gchar* top, GBytes* names, const gchar* gitdir, GCancellable* cancel, guint* copied,
                           GError** error) {
    SshProcess* p = vfs_spawn(top, GIT_MIRROR_TAR_COMMAND, error);
    if (p == NULL) return FALSE;
    Feed f = { ssh_process_get_stdin(p), names };
    GThread* t = g_thread_new("git-mirror-names", feed_names, &f);
    gboolean ok = git_mirror_extract(ssh_process_get_stdout(p), gitdir, cancel, copied, error);
    if (!ok) ssh_process_force_exit(p);
    g_thread_join(t);
    ssh_process_wait(p);
    ssh_process_free(p);
    return ok;
}

gboolean git_mirror_sync(const gchar* top, GCancellable* cancel, guint* copied, GError** error) {
    if (copied != NULL) *copied = 0;
    gchar* root = git_mirror_root(top);
    busy_enter(root);
    gint status = 0;
    GBytes* listing = NULL;
    gboolean ok = vfs_exec(top, GIT_MIRROR_LIST_COMMAND, NULL, cancel, &status, &listing, error);
    gsize len = 0;
    const gchar* data = ok ? g_bytes_get_data(listing, &len) : NULL;
    if (ok && (status != 0 || len == 0)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, _("no Git repository there"));
        ok = FALSE;
    }
    gchar* gitdir = g_build_filename(root, ".git", NULL);
    gchar* manifest_path = g_build_filename(root, MANIFEST_FILE, NULL);
    if (ok) {
        GHashTable* remote = git_mirror_parse_listing(data, len);
        gchar* old_text = NULL;
        gsize old_len = 0;
        GHashTable* local = g_file_get_contents(manifest_path, &old_text, &old_len, NULL)
                                ? git_mirror_parse_listing(old_text, old_len)
                                : g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        g_free(old_text);
        /* What changed: copied; what went: removed */
        GString* wanted = g_string_new(NULL);
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, remote);
        while (g_hash_table_iter_next(&it, &k, &v)) {
            gchar* path = g_build_filename(gitdir, k, NULL);
            if (g_strcmp0(g_hash_table_lookup(local, k), v) != 0 || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
                g_string_append_len(wanted, k, (gssize)strlen(k) + 1);
            }
            g_free(path);
        }
        g_hash_table_iter_init(&it, local);
        while (g_hash_table_iter_next(&it, &k, NULL)) {
            if (g_hash_table_contains(remote, k) || !safe_path(k)) continue;
            gchar* path = g_build_filename(gitdir, k, NULL);
            g_unlink(path);
            g_free(path);
        }
        g_mkdir_with_parents(gitdir, 0700);
        if (wanted->len > 0) {
            GBytes* names = g_string_free_to_bytes(wanted);
            ok = copy_files(top, names, gitdir, cancel, copied, error);
            g_bytes_unref(names);
        } else {
            g_string_free(wanted, TRUE);
        }
        /* The manifest only once the copy is whole: a sync cut short starts again */
        if (ok) ok = g_file_set_contents(manifest_path, data, (gssize)len, error);
        g_hash_table_destroy(local);
        g_hash_table_destroy(remote);
    }
    if (listing != NULL) g_bytes_unref(listing);
    busy_leave(root);
    g_free(manifest_path);
    g_free(gitdir);
    g_free(root);
    return ok;
}

gchar* git_mirror_head_blob(const gchar* path, gsize* len, GCancellable* cancel) {
    gchar* dir = g_path_get_dirname(path);
    gchar* top = git_remote_top(dir, cancel);
    g_free(dir);
    gsize n = top != NULL ? strlen(top) : 0;
    gchar* blob = NULL;
    if (n > 0 && strncmp(path, top, n) == 0 && path[n] == '/' && git_mirror_sync(top, cancel, NULL, NULL)) {
        gchar* root = git_mirror_root(top);
        gchar* copy = g_build_filename(root, path + n + 1, NULL); /* the same file below the copy */
        blob = git_read_head_blob(copy, len);
        g_free(copy);
        g_free(root);
    }
    g_free(top);
    return blob;
}
