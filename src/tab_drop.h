#ifndef TAB_DROP_H
#define TAB_DROP_H

#include <gtk/gtk.h>

#include "pane_geometry.h"

/* Dropping notebook tabs dragged with the mouse (GtkNotebook tab DnD between
 * notebooks of one group) onto a notebook: on its tab bar (insert there),
 * on its page (move into it) or near a page edge (split that side). A
 * translucent highlight on the nearest GtkOverlay ancestor shows the target
 * while dragging. On Windows, where GTK's tab drag crashes, the notebooks
 * follow the drag of their tabs themselves (no GDK drag and drop). */

/* A tab dropped on `target`: `zone` PANE_ZONE_CENTER moves it into target at
 * `index` (-1: append), another zone splits target on that side. `child` is
 * the dragged page; it may already be in target. */
typedef void (*TabDropFunc)(GtkNotebook* target, GtkWidget* child, PaneZone zone,
                            gint index, gpointer data);

/* The drag that moved a tab out of `source` has ended: GTK no longer uses
 * source, which may now be removed if it was left empty. */
typedef void (*TabDragDoneFunc)(GtkNotebook* source, gpointer data);

/* Whether target takes child, a tab dragged from source */
typedef gboolean (*TabAcceptFunc)(GtkNotebook* target, GtkNotebook* source, GtkWidget* child,
                                  gpointer data);

/* Shift held while dropping a tab from another notebook on nb: the dragged
 * page and the page shown in nb exchange places. With do_it FALSE, only
 * tells whether that swap is possible. */
typedef gboolean (*TabSwapFunc)(GtkNotebook* target, GtkWidget* child, gboolean do_it,
                                gpointer data);

/* Accept tab drops on nb. Without can_split every drop moves the tab into nb.
 * Leading pages that are not detachable (fixed tabs) stay first. on_done is
 * called for the notebooks tabs are dragged out of. */
void tab_drop_attach(GtkNotebook* nb, gboolean can_split, TabDropFunc on_drop,
                     TabDragDoneFunc on_done, gpointer data);
/* Let accept decide which tabs nb takes (by default: those of notebooks of
 * its group), tabs of other groups included; call after tab_drop_attach() */
void tab_drop_set_accept(GtkNotebook* nb, TabAcceptFunc accept);

/* Enable the Shift swap on nb; call after tab_drop_attach() */
void tab_drop_set_swap(GtkNotebook* nb, TabSwapFunc swap);

#endif /* TAB_DROP_H */
