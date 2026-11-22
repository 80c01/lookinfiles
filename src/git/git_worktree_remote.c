/* The uncommitted changes of a remote machine's work tree (see git_worktree_remote.h) */
#include "git/git_worktree_remote.h"
#include "core/ssh_session.h"
#include "core/vfs.h"

#include <string.h>

/* Line counts of a numstat part: path -> gint[2] (added, removed; -1 binary) */
static GHashTable* parse_numstat(const gchar* p, const gchar* end) {
    GHashTable* out = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    while (p < end) {
        const gchar* nul = memchr(p, '\0', (gsize)(end - p));
        if (nul == NULL) break;
        const gchar* rec = p;
        p = nul + 1;
        const gchar* t1 = memchr(rec, '\t', (gsize)(nul - rec));
        const gchar* t2 = t1 != NULL ? memchr(t1 + 1, '\t', (gsize)(nul - t1 - 1)) : NULL;
        if (t2 == NULL) continue;
        gint* n = g_new(gint, 2);
        n[0] = rec[0] == '-' ? -1 : (gint)g_ascii_strtoll(rec, NULL, 10);
        n[1] = t1[1] == '-' ? -1 : (gint)g_ascii_strtoll(t1 + 1, NULL, 10);
        const gchar* path = t2 + 1;
        if (path[0] == '\0') { /* a rename: "\0old\0new\0" follow */
            const gchar* old_end = memchr(p, '\0', (gsize)(end - p));
            if (old_end == NULL) {
                g_free(n);
                break;
            }
            const gchar* new_path = old_end + 1;
            const gchar* new_end = memchr(new_path, '\0', (gsize)(end - new_path));
            if (new_end == NULL) {
                g_free(n);
                break;
            }
            g_hash_table_replace(out, g_strdup(new_path), n);
            p = new_end + 1;
        } else {
            g_hash_table_replace(out, g_strdup(path), n);
        }
    }
    return out;
}

/* The end of the part starting at p: its "\001" record, or end */
static const gchar* part_end(const gchar* p, const gchar* end, const gchar** next) {
    for (const gchar* q = p; q < end;) {
        const gchar* nul = memchr(q, '\0', (gsize)(end - q));
        if (nul == NULL) break;
        if (nul - q == 1 && q[0] == '\001') {
            *next = nul + 1;
            return q;
        }
        q = nul + 1;
    }
    *next = end;
    return end;
}

static GitChange* change_new(const gchar* path, const gchar* old, GitChangeGroup group, gchar status,
                             GHashTable* counts) {
    GitChange* c = g_new0(GitChange, 1);
    c->path = g_strdup(path);
    c->old_path = g_strdup(old);
    c->group = group;
    c->status = status;
    const gint* n = counts != NULL ? g_hash_table_lookup(counts, path) : NULL;
    c->added = n != NULL ? n[0] : 0;
    c->removed = n != NULL ? n[1] : 0;
    return c;
}

static gint by_group_and_path(gconstpointer a, gconstpointer b) {
    const GitChange* x = *(GitChange* const*)a;
    const GitChange* y = *(GitChange* const*)b;
    if (x->group != y->group) return x->group < y->group ? -1 : 1;
    return strcmp(x->path, y->path);
}

GPtrArray* git_remote_parse_changes(const gchar* data, gsize len, guint max, gboolean* more) {
    if (more != NULL) *more = FALSE;
    const gchar* end = data + len;
    const gchar* next = NULL;
    const gchar* status_end = part_end(data, end, &next);
    const gchar* staged_start = next;
    const gchar* staged_end = part_end(staged_start, end, &next);
    const gchar* unstaged_start = next;
    const gchar* unstaged_end = part_end(unstaged_start, end, &next);
    const gchar* untracked_start = next;
    GHashTable* staged = parse_numstat(staged_start, staged_end);
    GHashTable* unstaged = parse_numstat(unstaged_start, unstaged_end);
    /* Untracked: "path\0count\n" */
    GHashTable* lines = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (const gchar* p = untracked_start; p < end;) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        if (nl == NULL) break;
        const gchar* nul = memchr(p, '\0', (gsize)(nl - p));
        if (nul != NULL) {
            gint* n = g_new(gint, 2);
            n[0] = (gint)g_ascii_strtoll(nul + 1, NULL, 10);
            n[1] = 0;
            g_hash_table_replace(lines, g_strndup(p, (gsize)(nul - p)), n);
        }
        p = nl + 1;
    }

    GPtrArray* out = g_ptr_array_new_with_free_func(git_change_free);
    for (const gchar* p = data; p < status_end;) {
        const gchar* nul = memchr(p, '\0', (gsize)(status_end - p));
        if (nul == NULL) break;
        const gchar* rec = p;
        p = nul + 1;
        if (nul - rec < 4 || rec[2] != ' ') continue;
        gchar x = rec[0], y = rec[1];
        const gchar* path = rec + 3;
        const gchar* old = NULL;
        if (x == 'R' || x == 'C') { /* "R  new\0old" */
            old = p;
            const gchar* old_end = memchr(p, '\0', (gsize)(status_end - p));
            if (old_end == NULL) break;
            p = old_end + 1;
        }
        if (x == '!' && y == '!') continue;
        if (x == '?' && y == '?') {
            g_ptr_array_add(out, change_new(path, NULL, GIT_CHANGE_UNTRACKED, 'U', lines));
            continue;
        }
        if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) {
            g_ptr_array_add(out, change_new(path, NULL, GIT_CHANGE_UNSTAGED, '!', unstaged));
            continue;
        }
        if (x != ' ') {
            gchar st = x == 'C' ? 'A' : x;
            g_ptr_array_add(out, change_new(path, x == 'R' ? old : NULL, GIT_CHANGE_STAGED, st, staged));
        }
        if (y != ' ') g_ptr_array_add(out, change_new(path, NULL, GIT_CHANGE_UNSTAGED, y, unstaged));
    }
    g_ptr_array_sort(out, by_group_and_path);
    if (max > 0 && out->len > max) {
        g_ptr_array_set_size(out, max);
        if (more != NULL) *more = TRUE;
    }
    g_hash_table_destroy(staged);
    g_hash_table_destroy(unstaged);
    g_hash_table_destroy(lines);
    return out;
}

