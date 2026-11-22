/* A deep search below a folder (see deep_search.h) */
#include "search/deep_search.h"
#include "core/vfs.h"
#include "search/deep_search_remote.h"
#include "search/remote_search.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

/* A folder to read: its path and its depth (1: the entries of the root) */
typedef struct {
    gchar* path;
    guint depth;
} DirTodo;

struct DeepSearch {
    gchar* root;
    DeepQuery query;
    DeepFilter* filter;
    TextMatcher* matcher; /* worker only */
    guint64 root_dev;
    GCancellable* cancel; /* borrowed during deep_search_run() */

    /* Shared with deep_search_take() */
    GMutex lock;
    DeepSearchProgress progress;
    GPtrArray* pending; /* DeepHit* not taken yet */
};

void deep_hit_free(gpointer data) {
    DeepHit* h = data;
    if (h == NULL) return;
    g_free(h->path);
    g_free(h->text);
    g_free(h);
}

void deep_search_progress_clear(DeepSearchProgress* p) {
    if (p == NULL) return;
    g_free(p->current);
    memset(p, 0, sizeof(*p));
}

DeepSearch* deep_search_new(const gchar* root, const DeepQuery* q) {
    if (root == NULL || q == NULL) return NULL;
    DeepFilter* filter = deep_filter_new(q);
    if (filter == NULL) return NULL;
    DeepSearch* s = g_new0(DeepSearch, 1);
    gchar* path = g_strdup(root[0] != '\0' ? root : ".");
    /* No trailing separator, but a filesystem root keeps its own */
    gsize len = strlen(path);
    while (len > 1 && G_IS_DIR_SEPARATOR(path[len - 1]) && g_path_skip_root(path) != path + len) path[--len] = '\0';
    s->root = path;
    deep_query_copy(&s->query, q);
    s->filter = filter;
    g_mutex_init(&s->lock);
    s->pending = g_ptr_array_new_with_free_func(deep_hit_free);
    return s;
}

void deep_search_free(DeepSearch* s) {
    if (s == NULL) return;
    deep_filter_free(s->filter);
    text_matcher_free(s->matcher);
    deep_query_clear(&s->query);
    g_ptr_array_unref(s->pending);
    deep_search_progress_clear(&s->progress);
    g_mutex_clear(&s->lock);
    g_free(s->root);
    g_free(s);
}

const gchar* deep_search_root(const DeepSearch* s) {
    return s != NULL ? s->root : NULL;
}

GPtrArray* deep_search_take(DeepSearch* s, DeepSearchProgress* progress) {
    if (s == NULL) return NULL;
    g_mutex_lock(&s->lock);
    if (progress != NULL) {
        *progress = s->progress;
        progress->current = g_strdup(s->progress.current);
    }
    GPtrArray* hits = NULL;
    if (s->pending->len > 0) {
        hits = s->pending;
        s->pending = g_ptr_array_new_with_free_func(deep_hit_free);
    }
    g_mutex_unlock(&s->lock);
    return hits;
}

static gboolean is_link(const GStatBuf* st) {
#ifdef S_ISLNK
    return S_ISLNK(st->st_mode);
#else
    (void)st;
    return FALSE;
#endif
}

/* The kernel's pseudo filesystems: endless or blocking files */
static gboolean is_pseudo_dir(const gchar* path) {
#ifdef G_OS_UNIX
    return strcmp(path, "/proc") == 0 || strcmp(path, "/sys") == 0 || strcmp(path, "/dev") == 0;
#else
    (void)path;
    return FALSE;
#endif
}

/* The path below the root ("a/b.c"), borrowed from path */
static const gchar* relative(const DeepSearch* s, const gchar* path) {
    gsize n = strlen(s->root);
    const gchar* rel = path + n;
    while (G_IS_DIR_SEPARATOR(*rel)) rel++;
    return rel;
}

/* Counters changed under the lock; FALSE when the search is full */
static gboolean add_hit(DeepSearch* s, DeepHit* hit) {
    g_mutex_lock(&s->lock);
    g_ptr_array_add(s->pending, hit);
    gboolean room = ++s->progress.found < DEEP_SEARCH_MAX_HITS;
    g_mutex_unlock(&s->lock);
    return room;
}

/* The content of a file passing the other criteria: TRUE when it matches
 * (hit filled) */
