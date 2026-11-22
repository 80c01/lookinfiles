/* Disk usage of a folder, read in a worker thread (see disk_scan.h) */
#include "core/disk_scan.h"
#include "core/disk_remote.h"
#include "core/vfs.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>

/* Longest extension counted as one ("tar.gz" counts as "gz"); longer: none */
#define EXT_MAX 12

/* A file seen through one of its hard links */
typedef struct {
    guint64 dev;
    guint64 ino;
} InodeKey;

struct DiskScan {
    gchar* root_path;
    gboolean one_filesystem;
    DiskNode* root;
    DiskStats stats;
    GString* path;          /* path of the folder being read (worker only) */
    GHashTable* inodes;     /* InodeKey of the files with several links (worker only) */
    GHashTable* excluded;   /* full paths left out of the reading */
    DiskScanFilter filter;  /* NULL: every entry read */
    gpointer filter_data;
    guint64 root_dev;
    GCancellable* cancel;   /* borrowed during disk_scan_run() */
    gboolean cancelled;
    DiskRemoteListing* listing; /* a remote machine's tree, read instead of the disk */

    /* Progress, read by disk_scan_snapshot() from any thread */
    GMutex lock;
    DiskScanSnapshot progress; /* top holds DiskNode copies */
};

static guint inode_hash(gconstpointer p) {
    const InodeKey* k = p;
    return (guint)(k->ino ^ (k->ino >> 32) ^ (k->dev * 31u));
}

static gboolean inode_equal(gconstpointer a, gconstpointer b) {
    const InodeKey* x = a;
    const InodeKey* y = b;
    return x->dev == y->dev && x->ino == y->ino;
}

static DiskNode* node_new(const gchar* name, DiskNode* parent) {
    DiskNode* n = g_new0(DiskNode, 1);
    n->name = g_strdup(name);
    n->parent = parent;
    return n;
}

static void node_free(gpointer data) {
    DiskNode* n = data;
    if (n == NULL) return;
    if (n->children != NULL) g_ptr_array_unref(n->children);
    if (n->files != NULL) {
        for (guint i = 0; i < n->files->len; i++) g_free(g_array_index(n->files, DiskFile, i).name);
        g_array_unref(n->files);
    }
    g_free(n->name);
    g_free(n);
}

/* A copy of a node's own values, without its children and files */
static DiskNode* node_copy_flat(const DiskNode* n) {
    DiskNode* c = g_new(DiskNode, 1);
    *c = *n;
    c->name = g_strdup(n->name);
    c->parent = NULL;
    c->children = NULL;
    c->files = NULL;
    return c;
}

/* A copy of path without trailing separator (a filesystem root keeps its own) */
static gchar* trimmed_path(const gchar* path) {
    gchar* copy = g_strdup(path);
    gsize len = strlen(copy);
    while (len > 1 && G_IS_DIR_SEPARATOR(copy[len - 1]) && g_path_skip_root(copy) != copy + len) {
        copy[--len] = '\0';
    }
    return copy;
}

DiskScan* disk_scan_new(const gchar* root, gboolean one_filesystem) {
    DiskScan* s = g_new0(DiskScan, 1);
    gchar* path = trimmed_path(root != NULL && root[0] != '\0' ? root : ".");
    s->root_path = path;
    s->one_filesystem = one_filesystem;
    s->root = node_new(path, NULL);
    s->path = g_string_new(path);
    s->inodes = g_hash_table_new_full(inode_hash, inode_equal, g_free, NULL);
    s->excluded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->stats.errors = g_array_new(FALSE, FALSE, sizeof(DiskError));
    s->stats.largest = g_array_new(FALSE, FALSE, sizeof(DiskBigFile));
    s->stats.extensions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_mutex_init(&s->lock);
    s->progress.top = g_ptr_array_new_with_free_func(node_free);
    return s;
}

void disk_scan_free(DiskScan* s) {
    if (s == NULL) return;
    node_free(s->root);
    for (guint i = 0; i < s->stats.errors->len; i++) {
        DiskError* e = &g_array_index(s->stats.errors, DiskError, i);
        g_free(e->path);
        g_free(e->message);
    }
    g_array_unref(s->stats.errors);
    for (guint i = 0; i < s->stats.largest->len; i++) g_free(g_array_index(s->stats.largest, DiskBigFile, i).path);
    g_array_unref(s->stats.largest);
    g_hash_table_unref(s->stats.extensions);
    g_free(s->stats.deepest);
    g_string_free(s->path, TRUE);
    g_hash_table_unref(s->inodes);
    g_hash_table_unref(s->excluded);
    g_mutex_clear(&s->lock);
    disk_scan_snapshot_clear(&s->progress);
    g_free(s->root_path);
    g_free(s);
}

