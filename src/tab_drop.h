#ifndef TAB_DROP_H
#define TAB_DROP_H

#include <gtk/gtk.h>

#include "pane_geometry.h"

/* Dropping notebook tabs dragged with the mouse (GtkNotebook tab DnD between
 * notebooks of one group) onto a notebook: on its tab bar (insert there),
 * on its page (move into it) or near a page edge (split that side). A
 * translucent highlight on the nearest GtkOverlay ancestor shows the target
 * while dragging. */

/* A tab dropped on `target`: `zone` PANE_ZONE_CENTER moves it into target at
 * `index` (-1: append), another zone splits target on that side. `child` is
 * the dragged page; it may already be in target. */
typedef void (*TabDropFunc)(GtkNotebook* target, GtkWidget* child, PaneZone zone,
                            gint index, gpointer data);

/* The drag that moved a tab out of `source` has ended: GTK no longer uses
 * source, which may now be removed if it was left empty. */
typedef void (*TabDragDoneFunc)(GtkNotebook* source, gpointer data);

/* Accept tab drops on nb. Without can_split every drop moves the tab into nb.
 * Leading pages that are not detachable (fixed tabs) stay first. */
void tab_drop_attach(GtkNotebook* nb, gboolean can_split, TabDropFunc on_drop,
                     TabDragDoneFunc on_done, gpointer data);

#endif /* TAB_DROP_H */
