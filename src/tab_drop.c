#include "tab_drop.h"

/* Set by GtkNotebook on the GdkDrag of a tab drag (documented in
 * gtk_notebook_set_tab_detachable()) */
#define DRAG_ORIGIN_KEY "gtk-notebook-drag-origin"
#define INDICATOR_KEY "tab-drop-indicator"
#define PENDING_KEY "tab-drop-pending"
#define INSERT_BAR_WIDTH 3.0

typedef struct {
    GtkNotebook* nb;
    gboolean can_split;
    TabDropFunc on_drop;
    TabDragDoneFunc on_done;
    TabAcceptFunc accept;
    gpointer data;
    gboolean pages_untargetable; /* while a tab drag is over nb */
} TabDrop;

/* Where a drop at the pointer would land */
typedef struct {
    PaneZone zone;
    gint index;     /* insertion index on the tab bar, -1 to append */
    PaneRect shown; /* highlight, in notebook coordinates */
} DropTarget;

/* Tab drag seen by a notebook of ours, until it ends */
typedef struct {
    GtkNotebook* source; /* ref held until the drag ends */
    TabDragDoneFunc on_done;
    gpointer data;
} PendingDrag;

/* ------------------------------------------------------------------------- */
/* Highlight                                                                 */
/* ------------------------------------------------------------------------- */

static gboolean on_overlay_child_position(GtkOverlay* overlay, GtkWidget* widget,
                                          GdkRectangle* alloc, gpointer data) {
    (void)data;
    if (widget != g_object_get_data(G_OBJECT(overlay), INDICATOR_KEY)) {
        return FALSE;
    }
    const PaneRect* r = g_object_get_data(G_OBJECT(widget), "rect");
    if (r == NULL) {
        return FALSE;
    }
    alloc->x = (int)r->x;
    alloc->y = (int)r->y;
    alloc->width = MAX(1, (int)r->width);
    alloc->height = MAX(1, (int)r->height);
    return TRUE;
}

static GtkWidget* indicator_for(GtkWidget* nb, GtkWidget** overlay_out) {
    GtkWidget* overlay = gtk_widget_get_ancestor(nb, GTK_TYPE_OVERLAY);
    *overlay_out = overlay;
    if (overlay == NULL) {
        return NULL;
    }
    GtkWidget* ind = g_object_get_data(G_OBJECT(overlay), INDICATOR_KEY);
    if (ind == NULL) {
        ind = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(ind, "tab-drop-indicator");
        gtk_widget_set_can_target(ind, FALSE);
        gtk_widget_set_can_focus(ind, FALSE);
        gtk_widget_set_visible(ind, FALSE);
        g_object_set_data_full(G_OBJECT(ind), "rect", g_new0(PaneRect, 1), g_free);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ind);
        g_object_set_data(G_OBJECT(overlay), INDICATOR_KEY, ind);
        g_signal_connect(overlay, "get-child-position",
                         G_CALLBACK(on_overlay_child_position), NULL);
    }
    return ind;
}

static void indicator_hide(GtkWidget* nb) {
    GtkWidget* overlay = NULL;
    GtkWidget* ind = indicator_for(nb, &overlay);
    if (ind != NULL) {
        gtk_widget_set_visible(ind, FALSE);
    }
}

static void indicator_show(GtkWidget* nb, const PaneRect* r) {
    GtkWidget* overlay = NULL;
    GtkWidget* ind = indicator_for(nb, &overlay);
    if (ind == NULL) {
        return;
    }
    graphene_point_t p;
    if (!gtk_widget_compute_point(nb, overlay, &GRAPHENE_POINT_INIT((float)r->x, (float)r->y), &p)) {
        gtk_widget_set_visible(ind, FALSE);
        return;
    }
    PaneRect* stored = g_object_get_data(G_OBJECT(ind), "rect");
    *stored = (PaneRect){p.x, p.y, r->width, r->height};
    gtk_widget_set_visible(ind, TRUE);
    gtk_widget_queue_allocate(overlay);
}