DiskAge disk_age_of(gint64 mtime, gint64 now) {
    const gint64 day = 24 * 3600;
    gint64 age = now - mtime;
    if (age < day) return DISK_AGE_DAY;
    if (age < 7 * day) return DISK_AGE_WEEK;
    if (age < 30 * day) return DISK_AGE_MONTH;
    if (age < 365 * day) return DISK_AGE_YEAR;
    if (age < 3 * 365 * day) return DISK_AGE_3_YEARS;
    return DISK_AGE_OLDER;
}

/* The top (excluded) of each group from DISK_SIZE_TINY */
static const guint64 SIZE_TOPS[] = { 1ULL << 10,  10ULL << 10,  100ULL << 10, 1ULL << 20,
                                     10ULL << 20, 100ULL << 20, 1ULL << 30,   10ULL << 30 };
G_STATIC_ASSERT(G_N_ELEMENTS(SIZE_TOPS) == DISK_SIZE_HUGE - DISK_SIZE_TINY);

DiskSize disk_size_of(guint64 size) {
    if (size == 0) return DISK_SIZE_EMPTY;
    for (guint i = 0; i < G_N_ELEMENTS(SIZE_TOPS); i++) {
        if (size < SIZE_TOPS[i]) return (DiskSize)(DISK_SIZE_TINY + i);
    }
    return DISK_SIZE_HUGE;
}

void disk_size_bounds(DiskSize group, gint64* min, gint64* max) {
    gint64 lo = 0, hi = -1;
    if (group <= DISK_SIZE_EMPTY) {
        hi = 0;
    } else if (group == DISK_SIZE_TINY) {
        lo = 1;
        hi = (gint64)SIZE_TOPS[0] - 1;
    } else if (group < DISK_SIZE_HUGE) {
        lo = (gint64)SIZE_TOPS[group - DISK_SIZE_TINY - 1];
        hi = (gint64)SIZE_TOPS[group - DISK_SIZE_TINY] - 1;
    } else {
        lo = (gint64)SIZE_TOPS[G_N_ELEMENTS(SIZE_TOPS) - 1];
    }
    if (min != NULL) *min = lo;
    if (max != NULL) *max = hi;
}

static void add_error(DiskScan* s, const gchar* path, const gchar* message) {
    s->stats.n_errors++;
    if (s->stats.errors->len >= DISK_SCAN_MAX_ERRORS) return;
    DiskError e = { g_strdup(path), g_strdup(message) };
    g_array_append_val(s->stats.errors, e);
}

/* Index where a file of this size goes in an array sorted largest first,
 * keeping max entries at most; -1 when it is too small to be kept */
static gint top_slot(GArray* arr, guint64 size, guint max, gsize size_offset) {
    guint n = arr->len;
    if (n >= max) {
        const guint64* last = (const guint64*)(arr->data + (gsize)(n - 1) * g_array_get_element_size(arr) + size_offset);
        if (size <= *last) return -1;
    }
    guint i = n;
    while (i > 0) {
        const guint64* prev = (const guint64*)(arr->data + (gsize)(i - 1) * g_array_get_element_size(arr) + size_offset);
        if (*prev >= size) break;
        i--;
    }
    return (gint)i;
}

static void keep_dir_file(DiskNode* node, const gchar* name, guint64 size, guint64 disk, gint64 mtime) {
    if (node->files == NULL) node->files = g_array_new(FALSE, FALSE, sizeof(DiskFile));
    gint i = top_slot(node->files, size, DISK_SCAN_DIR_FILES, G_STRUCT_OFFSET(DiskFile, size));
    if (i < 0) return;
    if (node->files->len >= DISK_SCAN_DIR_FILES) {
        g_free(g_array_index(node->files, DiskFile, node->files->len - 1).name);
        g_array_set_size(node->files, node->files->len - 1);
    }
    DiskFile f = { g_strdup(name), size, disk, mtime };
    g_array_insert_val(node->files, (guint)i, f);
}