static gboolean search_content(DeepSearch* s, const gchar* path, DeepHit* hit) {
    TextFileMatch fm;
    gboolean found = text_matcher_search_file(s->matcher, path, DEEP_SEARCH_MAX_COUNT, s->cancel, &fm);
    g_mutex_lock(&s->lock);
    s->progress.bytes += fm.bytes_read;
    if (fm.binary) {
        s->progress.binary++;
    } else if (fm.unreadable) {
        s->progress.unreadable++;
    } else {
        s->progress.searched++;
    }
    g_mutex_unlock(&s->lock);
    if (found) {
        hit->matches = fm.count;
        hit->more = fm.capped;
        hit->line = fm.line;
        hit->column = fm.column;
        hit->length = fm.length;
        hit->text = g_steal_pointer(&fm.text);
    }
    text_file_match_clear(&fm);
    return found;
}

static gint compare_names(gconstpointer a, gconstpointer b) {
    return strcmp(*(const gchar* const*)a, *(const gchar* const*)b);
}

/* Read one folder: test its entries, add its subfolders to todo (in name
 * order once popped); FALSE when the search ends */
static gboolean read_dir(DeepSearch* s, DirTodo* dir, GQueue* todo) {
    g_mutex_lock(&s->lock);
    g_free(s->progress.current);
    s->progress.current = g_strdup(dir->path);
    g_mutex_unlock(&s->lock);

    GDir* d = g_dir_open(dir->path, 0, NULL);
    if (d == NULL) {
        g_mutex_lock(&s->lock);
        s->progress.unreadable++;
        g_mutex_unlock(&s->lock);
        return TRUE;
    }
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL) g_ptr_array_add(names, g_strdup(name));
    g_dir_close(d);
    g_ptr_array_sort(names, compare_names);

    const DeepQuery* q = &s->query;
    gboolean go_on = TRUE;
    GList* subdirs = NULL; /* DirTodo*, last first */
    guint64 tested = 0;
    for (guint i = 0; i < names->len && go_on; i++) {
        if ((i & 255) == 0 && g_cancellable_is_cancelled(s->cancel)) {
            go_on = FALSE;
            break;
        }
        name = g_ptr_array_index(names, i);
        if (name[0] == '.' && !q->hidden) continue;
        gchar* path = g_build_filename(dir->path, name, NULL);
        GStatBuf st;
        if (g_lstat(path, &st) != 0) {
            g_mutex_lock(&s->lock);
            s->progress.unreadable++;
            g_mutex_unlock(&s->lock);
            g_free(path);
            continue;
        }
        gboolean is_dir = S_ISDIR(st.st_mode);
        if (deep_filter_excluded(s->filter, relative(s, path), is_dir)) {
            g_free(path);
            continue;
        }
        tested++;
        if (deep_filter_match_entry(s->filter, name, is_dir, (guint64)st.st_size, (gint64)st.st_mtime)) {
            DeepHit* hit = g_new0(DeepHit, 1);
            hit->is_dir = is_dir;
            hit->is_link = is_link(&st);
            hit->size = (guint64)st.st_size;
            hit->mtime = (gint64)st.st_mtime;
            gboolean keep = TRUE;
            if (s->matcher != NULL) keep = S_ISREG(st.st_mode) && search_content(s, path, hit);
            if (keep && !g_cancellable_is_cancelled(s->cancel)) {
                hit->path = g_strdup(path);
                if (!add_hit(s, hit)) go_on = FALSE;
            } else {
                deep_hit_free(hit);
            }
        }
        gboolean enter = is_dir && (q->max_depth <= 0 || dir->depth < (guint)q->max_depth) && !is_pseudo_dir(path) &&
                         !(q->one_filesystem && (guint64)st.st_dev != s->root_dev);
        if (enter) {
            DirTodo* sub = g_new(DirTodo, 1);
            sub->path = path;
            sub->depth = dir->depth + 1;
            subdirs = g_list_prepend(subdirs, sub);
        } else {
            g_free(path);
        }
    }
    g_ptr_array_unref(names);
    /* Pushed last first: the first name comes out first */
    for (GList* l = subdirs; l != NULL; l = l->next) g_queue_push_head(todo, l->data);
    g_list_free(subdirs);
    g_mutex_lock(&s->lock);
    s->progress.dirs++;
    s->progress.entries += tested;
    g_mutex_unlock(&s->lock);
    return go_on;
}

static void todo_free(gpointer data) {
    DirTodo* t = data;
    g_free(t->path);
    g_free(t);
}

/* --- On a remote machine: find and grep run there --- */

