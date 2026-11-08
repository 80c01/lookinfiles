#ifndef ICON_PICKER_H
#define ICON_PICKER_H

/* Icon picker for dialogs: a grid of toggle buttons, one per theme glyph
 * (icons.h), exactly one active. Used by the notebook and bookmark folder
 * dialogs. */
#include <gtk/gtk.h>

typedef void (*IconPickerChanged)(const gchar* icon, gpointer data);

/* icons: NULL-terminated static glyph names; selected: the active one (none
 * when absent from icons); on_changed (may be NULL) runs when another icon
 * is chosen */
GtkWidget* icon_picker_new(const gchar* const* icons, const gchar* selected,
                           IconPickerChanged on_changed, gpointer data);

/* Chosen icon (a string of icons), or NULL when none is active */
const gchar* icon_picker_get_icon(GtkWidget* picker);

#endif /* ICON_PICKER_H */