static void keep_big_file(DiskStats* st, const gchar* path, guint64 size, gint64 mtime) {
    gint i = top_slot(st->largest, size, DISK_SCAN_TOP_FILES, G_STRUCT_OFFSET(DiskBigFile, size));
    if (i < 0) return;
    if (st->largest->len >= DISK_SCAN_TOP_FILES) {
        g_free(g_array_index(st->largest, DiskBigFile, st->largest->len - 1).path);
        g_array_set_size(st->largest, st->largest->len - 1);
    }
    DiskBigFile f = { g_strdup(path), size, mtime };
    g_array_insert_val(st->largest, (guint)i, f);
}

/* Lower-case extension of a file name in buf ("" for none or a long one) */
static void extension_key(const gchar* name, gchar buf[EXT_MAX + 1]) {
    const gchar* dot = strrchr(name, '.');
    buf[0] = '\0';
    if (dot == NULL || dot == name || dot[1] == '\0') return;
    gsize len = strlen(dot + 1);
    if (len > EXT_MAX) return;
    for (gsize i = 0; i <= len; i++) buf[i] = g_ascii_tolower(dot[1 + i]);
}

/* A regular file directly in node (its path in s->path) */
static void add_file(DiskScan* s, DiskNode* node, const gchar* name, guint64 size, guint64 disk, gint64 mtime) {
    node->files_size += size;
    node->files_count++;
    node->n_files++;
    node->size += size;
    node->disk += disk;
    if (mtime > node->mtime) node->mtime = mtime;
    keep_dir_file(node, name, size, disk, mtime);

    DiskStats* st = &s->stats;
    st->n_files++;
    st->size += size;
    st->disk += disk;
    if (size == 0) st->n_empty_files++;
    DiskAge age = disk_age_of(mtime, st->now);
    DiskSize group = disk_size_of(size);
    st->ages[age].size += size;
    st->ages[age].count++;
    st->sizes[group].size += size;
    st->sizes[group].count++;
    node->ages[age].size += size;
    node->ages[age].count++;
    node->sizes[group].size += size;
    node->sizes[group].count++;
    if (st->oldest == 0 || mtime < st->oldest) st->oldest = mtime;
    if (mtime > st->newest) st->newest = mtime;
    gchar ext[EXT_MAX + 1];
    extension_key(name, ext);
    DiskTally* t = g_hash_table_lookup(st->extensions, ext);
    if (t == NULL) {
        t = g_new0(DiskTally, 1);
        g_hash_table_insert(st->extensions, g_strdup(ext), t);
    }
    t->size += size;
    t->count++;
    keep_big_file(st, s->path->str, size, mtime);
}

/* s->path becomes the path of name inside the folder of length base */
/* The groups of from added to into (sign 1) or taken from it (-1) */
static void tally_add(DiskTally* into, const DiskTally* from, guint n, gint sign) {
    for (guint i = 0; i < n; i++) {
        if (sign > 0) {
            into[i].size += from[i].size;
            into[i].count += from[i].count;
        } else {
            into[i].size = into[i].size > from[i].size ? into[i].size - from[i].size : 0;
            into[i].count = into[i].count > from[i].count ? into[i].count - from[i].count : 0;
        }
    }
}

static void path_enter(DiskScan* s, gsize base, const gchar* name) {
    g_string_truncate(s->path, base);
    if (base > 0 && !G_IS_DIR_SEPARATOR(s->path->str[base - 1])) g_string_append_c(s->path, G_DIR_SEPARATOR);
    g_string_append(s->path, name);
}

static gint compare_size(gconstpointer a, gconstpointer b) {
    const DiskNode* x = *(DiskNode* const*)a;
    const DiskNode* y = *(DiskNode* const*)b;
    if (x->size != y->size) return x->size > y->size ? -1 : 1;
    return g_strcmp0(x->name, y->name);
}

/* Is this file already counted through another of its links? (it is
 * recorded when not) */
static gboolean seen_link(DiskScan* s, const GStatBuf* st) {
#ifdef G_OS_UNIX
    if (st->st_nlink <= 1) return FALSE;
    InodeKey key = { (guint64)st->st_dev, (guint64)st->st_ino };
    if (g_hash_table_contains(s->inodes, &key)) return TRUE;
    g_hash_table_add(s->inodes, g_memdup2(&key, sizeof key));
#else
    (void)s;
    (void)st;
#endif
    return FALSE;
}

static guint64 disk_bytes(const GStatBuf* st) {
#ifdef G_OS_UNIX
    return (guint64)st->st_blocks * 512u;
#else
    return (guint64)st->st_size;
#endif
}