#define REMOTE_BATCH_FILES 256 /* files whose content one grep searches */

/* A file passing the other criteria, its content to search */
typedef struct {
    gchar* rel;
    DeepHit* hit;
} Pending;

static void pending_free(gpointer data) {
    Pending* p = data;
    g_free(p->rel);
    deep_hit_free(p->hit);
    g_free(p);
}

/* Search the content of the files of batch (emptied); FALSE when the search ends */
static gboolean grep_batch(DeepSearch* s, GPtrArray* batch) {
    if (batch->len == 0) return TRUE;
    GString* names = g_string_new(NULL);
    guint64 bytes = 0;
    for (guint i = 0; i < batch->len; i++) {
        const Pending* p = batch->pdata[i];
        g_string_append_len(names, p->rel, (gssize)strlen(p->rel) + 1); /* with its NUL */
        bytes += p->hit->size;
    }
    gchar* cmd = remote_search_grep_input_command(s->query.content, &s->query.content_opts, DEEP_SEARCH_MAX_COUNT);
    GBytes* input = g_string_free_to_bytes(names);
    gint status = 0;
    GBytes* out = NULL;
    gboolean ran = vfs_exec(s->root, cmd, input, s->cancel, &status, &out, NULL);
    g_bytes_unref(input);
    g_free(cmd);
    GHashTable* found = NULL;
    if (ran) {
        gsize len = 0;
        const gchar* data = g_bytes_get_data(out, &len);
        found = deep_remote_parse_matches(data != NULL ? data : "", len, s->query.content, &s->query.content_opts,
                                          DEEP_SEARCH_MAX_COUNT);
        g_bytes_unref(out);
    }
    g_mutex_lock(&s->lock);
    if (ran) {
        s->progress.searched += batch->len;
        s->progress.bytes += bytes;
    } else {
        s->progress.unreadable += batch->len;
    }
    g_mutex_unlock(&s->lock);
    gboolean go_on = TRUE;
    for (guint i = 0; i < batch->len && go_on; i++) {
        Pending* p = batch->pdata[i];
        DeepRemoteMatch* m = found != NULL ? g_hash_table_lookup(found, p->rel) : NULL;
        if (m == NULL || g_cancellable_is_cancelled(s->cancel)) continue;
        DeepHit* hit = g_steal_pointer(&p->hit);
        hit->matches = m->count;
        hit->more = m->more;
        hit->line = m->line;
        hit->column = m->column;
        hit->length = m->length;
        hit->text = g_steal_pointer(&m->text);
        go_on = add_hit(s, hit);
    }
    if (found != NULL) g_hash_table_destroy(found);
    g_ptr_array_set_size(batch, 0);
    return go_on;
}

/* rel, or a folder above it, was left out (excluded: their paths) */
static gboolean below_excluded(GHashTable* excluded, const gchar* rel) {
    if (g_hash_table_size(excluded) == 0) return FALSE;
    gchar* path = g_strdup(rel);
    gboolean below = FALSE;
    for (gchar* slash = strrchr(path, '/'); slash != NULL && !below; slash = strrchr(path, '/')) {
        *slash = '\0';
        below = g_hash_table_contains(excluded, path);
    }
    g_free(path);
    return below;
}

/* One entry listed by find; FALSE when the search ends */
static gboolean remote_entry(DeepSearch* s, const DeepRemoteEntry* e, GHashTable* excluded, GPtrArray* batch) {
    gboolean is_dir = e->type == 'd';
    if (below_excluded(excluded, e->rel)) return TRUE;
    if (deep_filter_excluded(s->filter, e->rel, is_dir)) {
        if (is_dir) g_hash_table_add(excluded, g_strdup(e->rel));
        return TRUE;
    }
    g_mutex_lock(&s->lock);
    s->progress.entries++;
    if (is_dir) {
        s->progress.dirs++;
        g_free(s->progress.current);
        s->progress.current = g_build_filename(s->root, e->rel, NULL);
    }
    g_mutex_unlock(&s->lock);
    const gchar* slash = strrchr(e->rel, '/');
    const gchar* name = slash != NULL ? slash + 1 : e->rel;
    if (!deep_filter_match_entry(s->filter, name, is_dir, e->size, e->mtime)) return TRUE;
    DeepHit* hit = g_new0(DeepHit, 1);
    hit->path = g_build_filename(s->root, e->rel, NULL);
    hit->is_dir = is_dir;
    hit->is_link = e->type == 'l';
    hit->size = e->size;
    hit->mtime = e->mtime;
    if (s->query.content == NULL || s->query.content[0] == '\0' || !deep_filter_has_content(s->filter)) {
        return add_hit(s, hit);
    }
    if (e->type != 'f') {
        deep_hit_free(hit);
        return TRUE;
    }
    Pending* p = g_new0(Pending, 1);
    p->rel = g_strdup(e->rel);
    p->hit = hit;
    g_ptr_array_add(batch, p);
    return batch->len < REMOTE_BATCH_FILES || grep_batch(s, batch);
}

