#ifndef DEEP_SEARCH_LIST_H
#define DEEP_SEARCH_LIST_H

/* The results of the Deep Search tab (deep_search_tab.h): a sorted
 * GtkColumnView of the entries found (name, folder below the one searched,
 * size, last change; with a content, the matches and the first matching
 * line), filled while the search runs. Enter or a double click shows the
 * entry in the explorer, Ctrl+Enter opens a file in an editor tab at its
 * first match; the right-click menu does both and copies the path. */
#include "core/app_types.h"
#include "search/deep_search.h"

typedef struct {
    void (*reveal)(gpointer data, const gchar* path);
    void (*open)(gpointer data, const gchar* path, gint line, gint column);
    gpointer data;
} DeepSearchListHandlers;

/* The list in its scrolled window */
GtkWidget* deep_search_list_new(AppData* app, const DeepSearchListHandlers* handlers);
/* Its GtkColumnView (for the focus) */
GtkWidget* deep_search_list_view(GtkWidget* list);

/* No row; the next ones are found below root, with the content columns
 * shown when content */
void deep_search_list_reset(GtkWidget* list, const gchar* root, gboolean content);
/* Append hits (DeepHit*, taken from the array, which is freed) */
void deep_search_list_add(GtkWidget* list, GPtrArray* hits);
guint deep_search_list_count(GtkWidget* list);

#endif /* DEEP_SEARCH_LIST_H */