static gboolean is_symlink(const GStatBuf* st) {
#ifdef S_ISLNK
    return S_ISLNK(st->st_mode);
#else
    (void)st;
    return FALSE;
#endif
}

/* Read the folder of node (its path in s->path), depth 0 for the root;
 * filtered: its entries go through s->filter */
static void scan_dir(DiskScan* s, DiskNode* node, guint depth, gboolean filtered) {
    if (g_cancellable_is_cancelled(s->cancel)) {
        s->cancelled = TRUE;
        return;
    }
    g_mutex_lock(&s->lock);
    g_free(s->progress.current);
    s->progress.current = g_strdup(s->path->str);
    g_mutex_unlock(&s->lock);

    gsize root_len = strlen(s->root_path);
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    const gchar* name;
    GHashTable* listed = NULL; /* a remote folder's entries: name -> GStatBuf* */
    if (s->listing != NULL) {
        const gchar* rel_dir = s->path->str + root_len;
        while (G_IS_DIR_SEPARATOR(*rel_dir)) rel_dir++;
        listed = disk_remote_listing_dir(s->listing, rel_dir);
        GHashTableIter it;
        gpointer key;
        if (listed != NULL) {
            g_hash_table_iter_init(&it, listed);
            while (g_hash_table_iter_next(&it, &key, NULL)) g_ptr_array_add(names, g_strdup(key));
        }
    } else {
        GError* err = NULL;
        GDir* dir = g_dir_open(s->path->str, 0, &err);
        if (dir == NULL) {
            node->unreadable = TRUE;
            add_error(s, s->path->str, err != NULL ? err->message : "");
            g_clear_error(&err);
            g_ptr_array_unref(names);
            return;
        }
        /* Names first, the folder closed before going down: a deep tree does
         * not keep a descriptor per level */
        while ((name = g_dir_read_name(dir)) != NULL) g_ptr_array_add(names, g_strdup(name));
        g_dir_close(dir);
    }
    if (names->len == 0 && depth > 0) s->stats.n_empty_dirs++;

    gsize base = s->path->len;
    gboolean excluded = g_hash_table_size(s->excluded) > 0;
    GPtrArray* subdirs = g_ptr_array_new(); /* names borrowed from names */
    GArray* subdir_all = g_array_new(FALSE, FALSE, sizeof(gboolean)); /* read unfiltered */
    GArray* subdir_disk = g_array_new(FALSE, FALSE, sizeof(guint64)); /* their own blocks */
    guint64 files = 0, size = 0, disk = 0;
    for (guint i = 0; i < names->len; i++) {
        name = g_ptr_array_index(names, i);
        path_enter(s, base, name);
        if (excluded && g_hash_table_contains(s->excluded, s->path->str)) {
            s->stats.n_excluded++;
            continue;
        }
        GStatBuf st;
        if (listed != NULL) {
            st = *(const GStatBuf*)g_hash_table_lookup(listed, name);
        } else if (g_lstat(s->path->str, &st) != 0) {
            add_error(s, s->path->str, g_strerror(errno));
            continue;
        }
        gboolean all = !filtered;
        if (filtered) {
            gboolean dir = S_ISDIR(st.st_mode) && !is_symlink(&st);
            const gchar* rel = s->path->str + root_len; /* the entry below the root */
            while (G_IS_DIR_SEPARATOR(*rel)) rel++;
            DiskFilterVerdict v = s->filter(rel, dir, s->filter_data);
            if (v == DISK_FILTER_SKIP) continue;
            all = v == DISK_FILTER_ALL;
        }
        if (is_symlink(&st)) {
            s->stats.n_symlinks++;
        } else if (S_ISDIR(st.st_mode)) {
            if (s->one_filesystem && (guint64)st.st_dev != s->root_dev) {
                s->stats.n_mounts++;
            } else {
                g_ptr_array_add(subdirs, (gpointer)name);
                g_array_append_val(subdir_all, all);
                guint64 bytes = disk_bytes(&st);
                g_array_append_val(subdir_disk, bytes);
            }
        } else if (!S_ISREG(st.st_mode)) {
            s->stats.n_special++;
        } else if (seen_link(s, &st)) {
            s->stats.n_hardlinks++;
        } else {
            guint64 bytes = disk_bytes(&st);
            add_file(s, node, name, (guint64)st.st_size, bytes, (gint64)st.st_mtime);
            files++;
            size += (guint64)st.st_size;
            disk += bytes;
        }
    }
    g_string_truncate(s->path, base);
    for (guint i = 0; i < subdir_disk->len; i++) disk += g_array_index(subdir_disk, guint64, i);

    g_mutex_lock(&s->lock);
    s->progress.files += files;
    s->progress.size += size;
    s->progress.disk += disk;
    s->progress.dirs += subdirs->len;
    if (depth == 0) {
        s->progress.top_total = subdirs->len;
        s->progress.root_files_size = node->files_size;
        s->progress.root_files_count = node->files_count;
    }
    g_mutex_unlock(&s->lock);

    for (guint i = 0; i < subdirs->len; i++) {
        name = g_ptr_array_index(subdirs, i);
        DiskNode* child = node_new(name, node);
        /* A folder's own blocks are used on disk too (as du counts them) */
        child->disk = g_array_index(subdir_disk, guint64, i);
        s->stats.disk += child->disk;
        path_enter(s, base, name);
        guint d = depth + 1;
        s->stats.n_dirs++;
        if (d > s->stats.max_depth) {
            s->stats.max_depth = d;
            g_free(s->stats.deepest);
            s->stats.deepest = g_strdup(s->path->str);
        }
        scan_dir(s, child, d, !g_array_index(subdir_all, gboolean, i));
        g_string_truncate(s->path, base);

        node->size += child->size;
        node->disk += child->disk;
        node->n_files += child->n_files;
        node->n_dirs += child->n_dirs + 1;
        tally_add(node->sizes, child->sizes, DISK_SIZE_COUNT, 1);
        tally_add(node->ages, child->ages, DISK_AGE_COUNT, 1);
        if (child->mtime > node->mtime) node->mtime = child->mtime;
        if (node->children == NULL) node->children = g_ptr_array_new_with_free_func(node_free);
        g_ptr_array_add(node->children, child);
        if (depth == 0) {
            g_mutex_lock(&s->lock);
            s->progress.top_done++;
            g_ptr_array_add(s->progress.top, node_copy_flat(child));
            g_mutex_unlock(&s->lock);
        }
        if (s->cancelled) break;
    }
    if (node->children != NULL) g_ptr_array_sort(node->children, compare_size);
    g_ptr_array_unref(subdirs);
    g_array_unref(subdir_all);
    g_array_unref(subdir_disk);
    g_ptr_array_unref(names);
}

