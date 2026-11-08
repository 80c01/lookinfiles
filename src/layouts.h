#ifndef LAYOUTS_H
#define LAYOUTS_H

/* Saved workspace layouts: save the current workspace's panel arrangement
 * under a name and apply a saved one, from the Edit Layouts dialog inside
 * the window (the Alt+L switcher is layout_switcher.h). Data and file
 * format: layout_store.h. */
#include "core/app_types.h"
#include "layout_store.h"

/* The Edit Layouts dialog: apply, save, delete, reset the default */
void layouts_show_dialog(AppData* app);

/* The current workspace's panels, editor splits and terminals. Caller frees. */
SavedLayout* layouts_capture(AppData* app, const gchar* name);
/* Apply a layout to the current workspace, asking before closing terminals.
 * FALSE if the user cancelled. */
gboolean layouts_apply(AppData* app, const SavedLayout* layout);

/* Previews: layouts shown in full, then the start put back exactly (the
 * same tabs in the same panes and places, the same terminals) or one of
 * them applied. Nothing closes meanwhile: terminals a layout has no room
 * for are set aside, the shells started for it end with the preview. */
typedef struct LayoutsPreview LayoutsPreview;
/* Take the current workspace's arrangement as the start */
LayoutsPreview* layouts_preview_begin(AppData* app);
/* Show layout in full (NULL: the start) */
void layouts_preview_show(LayoutsPreview* preview, const SavedLayout* layout);
/* End the preview: apply layout (asking before closing terminals running at
 * the start), or with NULL put the start back. Frees preview; TRUE if applied. */
gboolean layouts_preview_end(LayoutsPreview* preview, const SavedLayout* layout);

/* A list row: the title above the layout's summary */
GtkWidget* layouts_row_new(const gchar* title, const SavedLayout* layout);
/* Draw a separator above row index of list (the first built-in preset);
 * a negative index removes it */
void layouts_list_separate_at(GtkListBox* list, gint index);
/* Title of a built-in preset's row, e.g. "Code (built-in)". Caller frees. */
gchar* layouts_preset_title(const SavedLayout* preset);
/* Title of the default layout's row. Caller frees. */
gchar* layouts_default_title(const SavedLayout* def);

#endif /* LAYOUTS_H */
