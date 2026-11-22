/* The room of a git work tree in a disk scan (see git_space.h) */
#include "git/git_space.h"
#include "git/git_utils.h"
#include "core/vfs.h"
#include "git/git_remote.h"

#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    DiskScan* scan;
    const gchar* root;   /* the scan's root path */
    gchar* prefix;       /* the root below the top, '/'-separated with a final
                            '/', "" when the root is the top */
    gsize prefix_len;
    GCancellable* cancel;
    GitSpace* space;
    GHashTable* dirs;    /* folder below the root ("" the root, '/'-separated)
                            -> its DiskNode */
    GHashTable* listed;  /* files listed (tracked or untracked), below the root */
    GHashTable* holders; /* folders holding a listed file or a nested
                            repository, at any depth */
    GHashTable* nested;  /* folders of the nested repositories */
    GHashTable* inodes;  /* "dev:ino" of the files with several links counted */
} Ctx;

static gboolean is_symlink(const GStatBuf* st) {
#ifdef S_ISLNK
    return S_ISLNK(st->st_mode);
#else
    (void)st; /* no symbolic links in the stat modes (Windows) */
    return FALSE;
#endif
}

static void entry_clear(gpointer data) {
    g_free(((GitSpaceEntry*)data)->path);
}

static GArray* entries_new(void) {
    GArray* a = g_array_new(FALSE, TRUE, sizeof(GitSpaceEntry));
    g_array_set_clear_func(a, entry_clear);
    return a;
}

void git_space_free(GitSpace* space) {
    if (space == NULL) return;
    g_free(space->top);
    g_free(space->top_shown);
    g_array_unref(space->ignored_top);
    g_array_unref(space->tracked_top);
    g_free(space);
}

static guint64 minus(guint64 a, guint64 b) {
    return a > b ? a - b : 0;
}

/* Every folder of the tree, by its path below the root */
static void index_dirs(GHashTable* dirs, DiskNode* node, const gchar* rel) {
    g_hash_table_insert(dirs, g_strdup(rel), node);
    for (guint i = 0; node->children != NULL && i < node->children->len; i++) {
        DiskNode* c = g_ptr_array_index(node->children, i);
        gchar* crel = rel[0] != '\0' ? g_strconcat(rel, "/", c->name, NULL) : g_strdup(c->name);
        index_dirs(dirs, c, crel);
        g_free(crel);
    }
}

/* rel's folder and the folders above it hold something listed */
static void add_holders(GHashTable* holders, const gchar* rel) {
    gchar* dir = g_strdup(rel);
    for (;;) {
        gchar* slash = strrchr(dir, '/');
        if (slash == NULL) {
            dir[0] = '\0';
        } else {
            *slash = '\0';
        }
        if (!g_hash_table_add(holders, g_strdup(dir)) || dir[0] == '\0') break; /* the ones above are in */
    }
    g_free(dir);
}

/* Keep e among the GIT_SPACE_TOP largest of top (largest first); takes its path */
static void keep_largest(GArray* top, GitSpaceEntry e) {
    guint at = top->len;
    while (at > 0 && g_array_index(top, GitSpaceEntry, at - 1).size < e.size) at--;
    if (at >= GIT_SPACE_TOP) {
        g_free(e.path);
        return;
    }
    g_array_insert_val(top, at, e);
    if (top->len > GIT_SPACE_TOP) g_array_remove_index(top, top->len - 1);
}

static gboolean on_file(const gchar* rel, GitFileKind kind, gpointer data) {
    Ctx* c = data;
    if (g_cancellable_is_cancelled(c->cancel)) return FALSE;
    if (c->prefix_len > 0 && strncmp(rel, c->prefix, c->prefix_len) != 0) return TRUE; /* beside the root */
    gchar* r = g_strdup(rel + c->prefix_len);
    gsize len = strlen(r);
    while (len > 0 && r[len - 1] == '/') r[--len] = '\0'; /* "repo/": a nested repository */
    if (len == 0) {
        g_free(r);
        return TRUE;
    }
    const gchar* slash = strrchr(r, '/');
    gchar* parent = slash != NULL ? g_strndup(r, (gsize)(slash - r)) : g_strdup("");
    gboolean read = g_hash_table_contains(c->dirs, parent); /* else excluded, another filesystem... */
    g_free(parent);
    gchar* full = read ? g_build_filename(c->root, r, NULL) : NULL;
    GStatBuf st;
    if (full == NULL || disk_scan_is_excluded(c->scan, full) || !disk_scan_lstat(c->scan, full, &st)) {
        g_free(full);
        g_free(r);
        return TRUE;
    }
    GitSpace* sp = c->space;
    if (S_ISDIR(st.st_mode)) { /* a submodule or a repository of its own */
        DiskNode* node = g_hash_table_lookup(c->dirs, r);
        if (node != NULL && g_hash_table_add(c->nested, g_strdup(r))) {
            sp->nested.size += node->size;
            sp->nested.count += node->n_files;
            add_holders(c->holders, r);
        }
        g_free(full);
        g_free(r);
        return TRUE;
    }
    gboolean regular = S_ISREG(st.st_mode);
    if (!regular && !is_symlink(&st)) {
        g_free(full);
        g_free(r);
        return TRUE;
    }
#ifdef G_OS_UNIX
    if (regular && st.st_nlink > 1) { /* the scan counts a file once */
        gchar* key = g_strdup_printf("%" G_GUINT64_FORMAT ":%" G_GUINT64_FORMAT, (guint64)st.st_dev,
                                     (guint64)st.st_ino);
        if (!g_hash_table_add(c->inodes, key)) {
            g_free(full);
            g_free(r);
            return TRUE;
        }
    }
#endif
    guint64 size = st.st_size > 0 ? (guint64)st.st_size : 0;
    DiskTally* t = kind == GIT_FILE_TRACKED ? &sp->tracked : &sp->untracked;
    t->size += size;
    if (regular) t->count++;
    add_holders(c->holders, r);
    if (kind == GIT_FILE_TRACKED && regular && size > 0) {
        keep_largest(sp->tracked_top, (GitSpaceEntry){ full, size, 1, FALSE });
        full = NULL;
    }
    g_hash_table_add(c->listed, r);
    g_free(full);
    return TRUE;
}

