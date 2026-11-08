#ifndef EDITOR_PRIVATE_H
#define EDITOR_PRIVATE_H

/* Internal header of the editor tabs: editor.c, editor_notebooks.c and
 * editor_tab_menu.c (the public API is editor.h) */
#include "editor/editor.h"
#include "widgets/pane_geometry.h"

/* editor_notebooks.c */
/* GTK selects the neighbour page when the current one is removed: when that
 * lands on the Git tab of the preview notebook (no editor tab left beside
 * it), show the preview instead */
void editor_after_current_removed(AppData* app, GtkNotebook* nb, gboolean was_current);
/* Remove an empty editor notebook and the split that held it */
void editor_cleanup_notebook(AppData* app, GtkWidget* nb_widget);
/* Editor tabs can be reordered and dragged to another notebook */
void editor_page_set_draggable(GtkNotebook* nb, GtkWidget* child);
/* Split editor notebook nb: a new empty notebook takes the `side` half of its
 * place (PANE_ZONE_LEFT/RIGHT/TOP/BOTTOM). Returns it, or NULL. */
GtkWidget* editor_split_notebook(AppData* app, GtkNotebook* nb, PaneZone side);
/* The notebook of tab, else editor_target_notebook() */
GtkNotebook* editor_tab_notebook(AppData* app, EditorTab* tab);
/* Where a new editor tab goes: the last focused tab's notebook, else the
 * dedicated panel when shown, else the preview notebook */
GtkNotebook* editor_target_notebook(AppData* app);
/* Show the dedicated panel while it holds tabs, hide it otherwise */
void editor_update_panel_visibilities(AppData* app);
/* The editor notebook next to cur_nb in direction dir, or NULL */
GtkWidget* editor_neighbor_notebook(AppData* app, GtkNotebook* cur_nb, EditorMoveDirection dir);
/* The notebook holding child as a page, or NULL */
GtkNotebook* editor_notebook_of_page(GtkWidget* child);

/* editor_tab_menu.c */
/* Right click: the tab menu; left click: focus the tab; middle click: close it */
void editor_tab_label_connect(EditorTab* tab);

#endif /* EDITOR_PRIVATE_H */