/* ------------------------------------------------------------------------- */
/* Drop position                                                             */
/* ------------------------------------------------------------------------- */

static gboolean bounds_in(GtkWidget* w, GtkWidget* ref, PaneRect* out) {
    graphene_rect_t b;
    if (w == NULL || !gtk_widget_get_mapped(w) || !gtk_widget_compute_bounds(w, ref, &b)) {
        return FALSE;
    }
    *out = (PaneRect){b.origin.x, b.origin.y, b.size.width, b.size.height};
    return TRUE;
}

/* Number of leading pages that cannot be dragged (they stay first) */
static gint fixed_page_count(GtkNotebook* nb) {
    gint n = gtk_notebook_get_n_pages(nb);
    gint i = 0;
    while (i < n && !gtk_notebook_get_tab_detachable(nb, gtk_notebook_get_nth_page(nb, i))) {
        i++;
    }
    return i;
}

/* Insertion index on the tab bar of nb at x, and the bar marking it */
static gint tab_bar_index(GtkNotebook* nb, GtkWidget* child, double x, PaneRect* bar) {
    GtkWidget* w = GTK_WIDGET(nb);
    gint n = gtk_notebook_get_n_pages(nb);
    gint first = fixed_page_count(nb);
    gint index = n;
    gboolean have_bar = FALSE;
    PaneRect last = {0};
    gboolean have_last = FALSE;

    for (gint i = 0; i < n; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        PaneRect r;
        if (!bounds_in(gtk_notebook_get_tab_label(nb, page), w, &r)) {
            continue;
        }
        if (i >= first && x < r.x + r.width / 2.0) {
            index = i;
            *bar = (PaneRect){r.x - 6.0 - INSERT_BAR_WIDTH / 2.0, r.y, INSERT_BAR_WIDTH, r.height};
            have_bar = TRUE;
            break;
        }
        last = r;
        have_last = TRUE;
    }
    if (!have_bar) {
        if (have_last) {
            *bar = (PaneRect){last.x + last.width + 6.0 - INSERT_BAR_WIDTH / 2.0, last.y,
                              INSERT_BAR_WIDTH, last.height};
        } else {
            *bar = (PaneRect){0, 0, INSERT_BAR_WIDTH, 24.0};
        }
    }

    /* The dragged page leaves its slot first when it is moved within nb */
    gint cur = gtk_notebook_page_num(nb, child);
    if (cur >= 0 && cur < index) {
        index--;
    }
    return index;
}

static GtkNotebook* drag_source(GtkDropTarget* dest) {
    GdkDrop* drop = gtk_drop_target_get_current_drop(dest);
    GdkDrag* drag = drop != NULL ? gdk_drop_get_drag(drop) : NULL;
    GObject* src = drag != NULL ? g_object_get_data(G_OBJECT(drag), DRAG_ORIGIN_KEY) : NULL;
    return GTK_IS_NOTEBOOK(src) ? GTK_NOTEBOOK(src) : NULL;
}