/* Run command at top: its output, NULL when it could not run or failed */
static GBytes* run(const gchar* top, const gchar* command, GCancellable* cancel) {
    gint status = 0;
    GBytes* out = NULL;
    if (!vfs_exec(top, command, NULL, cancel, &status, &out, NULL)) return NULL;
    if (status != 0) g_clear_pointer(&out, g_bytes_unref);
    return out;
}

GPtrArray* git_remote_read_changes(const gchar* top, guint max, GCancellable* cancel, gboolean* more) {
    GBytes* out = run(top, GIT_WORKTREE_REMOTE_COMMAND, cancel);
    if (out == NULL || g_cancellable_is_cancelled(cancel)) {
        if (out != NULL) g_bytes_unref(out);
        return NULL;
    }
    gsize len = 0;
    const gchar* data = g_bytes_get_data(out, &len);
    GPtrArray* changes = git_remote_parse_changes(data != NULL ? data : "", len, max, more);
    g_bytes_unref(out);
    return changes;
}

gint git_remote_count_changes(const gchar* top, GCancellable* cancel) {
    GBytes* out = run(top, "git status --porcelain=v1 -z --untracked-files=all 2>/dev/null || exit 3; printf '\\001\\0'",
                      cancel);
    if (out == NULL) return -1;
    gsize len = 0;
    const gchar* data = g_bytes_get_data(out, &len);
    GPtrArray* changes = git_remote_parse_changes(data != NULL ? data : "", len, 0, NULL);
    gint n = (gint)changes->len;
    g_ptr_array_unref(changes);
    g_bytes_unref(out);
    return n;
}

gchar* git_remote_patch_of(const gchar* diff, const gchar* path) {
    gchar* suffix = g_strconcat(" b/", path, NULL);
    gsize slen = strlen(suffix);
    const gchar* block = NULL;
    for (const gchar* p = diff; p != NULL && *p != '\0';) {
        const gchar* line_end = strchr(p, '\n');
        gsize ll = line_end != NULL ? (gsize)(line_end - p) : strlen(p);
        if (g_str_has_prefix(p, "diff --git ")) {
            if (block != NULL) {
                g_free(suffix);
                return g_strndup(block, (gsize)(p - block)); /* up to the next file */
            }
            if (ll >= slen && memcmp(p + ll - slen, suffix, slen) == 0) block = p;
        }
        p = line_end != NULL ? line_end + 1 : NULL;
    }
    g_free(suffix);
    return g_strdup(block != NULL ? block : "");
}

/* Patches longer than this are cut (as the view shows them) */
#define PATCH_MAX (8 * 1024 * 1024)

gchar* git_remote_change_patch(const gchar* top, const gchar* path, GitChangeGroup group) {
    if (path == NULL || path[0] == '\0') return NULL;
    gchar* q = ssh_session_quote(path);
    /* Staged: the whole index, for the renames (as git_worktree.c); untracked: the whole file */
    gchar* cmd = group == GIT_CHANGE_STAGED
                     ? g_strdup_printf("git -c core.quotepath=off diff --cached -M 2>/dev/null | head -c %d; exit 0",
                                       PATCH_MAX)
                 : group == GIT_CHANGE_UNSTAGED
                     ? g_strdup_printf("git -c core.quotepath=off diff -- %s 2>/dev/null | head -c %d; exit 0", q,
                                       PATCH_MAX)
                     : g_strdup_printf("git -c core.quotepath=off diff --no-index -- /dev/null %s 2>/dev/null"
                                       " | head -c %d; exit 0", q, PATCH_MAX);
    GBytes* out = run(top, cmd, NULL);
    g_free(cmd);
    g_free(q);
    if (out == NULL) return NULL;
    gsize len = 0;
    const gchar* data = g_bytes_get_data(out, &len);
    gchar* text = g_utf8_make_valid(data != NULL ? data : "", (gssize)len);
    g_bytes_unref(out);
    gchar* patch = git_remote_patch_of(text, path);
    g_free(text);
    return patch;
}