static gint by_size(gconstpointer a, gconstpointer b) {
    guint64 x = ((const GitSpaceEntry*)a)->size, y = ((const GitSpaceEntry*)b)->size;
    return x < y ? 1 : x > y ? -1 : 0;
}

/* The ignored entries below node (at rel): the folders holding nothing
 * listed, and the files kept by the scan that are not listed */
static void find_ignored(Ctx* c, DiskNode* node, const gchar* rel, GArray* out) {
    for (guint i = 0; node->children != NULL && i < node->children->len; i++) {
        DiskNode* child = g_ptr_array_index(node->children, i);
        gchar* crel = rel[0] != '\0' ? g_strconcat(rel, "/", child->name, NULL) : g_strdup(child->name);
        gboolean history = !c->space->inside && rel[0] == '\0' && strcmp(child->name, ".git") == 0;
        if (history || g_hash_table_contains(c->nested, crel)) {
            /* counted apart */
        } else if (g_hash_table_contains(c->holders, crel)) {
            find_ignored(c, child, crel, out);
        } else if (child->size > 0) {
            GitSpaceEntry e = { g_build_filename(c->root, crel, NULL), child->size, child->n_files, TRUE };
            g_array_append_val(out, e);
        }
        g_free(crel);
    }
    for (guint i = 0; node->files != NULL && i < node->files->len; i++) {
        const DiskFile* f = &g_array_index(node->files, DiskFile, i);
        if (f->size == 0) continue;
        gchar* frel = rel[0] != '\0' ? g_strconcat(rel, "/", f->name, NULL) : g_strdup(f->name);
        if (!g_hash_table_contains(c->listed, frel)) {
            GitSpaceEntry e = { g_build_filename(c->root, frel, NULL), f->size, 1, FALSE };
            g_array_append_val(out, e);
        }
        g_free(frel);
    }
}

GitSpace* git_space_compute(DiskScan* scan, GCancellable* cancel) {
    DiskNode* root_node = disk_scan_root(scan);
    const gchar* root = disk_scan_root_path(scan);
    if (root_node == NULL || root == NULL) return NULL;
    gchar* canon = g_canonicalize_filename(root, NULL);
    gboolean remote = vfs_is_remote(canon); /* git run on the machine, the sizes from the scan's listing */
    gchar* top = remote ? git_remote_top(canon, cancel) : find_git_root(canon);
    if (top == NULL) {
        g_free(canon);
        return NULL;
    }
    GitSpace* sp = g_new0(GitSpace, 1);
    sp->top = top;
    sp->inside = strcmp(canon, top) != 0;
    sp->ignored_top = entries_new();
    sp->tracked_top = entries_new();
    Ctx c = { 0 };
    c.scan = scan;
    c.root = root;
    c.prefix = git_path_prefix(top, canon);
    c.prefix_len = strlen(c.prefix);
    c.cancel = cancel;
    c.space = sp;
    c.dirs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    c.listed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    c.holders = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    c.nested = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    c.inodes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    index_dirs(c.dirs, root_node, "");

    gboolean ok = (remote ? git_remote_list_files_by_kind(top, on_file, &c, cancel)
                          : git_list_files_by_kind(top, FALSE, on_file, &c)) &&
                  !g_cancellable_is_cancelled(cancel);
    if (ok) {
        DiskNode* git = !sp->inside ? g_hash_table_lookup(c.dirs, ".git") : NULL;
        if (git != NULL) sp->history = (DiskTally){ git->size, git->n_files };
        sp->ignored.size = minus(root_node->size,
                                 sp->tracked.size + sp->untracked.size + sp->history.size + sp->nested.size);
        sp->ignored.count = minus(root_node->n_files,
                                  sp->tracked.count + sp->untracked.count + sp->history.count + sp->nested.count);
        GArray* all = entries_new();
        find_ignored(&c, root_node, "", all);
        g_array_sort(all, by_size);
        for (guint i = 0; i < all->len && i < GIT_SPACE_TOP; i++) {
            GitSpaceEntry* e = &g_array_index(all, GitSpaceEntry, i);
            g_array_append_val(sp->ignored_top, *e);
            e->path = NULL; /* moved */
        }
        g_array_unref(all);
    }
    g_hash_table_destroy(c.dirs);
    g_hash_table_destroy(c.listed);
    g_hash_table_destroy(c.holders);
    g_hash_table_destroy(c.nested);
    g_hash_table_destroy(c.inodes);
    g_free(c.prefix);
    g_free(canon);
    if (!ok) {
        git_space_free(sp);
        return NULL;
    }
    return sp;
}
