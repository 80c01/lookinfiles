#ifndef DEEP_SEARCH_STORE_H
#define DEEP_SEARCH_STORE_H

/* Saved Deep Search criteria (search bookmarks): one search per file
 * app_config_dir()/searches/<id>.ini (GKeyFile). A bookmark points to it
 * with the target "lookinfiles-search://<id>"; an id is lowercase hex only,
 * so no target can name a file outside that folder.
 *
 * The criteria are stored as the raw text typed in the Deep Search form so
 * that relative dates ("7d", "today") and human sizes ("10M") stay dynamic
 * and readable when the search is run again. Pure GLib, unit-tested. */
#include "core/file_types.h"
#include "search/deep_query.h"

#define DEEP_SEARCH_TARGET_PREFIX "lookinfiles-search://"

typedef struct {
    gchar* folder;              /* folder to search, or NULL when use_current_folder */
    gboolean use_current_folder; /* TRUE: search the explorer's current folder on open */
    gchar* name;                /* Name field, or NULL */
    gchar* extensions;          /* Extensions field, or NULL (always NULL when family >= 0) */
    gint family;                /* FileFamily (0 .. FILE_FAMILY_OTHER - 1), or -1 for any */
    gchar* content;             /* Contains field, or NULL */
    TextSearchOptions content_opts;
    DeepKind kind;              /* DEEP_KIND_ANY / FILES / FOLDERS */
    gchar* min_size;            /* Size from (raw text, e.g. "10M"), or NULL */
    gchar* max_size;            /* Size to, or NULL */
    gchar* after;               /* Last change from (raw text, e.g. "7d"), or NULL */
    gchar* before;              /* Last change to, or NULL */
    gchar* exclude;             /* Exclude patterns, or NULL */
    gchar* depth;               /* Depth (raw text, e.g. "2"), or NULL */
    gboolean hidden;            /* Hidden entries */
    gboolean one_filesystem;    /* This filesystem only (default TRUE) */
} SavedDeepSearch;

/* Defaults: no criteria, any category, any type, one_filesystem TRUE */
void saved_deep_search_init(SavedDeepSearch* s);
void saved_deep_search_clear(SavedDeepSearch* s);
void saved_deep_search_free(SavedDeepSearch* s);

/* The bookmark target of a saved search (caller frees), NULL for an invalid id */
gchar* deep_search_target_make(const gchar* id);
/* The id inside a target, or NULL when target is not a valid saved search
 * target. Points into target. */
const gchar* deep_search_target_id(const gchar* target);
/* A new unique id (caller frees) */
gchar* deep_search_store_new_id(void);
/* The file of a saved search, NULL for an invalid id (caller frees) */
gchar* deep_search_store_path(const gchar* id);
/* Delete the file of a saved search; missing is fine */
void deep_search_store_remove(const gchar* id);

/* NULL when the file is missing or malformed (free with saved_deep_search_free()) */
SavedDeepSearch* deep_search_store_load(const gchar* path);
gboolean deep_search_store_save(const SavedDeepSearch* s, const gchar* path, GError** error);

/* Default bookmark name suggested from the criteria (caller frees) */
gchar* saved_deep_search_default_name(const SavedDeepSearch* s);

#endif /* DEEP_SEARCH_STORE_H */