/* Is path the mount point of its filesystem? (its parent is on another one) */
static gboolean is_mount_point(const gchar* path, const GStatBuf* st) {
    if (g_path_skip_root(path) != NULL && *g_path_skip_root(path) == '\0') return TRUE;
#ifdef G_OS_UNIX
    gchar* parent = g_path_get_dirname(path);
    GStatBuf pst;
    gboolean mount = g_stat(parent, &pst) == 0 && (pst.st_dev != st->st_dev || pst.st_ino == st->st_ino);
    g_free(parent);
    return mount;
#else
    (void)st;
    return FALSE;
#endif
}

/* Bytes used on the filesystem of path, 0 if unknown */
static guint64 filesystem_used(const gchar* path) {
    GFile* file = g_file_new_for_path(path);
    GFileInfo* info = g_file_query_filesystem_info(file, G_FILE_ATTRIBUTE_FILESYSTEM_USED, NULL, NULL);
    guint64 used = 0;
    if (info != NULL && g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_FILESYSTEM_USED)) {
        used = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_USED);
    }
    if (info != NULL) g_object_unref(info);
    g_object_unref(file);
    return used;
}

void disk_scan_exclude(DiskScan* s, const gchar* path) {
    if (s == NULL || path == NULL || path[0] == '\0') return;
    gchar* clean = trimmed_path(path);
    g_hash_table_add(s->excluded, clean);
}

gboolean disk_scan_is_excluded(const DiskScan* s, const gchar* path) {
    if (s == NULL || path == NULL || g_hash_table_size(s->excluded) == 0) return FALSE;
    gchar* clean = trimmed_path(path);
    gboolean found = g_hash_table_contains(s->excluded, clean);
    g_free(clean);
    return found;
}

void disk_scan_set_filter(DiskScan* s, DiskScanFilter filter, gpointer data) {
    if (s == NULL) return;
    s->filter = filter;
    s->filter_data = data;
}

/* A remote machine's tree, listed there by find (disk_remote.h); NULL when
 * it could not run */
