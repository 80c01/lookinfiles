#ifndef DEEP_SEARCH_LIST_H
#define DEEP_SEARCH_LIST_H

/* The results of the Deep Search tab (deep_search_tab.h): a sorted
 * GtkColumnView of the entries found (name, folder below the one searched,
 * size, last change; with a content, the matches and the first matching
 * line), filled while the search runs. Enter or a double click shows the
 * entry in the explorer, Ctrl+Enter opens a file in an editor tab at its
 * first match; the right-click menu does both and copies the path. A
 * range (deep_search_list_set_range(), the bars of deep_search_charts)
 * shows only the files in it. */
#include "core/app_types.h"
#include "search/deep_search.h"

typedef struct {
    void (*reveal)(gpointer data, const gchar* path);
    void (*open)(gpointer data, const gchar* path, gint line, gint column);
    gpointer data;
} DeepSearchListHandlers;

/* The files shown: a size from min_size to max_size and a day of last
 * change (local, YYYYMMDD) from first_day to last_day, both ends included;
 * -1 (sizes) or 0 (days): no bound */
typedef struct {
    gint64 min_size;
    gint64 max_size;
    gint first_day;
    gint last_day;
} DeepSearchRange;

/* The list in its scrolled window */
GtkWidget* deep_search_list_new(AppData* app, const DeepSearchListHandlers* handlers);
/* Its GtkColumnView (for the focus) */
GtkWidget* deep_search_list_view(GtkWidget* list);

/* No row, no range; the next ones are found below root, with the content columns
 * shown when content */
void deep_search_list_reset(GtkWidget* list, const gchar* root, gboolean content);
/* Append hits (DeepHit*, taken from the array, which is freed) */
void deep_search_list_add(GtkWidget* list, GPtrArray* hits);
guint deep_search_list_count(GtkWidget* list);

/* Show only the files in range (copied); NULL: every result */
void deep_search_list_set_range(GtkWidget* list, const DeepSearchRange* range);
/* Rows shown (the range applied) */
guint deep_search_list_shown(GtkWidget* list);

#endif /* DEEP_SEARCH_LIST_H */
