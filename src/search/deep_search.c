/* A deep search below a folder (see deep_search.h) */
#include "search/deep_search.h"

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

void deep_search_run(DeepSearch* s, GCancellable* cancel) {
    if (s == NULL) return;
    s->cancel = cancel;
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