static DiskRemoteListing* list_remote(DiskScan* s) {
    gchar* cmd = disk_remote_command(s->one_filesystem);
    GSubprocess* proc = vfs_spawn(s->root_path, cmd, NULL);
    g_free(cmd);
    if (proc == NULL) return NULL;
    g_output_stream_close(g_subprocess_get_stdin_pipe(proc), NULL, NULL);
    GDataInputStream* in = g_data_input_stream_new(g_subprocess_get_stdout_pipe(proc));
    DiskRemoteListing* l = disk_remote_listing_new();
    guint64 n = 0;
    gchar* rec;
    gsize len = 0;
    while (!g_cancellable_is_cancelled(s->cancel) &&
           (rec = g_data_input_stream_read_upto(in, "", 1, &len, s->cancel, NULL)) != NULL) {
        g_data_input_stream_read_byte(in, NULL, NULL); /* the NUL */
        const gchar* rel = NULL;
        if (disk_remote_listing_add(l, rec, &rel) && (++n & 1023) == 0) {
            /* Where find is: the progress while it lists */
            g_mutex_lock(&s->lock);
            g_free(s->progress.current);
            s->progress.current = g_build_filename(s->root_path, rel, NULL);
            g_mutex_unlock(&s->lock);
        }
        g_free(rec);
    }
    g_object_unref(in);
    if (g_cancellable_is_cancelled(s->cancel)) {
        g_subprocess_force_exit(proc);
        s->cancelled = TRUE;
    }
    g_subprocess_wait(proc, NULL, NULL);
    g_object_unref(proc);
    return l;
}

void disk_scan_run(DiskScan* s, GCancellable* cancel) {
    if (s == NULL) return;
    s->cancel = cancel;
    s->stats.now = g_get_real_time() / G_USEC_PER_SEC;
    GStatBuf st;
    gboolean found;
    if (vfs_is_remote(s->root_path)) { /* the whole tree listed there first */
        s->listing = list_remote(s);
        const GStatBuf* root = disk_remote_listing_root(s->listing);
        found = root != NULL;
        if (found) st = *root;
        errno = ENOENT;
    } else {
        /* The root itself is followed when it is a link: it is what was asked */
        found = g_stat(s->root_path, &st) == 0;
    }
    if (!found || !S_ISDIR(st.st_mode)) {
        s->root->unreadable = TRUE;
        add_error(s, s->root_path, g_strerror(found ? ENOTDIR : errno));
    } else {
        s->root_dev = (guint64)st.st_dev;
        s->root->disk = disk_bytes(&st);
        s->stats.disk = s->root->disk;
        if (s->listing == NULL && is_mount_point(s->root_path, &st)) {
            guint64 used = filesystem_used(s->root_path);
            g_mutex_lock(&s->lock);
            s->progress.fs_used = used;
            g_mutex_unlock(&s->lock);
        }
        scan_dir(s, s->root, 0, s->filter != NULL);
    }
    g_hash_table_remove_all(s->inodes);
    g_clear_pointer(&s->listing, disk_remote_listing_free);
    s->cancel = NULL;
    g_mutex_lock(&s->lock);
    g_clear_pointer(&s->progress.current, g_free);
    s->progress.done = TRUE;
    g_mutex_unlock(&s->lock);
}

void disk_scan_snapshot(DiskScan* s, DiskScanSnapshot* out) {
    memset(out, 0, sizeof *out);
    if (s == NULL) return;
    g_mutex_lock(&s->lock);
    *out = s->progress;
    out->current = g_strdup(s->progress.current);
    out->top = g_ptr_array_new_full(s->progress.top->len, node_free);
    for (guint i = 0; i < s->progress.top->len; i++) {
        g_ptr_array_add(out->top, node_copy_flat(g_ptr_array_index(s->progress.top, i)));
    }
    g_mutex_unlock(&s->lock);
}

void disk_scan_snapshot_clear(DiskScanSnapshot* snap) {
    if (snap == NULL) return;
    g_clear_pointer(&snap->current, g_free);
    g_clear_pointer(&snap->top, g_ptr_array_unref);
}

const gchar* disk_scan_root_path(const DiskScan* s) {
    return s != NULL ? s->root_path : NULL;
}

DiskNode* disk_scan_root(DiskScan* s) {
    return s != NULL ? s->root : NULL;
}

const DiskStats* disk_scan_stats(const DiskScan* s) {
    return s != NULL ? &s->stats : NULL;
}

gboolean disk_scan_cancelled(const DiskScan* s) {
    return s != NULL && s->cancelled;
}