/* Target of a drop at (x, y) on td->nb; FALSE when it would change nothing */
static gboolean find_target(TabDrop* td, GtkNotebook* source, double x, double y,
                            DropTarget* out) {
    GtkNotebook* nb = td->nb;
    if (source == NULL) {
        return FALSE;
    }
    gint cur_page = gtk_notebook_get_current_page(source);
    GtkWidget* child = cur_page >= 0 ? gtk_notebook_get_nth_page(source, cur_page) : NULL;
    if (child == NULL) {
        return FALSE;
    }
    if (td->accept != NULL) {
        if (!td->accept(nb, source, child, td->data)) {
            return FALSE;
        }
    } else {
        const gchar* g1 = gtk_notebook_get_group_name(nb);
        const gchar* g2 = gtk_notebook_get_group_name(source);
        if (g1 == NULL || g2 == NULL || g_strcmp0(g1, g2) != 0) {
            return FALSE;
        }
    }

    PaneRect page_area;
    gint shown = gtk_notebook_get_current_page(nb);
    GtkWidget* shown_child = shown >= 0 ? gtk_notebook_get_nth_page(nb, shown) : NULL;
    if (!bounds_in(shown_child, GTK_WIDGET(nb), &page_area)) {
        page_area = (PaneRect){0, 0, gtk_widget_get_width(GTK_WIDGET(nb)),
                               gtk_widget_get_height(GTK_WIDGET(nb))};
    }

    if (y < page_area.y) {
        /* Tab bar: insert at the pointer */
        out->zone = PANE_ZONE_CENTER;
        out->index = tab_bar_index(nb, child, x, &out->shown);
        if (source == nb && out->index == gtk_notebook_page_num(nb, child)) {
            return FALSE;
        }
        return TRUE;
    }

    out->zone = td->can_split ? pane_drop_zone(&page_area, x, y) : PANE_ZONE_CENTER;
    out->index = -1;
    out->shown = pane_zone_rect(&page_area, out->zone);
    if (source == nb && (out->zone == PANE_ZONE_CENTER || gtk_notebook_get_n_pages(nb) <= 1)) {
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Drop target                                                               */
/* ------------------------------------------------------------------------- */

static void pending_finish(PendingDrag* pd) {
    if (pd->source == NULL) {
        return;
    }
    GtkNotebook* source = pd->source;
    pd->source = NULL;
    if (pd->on_done != NULL) {
        pd->on_done(source, pd->data);
    }
    g_object_unref(source);
}

static void pending_free(gpointer p) {
    pending_finish(p);
    g_free(p);
}

static void on_drag_ended(GdkDrag* drag) {
    PendingDrag* pd = g_object_get_data(G_OBJECT(drag), PENDING_KEY);
    if (pd != NULL) {
        pending_finish(pd);
    }
}

/* GtkNotebook resets a tab drag only on "dnd-finished": after a "cancel"
 * (dropped outside any notebook, or Escape) the tab stays drawn where the
 * drag started. Finish the drag so GTK puts the tab back. Connected after
 * GtkNotebook's own handlers, so this runs after them. */
static void on_drag_cancelled(GdkDrag* drag, GdkDragCancelReason reason) {
    (void)reason;
    if (g_object_get_data(G_OBJECT(drag), PENDING_KEY) != NULL) {
        g_signal_emit_by_name(drag, "dnd-finished"); /* runs on_drag_ended() */
    }
}

/* Follow the tab drag currently over a notebook of ours until it ends, to
 * report it to on_done once GTK no longer uses the source notebook (the drag
 * still signals it after the drop) */
static void watch_drag(GtkDropTarget* dest, GtkNotebook* source, TabDrop* td) {
    GdkDrop* drop = gtk_drop_target_get_current_drop(dest);
    GdkDrag* drag = drop != NULL ? gdk_drop_get_drag(drop) : NULL;
    if (drag == NULL || g_object_get_data(G_OBJECT(drag), PENDING_KEY) != NULL) {
        return;
    }
    /* The source notebook's own handler: it may belong to another group */
    TabDrop* src_td = g_object_get_data(G_OBJECT(source), "tab-drop");
    if (src_td == NULL) {
        src_td = td;
    }
    PendingDrag* pd = g_new0(PendingDrag, 1);
    pd->source = g_object_ref(source);
    pd->on_done = src_td->on_done;
    pd->data = src_td->data;
    g_object_set_data_full(G_OBJECT(drag), PENDING_KEY, pd, pending_free);
    g_signal_connect(drag, "dnd-finished", G_CALLBACK(on_drag_ended), NULL);
    g_signal_connect(drag, "cancel", G_CALLBACK(on_drag_cancelled), NULL);
}

/* While a tab is dragged over nb, its pages ignore the pointer: a text view
 * would otherwise scroll when the drag nears its edges */
static void set_pages_targetable(TabDrop* td, gboolean targetable) {
    if (td->pages_untargetable == !targetable) {
        return;
    }
    td->pages_untargetable = !targetable;
    gint n = gtk_notebook_get_n_pages(td->nb);
    for (gint i = 0; i < n; i++) {
        gtk_widget_set_can_target(gtk_notebook_get_nth_page(td->nb, i), targetable);
    }
}

static GdkDragAction on_drop_motion(GtkDropTarget* dest, double x, double y, gpointer data) {
    TabDrop* td = data;
    DropTarget t;
    if (!find_target(td, drag_source(dest), x, y, &t)) {
        indicator_hide(GTK_WIDGET(td->nb));
        return 0;
    }
    indicator_show(GTK_WIDGET(td->nb), &t.shown);
    return GDK_ACTION_MOVE;
}

static GdkDragAction on_drop_enter(GtkDropTarget* dest, double x, double y, gpointer data) {
    GtkNotebook* source = drag_source(dest);
    if (source != NULL) {
        watch_drag(dest, source, (TabDrop*)data);
        set_pages_targetable((TabDrop*)data, FALSE);
    }
    return on_drop_motion(dest, x, y, data);
}

static void on_drop_leave(GtkDropTarget* dest, gpointer data) {
    (void)dest;
    TabDrop* td = data;
    set_pages_targetable(td, TRUE);
    indicator_hide(GTK_WIDGET(td->nb));
}

static gboolean on_drop(GtkDropTarget* dest, const GValue* value, double x, double y,
                        gpointer data) {
    TabDrop* td = data;
    set_pages_targetable(td, TRUE);
    indicator_hide(GTK_WIDGET(td->nb));

    GtkNotebook* source = drag_source(dest);
    DropTarget t;
    if (!G_VALUE_HOLDS(value, GTK_TYPE_NOTEBOOK_PAGE) || !find_target(td, source, x, y, &t)) {
        return FALSE;
    }
    GtkNotebookPage* page = g_value_get_object(value);
    GtkWidget* child = page != NULL ? gtk_notebook_page_get_child(page) : NULL;
    if (child == NULL) {
        return FALSE;
    }

    watch_drag(dest, source, td);
    td->on_drop(td->nb, child, t.zone, t.index, td->data);
    return TRUE;
}

void tab_drop_attach(GtkNotebook* nb, gboolean can_split, TabDropFunc on_drop_func,
                     TabDragDoneFunc on_done, gpointer data) {
    g_return_if_fail(GTK_IS_NOTEBOOK(nb) && on_drop_func != NULL);

    TabDrop* td = g_new0(TabDrop, 1);
    td->nb = nb;
    td->can_split = can_split;
    td->on_drop = on_drop_func;
    td->on_done = on_done;
    td->data = data;
    g_object_set_data_full(G_OBJECT(nb), "tab-drop", td, g_free);

    GtkDropTarget* dest = gtk_drop_target_new(GTK_TYPE_NOTEBOOK_PAGE, GDK_ACTION_MOVE);
    /* Capture: handle tab bar drops too, before GtkNotebook's own target */
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(dest), GTK_PHASE_CAPTURE);
    g_signal_connect(dest, "enter", G_CALLBACK(on_drop_enter), td);
    g_signal_connect(dest, "motion", G_CALLBACK(on_drop_motion), td);
    g_signal_connect(dest, "leave", G_CALLBACK(on_drop_leave), td);
    g_signal_connect(dest, "drop", G_CALLBACK(on_drop), td);
    gtk_widget_add_controller(GTK_WIDGET(nb), GTK_EVENT_CONTROLLER(dest));
}

void tab_drop_set_accept(GtkNotebook* nb, TabAcceptFunc accept) {
    TabDrop* td = g_object_get_data(G_OBJECT(nb), "tab-drop");
    g_return_if_fail(td != NULL);
    td->accept = accept;
}