static DeepSearchState run_remote(DeepSearch* s) {
    gchar* cmd = deep_remote_find_command(&s->query);
    GSubprocess* proc = vfs_spawn(s->root, cmd, NULL);
    g_free(cmd);
    if (proc == NULL) return DEEP_SEARCH_NO_FOLDER;
    g_output_stream_close(g_subprocess_get_stdin_pipe(proc), NULL, NULL);
    GDataInputStream* in = g_data_input_stream_new(g_subprocess_get_stdout_pipe(proc));
    GHashTable* excluded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GPtrArray* batch = g_ptr_array_new_with_free_func(pending_free);
    gboolean go_on = TRUE;
    gboolean entered = FALSE; /* the folder could be read */
    while (go_on && !g_cancellable_is_cancelled(s->cancel)) {
        gsize len = 0;
        gchar* rec = g_data_input_stream_read_upto(in, "", 1, &len, s->cancel, NULL); /* up to the NUL */
        if (rec == NULL) break; /* the end of the output */
        g_data_input_stream_read_byte(in, NULL, NULL); /* the NUL */
        DeepRemoteEntry e;
        if (len > 0 && deep_remote_parse_entry(rec, &e)) {
            if (e.type == 'r') {
                entered = TRUE;
            } else {
                go_on = remote_entry(s, &e, excluded, batch);
            }
        }
        g_free(rec);
    }
    if (go_on && !g_cancellable_is_cancelled(s->cancel)) go_on = grep_batch(s, batch);
    g_ptr_array_unref(batch);
    g_hash_table_destroy(excluded);
    g_object_unref(in);
    if (!go_on || g_cancellable_is_cancelled(s->cancel)) g_subprocess_force_exit(proc);
    g_subprocess_wait(proc, NULL, NULL);
    g_object_unref(proc);
    if (g_cancellable_is_cancelled(s->cancel)) return DEEP_SEARCH_STOPPED;
    if (!go_on) return DEEP_SEARCH_FULL;
    return entered ? DEEP_SEARCH_DONE : DEEP_SEARCH_NO_FOLDER;
}

void deep_search_run(DeepSearch* s, GCancellable* cancel) {
    if (s == NULL) return;
    s->cancel = cancel;
    if (vfs_is_remote(s->root)) {
        DeepSearchState end = run_remote(s);
        g_mutex_lock(&s->lock);
        s->progress.state = end;
        g_clear_pointer(&s->progress.current, g_free);
        g_mutex_unlock(&s->lock);
        s->cancel = NULL;
        return;
    }
    DeepSearchState end = DEEP_SEARCH_DONE;
    GStatBuf st;
    if (g_stat(s->root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        end = DEEP_SEARCH_NO_FOLDER;
    } else {
        s->root_dev = (guint64)st.st_dev;
        if (deep_filter_has_content(s->filter)) {
            s->matcher = text_matcher_new(s->query.content, &s->query.content_opts);
        }
        GQueue todo = G_QUEUE_INIT;
        DirTodo* first = g_new(DirTodo, 1);
        first->path = g_strdup(s->root);
        first->depth = 1;
        g_queue_push_head(&todo, first);
        DirTodo* dir;
        while ((dir = g_queue_pop_head(&todo)) != NULL) {
            gboolean go_on = !g_cancellable_is_cancelled(cancel) && read_dir(s, dir, &todo);
            todo_free(dir);
            if (!go_on) {
                end = g_cancellable_is_cancelled(cancel) ? DEEP_SEARCH_STOPPED : DEEP_SEARCH_FULL;
                break;
            }
        }
        g_queue_clear_full(&todo, todo_free);
    }
    g_mutex_lock(&s->lock);
    s->progress.state = end;
    g_clear_pointer(&s->progress.current, g_free);
    g_mutex_unlock(&s->lock);
    s->cancel = NULL;
}