/* The part of path below the root, NULL when it is not below it */
static const gchar* below_root(const DiskScan* s, const gchar* path) {
    gsize len = strlen(s->root_path);
    if (strncmp(path, s->root_path, len) != 0) return NULL;
    const gchar* rel = path + len;
    if (*rel != '\0' && !G_IS_DIR_SEPARATOR(*rel) && !G_IS_DIR_SEPARATOR(s->root_path[len - 1])) return NULL;
    while (G_IS_DIR_SEPARATOR(*rel)) rel++;
    return rel;
}

static DiskNode* child_named(DiskNode* node, const gchar* name, guint* index) {
    for (guint i = 0; node->children != NULL && i < node->children->len; i++) {
        DiskNode* c = g_ptr_array_index(node->children, i);
        if (strcmp(c->name, name) == 0) {
            if (index != NULL) *index = i;
            return c;
        }
    }
    return NULL;
}

/* The folder at the components parts[0..n) below the root, NULL if none */
static DiskNode* walk(DiskScan* s, gchar** parts, guint n) {
    DiskNode* node = s->root;
    for (guint i = 0; node != NULL && i < n; i++) {
        if (parts[i][0] == '\0') continue;
        node = child_named(node, parts[i], NULL);
    }
    return node;
}

DiskNode* disk_scan_find(DiskScan* s, const gchar* path) {
    if (s == NULL || path == NULL) return NULL;
    gchar* clean = trimmed_path(path);
    const gchar* rel = below_root(s, clean);
    DiskNode* node = NULL;
    if (rel != NULL) {
        gchar** parts = g_strsplit(rel, G_DIR_SEPARATOR_S, -1);
        node = walk(s, parts, g_strv_length(parts));
        g_strfreev(parts);
    }
    g_free(clean);
    return node;
}

/* Is path at or below the folder dir? */
static gboolean path_inside(const gchar* path, const gchar* dir) {
    gsize len = strlen(dir);
    return strncmp(path, dir, len) == 0 && (path[len] == '\0' || G_IS_DIR_SEPARATOR(path[len]));
}

/* Drop the largest files and the unreadable entries at or below path */
static void drop_stats_below(DiskStats* st, const gchar* path) {
    for (guint i = st->largest->len; i > 0; i--) {
        DiskBigFile* f = &g_array_index(st->largest, DiskBigFile, i - 1);
        if (!path_inside(f->path, path)) continue;
        g_free(f->path);
        g_array_remove_index(st->largest, i - 1);
    }
    for (guint i = st->errors->len; i > 0; i--) {
        DiskError* e = &g_array_index(st->errors, DiskError, i - 1);
        if (!path_inside(e->path, path)) continue;
        g_free(e->path);
        g_free(e->message);
        g_array_remove_index(st->errors, i - 1);
        if (st->n_errors > 0) st->n_errors--;
    }
}

static guint64 minus(guint64 a, guint64 b) {
    return a > b ? a - b : 0;
}

