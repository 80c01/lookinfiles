#ifndef PATH_SUGGEST_H
#define PATH_SUGGEST_H

/* Path suggestions under a text field that expects a local path: a popover
 * listing, as you type, the entries of the folder typed ("~/pro" ->
 * ~/projects), read in the background, then the extra suggestions of the
 * caller (path_suggest_set_extra()). Ctrl+Space shows them for the text as
 * it is. In the field: Down / Up choose one, Tab completes with it (or the
 * first; a folder then lists its own entries), Enter takes the one chosen
 * (a folder: lists its entries; none chosen: the field's own Enter),
 * Escape hides them; a click takes one.
 *
 * PATH_SUGGEST_FOLDERS lists the subfolders only (field texts without a
 * trailing '/'). PATH_SUGGEST_PROGRAMS lists the subfolders (field texts
 * ending with '/') and the programs (executable files), the Pythons first:
 * for a field that expects a program. */
#include <gtk/gtk.h>

typedef enum {
    PATH_SUGGEST_FOLDERS,
    PATH_SUGGEST_PROGRAMS,
} PathSuggestKind;

/* Add the extra suggestions for text to out (newly allocated field texts,
 * at most PATH_SUGGEST_MAX in all; the duplicates are dropped). is_path:
 * text is a path, its folder's entries already in out. */
typedef void (*PathSuggestExtra)(const gchar* text, gboolean is_path, GPtrArray* out, gpointer data);

#define PATH_SUGGEST_MAX 50

void path_suggest_attach(GtkWidget* entry, PathSuggestKind kind);
void path_suggest_set_extra(GtkWidget* entry, PathSuggestExtra extra, gpointer data);

/* TRUE while the suggestions of entry are on screen (its key handlers then
 * leave Down, Up, Enter and Escape to them) */
gboolean path_suggest_is_open(GtkWidget* entry);

#endif /* PATH_SUGGEST_H */