gboolean disk_scan_remove(DiskScan* s, const gchar* path) {
    if (s == NULL || path == NULL) return FALSE;
    gchar* clean = trimmed_path(path);
    const gchar* rel = below_root(s, clean);
    gchar** parts = rel != NULL && rel[0] != '\0' ? g_strsplit(rel, G_DIR_SEPARATOR_S, -1) : NULL;
    guint n = parts != NULL ? g_strv_length(parts) : 0;
    DiskNode* parent = n > 0 ? walk(s, parts, n - 1) : NULL;
    const gchar* name = n > 0 ? parts[n - 1] : NULL;
    guint64 size = 0, disk = 0, files = 0, dirs = 0;
    DiskTally sizes[DISK_SIZE_COUNT] = { { 0, 0 } };
    DiskTally ages[DISK_AGE_COUNT] = { { 0, 0 } };
    gboolean found = FALSE;
    guint index = 0;
    DiskNode* folder = parent != NULL ? child_named(parent, name, &index) : NULL;
    if (folder != NULL) {
        size = folder->size;
        disk = folder->disk;
        files = folder->n_files;
        dirs = folder->n_dirs + 1;
        memcpy(sizes, folder->sizes, sizeof sizes);
        memcpy(ages, folder->ages, sizeof ages);
        g_ptr_array_remove_index(parent->children, index); /* frees it */
        if (parent->children->len == 0) g_clear_pointer(&parent->children, g_ptr_array_unref);
        found = TRUE;
    } else if (parent != NULL) {
        for (guint i = 0; parent->files != NULL && i < parent->files->len; i++) {
            DiskFile* f = &g_array_index(parent->files, DiskFile, i);
            if (strcmp(f->name, name) != 0) continue;
            size = f->size;
            disk = f->disk;
            files = 1;
            DiskTally one = { f->size, 1 };
            sizes[disk_size_of(f->size)] = one;
            ages[disk_age_of(f->mtime, s->stats.now)] = one;
            parent->files_size = minus(parent->files_size, f->size);
            if (parent->files_count > 0) parent->files_count--;
            g_free(f->name);
            g_array_remove_index(parent->files, i);
            if (parent->files->len == 0) g_clear_pointer(&parent->files, g_array_unref);
            found = TRUE;
            break;
        }
    }
    if (found) {
        for (DiskNode* a = parent; a != NULL; a = a->parent) {
            a->size = minus(a->size, size);
            a->disk = minus(a->disk, disk);
            a->n_files = minus(a->n_files, files);
            a->n_dirs = minus(a->n_dirs, dirs);
            tally_add(a->sizes, sizes, DISK_SIZE_COUNT, -1);
            tally_add(a->ages, ages, DISK_AGE_COUNT, -1);
            if (a->parent != NULL) g_ptr_array_sort(a->parent->children, compare_size);
        }
        DiskStats* st = &s->stats;
        st->size = minus(st->size, size);
        st->disk = minus(st->disk, disk);
        st->n_files = minus(st->n_files, files);
        st->n_dirs = minus(st->n_dirs, dirs);
        tally_add(st->sizes, sizes, DISK_SIZE_COUNT, -1);
        tally_add(st->ages, ages, DISK_AGE_COUNT, -1);
        st->n_excluded++;
        drop_stats_below(st, clean);
    }
    g_strfreev(parts);
    g_free(clean);
    return found;
}

gchar* disk_node_path(const DiskNode* node) {
    if (node == NULL) return NULL;
    GPtrArray* parts = g_ptr_array_new();
    for (const DiskNode* n = node; n != NULL; n = n->parent) g_ptr_array_insert(parts, 0, n->name);
    g_ptr_array_add(parts, NULL);
    gchar* path = g_build_filenamev((gchar**)parts->pdata);
    g_ptr_array_unref(parts);
    return path;
}

guint64 disk_node_other_files_size(const DiskNode* node) {
    if (node == NULL) return 0;
    guint64 listed = 0;
    for (guint i = 0; node->files != NULL && i < node->files->len; i++) {
        listed += g_array_index(node->files, DiskFile, i).size;
    }
    return node->files_size > listed ? node->files_size - listed : 0;
}

guint64 disk_node_other_files_count(const DiskNode* node) {
    if (node == NULL) return 0;
    guint64 listed = node->files != NULL ? node->files->len : 0;
    return node->files_count > listed ? node->files_count - listed : 0;
}

static gint compare_entries(gconstpointer a, gconstpointer b) {
    const DiskEntry* x = a;
    const DiskEntry* y = b;
    return x->size > y->size ? -1 : x->size < y->size ? 1 : 0;
}

GArray* disk_node_entries(DiskNode* node, guint max) {
    GArray* entries = g_array_new(FALSE, FALSE, sizeof(DiskEntry));
    if (node == NULL) return entries;
    for (guint i = 0; node->children != NULL && i < node->children->len; i++) {
        DiskNode* c = g_ptr_array_index(node->children, i);
        DiskEntry e = { c->size, c, NULL, 0 };
        if (e.size > 0) g_array_append_val(entries, e);
    }
    for (guint i = 0; node->files != NULL && i < node->files->len; i++) {
        const DiskFile* f = &g_array_index(node->files, DiskFile, i);
        DiskEntry e = { f->size, NULL, f->name, 0 };
        if (e.size > 0) g_array_append_val(entries, e);
    }
    DiskEntry other = { disk_node_other_files_size(node), NULL, NULL, disk_node_other_files_count(node) };
    if (other.size > 0) g_array_append_val(entries, other);
    g_array_sort(entries, compare_entries);
    if (max > 0 && entries->len > max) {
        DiskEntry rest = { 0, NULL, NULL, 0 };
        for (guint i = max - 1; i < entries->len; i++) {
            const DiskEntry* e = &g_array_index(entries, DiskEntry, i);
            rest.size += e->size;
            rest.count += e->count > 0 ? e->count : 1;
        }
        g_array_set_size(entries, max - 1);
        g_array_append_val(entries, rest);
    }
    return entries;
}
