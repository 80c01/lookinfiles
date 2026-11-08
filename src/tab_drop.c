#include "tab_drop.h"

#if defined(G_OS_WIN32) || defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
    TabSwapFunc swap;
    gpointer data;
    gboolean pages_untargetable; /* while a tab drag is over nb */
    GtkDropTarget* dest;         /* while a tab drag is over nb */
    double x, y;                 /* last pointer position over nb */
    guint poll;                  /* refreshes the highlight when Shift changes */
    gboolean shift;              /* Shift state at the last refresh */
} TabDrop;

/* Where a drop at the pointer would land */
typedef struct {
    PaneZone zone;
    gint index;     /* insertion index on the tab bar, -1 to append */
    gboolean swap;  /* Shift: exchange places with the shown page */
    PaneRect shown; /* highlight, in notebook coordinates */
} DropTarget;

/* Tab drag seen by a notebook of ours, until it ends */
typedef struct {
    GtkNotebook* source; /* ref held until the drag ends */
    TabDragDoneFunc on_done;
    gpointer data;
    GtkNotebook* drop_nb;  /* drop applied once the drag is over (Windows), ref */
    GtkWidget* drop_child; /* ref */
    DropTarget drop;
    guint drop_timeout;
} PendingDrag;

/* Wait for the end of the drag at most this long before applying a drop */
#define DEFERRED_DROP_MS 1000

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
        gtk_widget_set_halign(ind, GTK_ALIGN_FILL);
        GtkWidget* label = gtk_label_new("\u21C4  Swap places");
        gtk_widget_add_css_class(label, "tab-drop-swap-label");
        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
        gtk_widget_set_visible(label, FALSE);
        gtk_box_append(GTK_BOX(ind), label);
        g_object_set_data(G_OBJECT(ind), "swap-label", label);
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

static void indicator_show(GtkWidget* nb, const PaneRect* r, gboolean swap) {
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
    if (swap) {
        gtk_widget_add_css_class(ind, "swap");
    } else {
        gtk_widget_remove_css_class(ind, "swap");
    }
    gtk_widget_set_visible(g_object_get_data(G_OBJECT(ind), "swap-label"), swap);
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

static gboolean shift_held(void) {
#if defined(G_OS_WIN32) || defined(_WIN32)
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
#else
    GdkDisplay* display = gdk_display_get_default();
    GdkSeat* seat = display != NULL ? gdk_display_get_default_seat(display) : NULL;
    GdkDevice* keyboard = seat != NULL ? gdk_seat_get_keyboard(seat) : NULL;
    return keyboard != NULL && (gdk_device_get_modifier_state(keyboard) & GDK_SHIFT_MASK) != 0;
#endif
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

    out->swap = FALSE;
    if (td->swap != NULL && source != nb && shift_held() &&
        td->swap(nb, child, FALSE, td->data)) {
        out->zone = PANE_ZONE_CENTER;
        out->index = -1;
        out->swap = TRUE;
        out->shown = page_area;
        return TRUE;
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
/* Drop target lifecycle and tracking                                        */
/* ------------------------------------------------------------------------- */

static TabDrop* active_hover_td = NULL;

static void poll_stop(TabDrop* td) {
    td->dest = NULL;
    if (td->poll != 0) {
        g_source_remove(td->poll);
        td->poll = 0;
    }
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

static void tab_drop_reset(TabDrop* td) {
    if (td == NULL) {
        return;
    }
    poll_stop(td);
    set_pages_targetable(td, TRUE);
    if (td->nb != NULL && GTK_IS_WIDGET(td->nb)) {
        indicator_hide(GTK_WIDGET(td->nb));
    }
}

static void tab_drop_free(gpointer data) {
    TabDrop* td = (TabDrop*)data;
    if (td == NULL) {
        return;
    }
    if (active_hover_td == td) {
        active_hover_td = NULL;
    }
    tab_drop_reset(td);
    g_free(td);
}

/* Apply a drop accepted on td->nb */
static void apply_drop(TabDrop* td, GtkWidget* child, const DropTarget* t) {
    if (t->swap && td->swap != NULL && td->swap(td->nb, child, TRUE, td->data)) {
        return;
    }
    td->on_drop(td->nb, child, t->zone, t->index, td->data);
}

static void pending_apply_drop(PendingDrag* pd) {
    if (pd->drop_nb == NULL) {
        return;
    }
    GtkNotebook* nb = pd->drop_nb;
    GtkWidget* child = pd->drop_child;
    pd->drop_nb = NULL;
    pd->drop_child = NULL;
    g_clear_handle_id(&pd->drop_timeout, g_source_remove);
    TabDrop* td = g_object_get_data(G_OBJECT(nb), "tab-drop");
    if (td != NULL && gtk_widget_get_root(GTK_WIDGET(nb)) != NULL &&
        gtk_widget_get_ancestor(child, GTK_TYPE_NOTEBOOK) != NULL) {
        apply_drop(td, child, &pd->drop);
    }
    g_object_unref(child);
    g_object_unref(nb);
}

static void pending_finish(PendingDrag* pd) {
    pending_apply_drop(pd);
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
    if (active_hover_td != NULL) {
        tab_drop_reset(active_hover_td);
        active_hover_td = NULL;
    }
    PendingDrag* pd = g_object_get_data(G_OBJECT(drag), PENDING_KEY);
    if (pd != NULL) {
        pending_finish(pd);
    }
}

/* A cancelled drag must not emit dnd-finished: GtkNotebook and GDK drag
 * state machines treat cancel and finish as mutually exclusive. Clean up
 * pending drag tracking without forcing a finish event. */
static void on_drag_cancelled(GdkDrag* drag, GdkDragCancelReason reason) {
    (void)reason;
    if (active_hover_td != NULL) {
        tab_drop_reset(active_hover_td);
        active_hover_td = NULL;
    }
    PendingDrag* pd = g_object_get_data(G_OBJECT(drag), PENDING_KEY);
    if (pd != NULL) {
        pending_finish(pd);
    }
}

#if defined(G_OS_WIN32) || defined(_WIN32)
/* The drag never reported its end: apply the drop anyway */
static gboolean on_deferred_drop_timeout(gpointer data) {
    PendingDrag* pd = data;
    pd->drop_timeout = 0;
    pending_finish(pd);
    return G_SOURCE_REMOVE;
}
#endif

/* On Windows a drop is handled inside the OLE drop call, in a nested main
 * loop, while the GDK drag thread still runs DoDragDrop: moving the page
 * there crashes GTK. Keep the drop and apply it once the drag is over.
 * FALSE when the drag is not followed (the drop is applied at once). */
static gboolean defer_drop(GtkDropTarget* dest, TabDrop* td, GtkWidget* child,
                           const DropTarget* t) {
#if defined(G_OS_WIN32) || defined(_WIN32)
    GdkDrop* drop = gtk_drop_target_get_current_drop(dest);
    GdkDrag* drag = drop != NULL ? gdk_drop_get_drag(drop) : NULL;
    PendingDrag* pd = drag != NULL ? g_object_get_data(G_OBJECT(drag), PENDING_KEY) : NULL;
    if (pd == NULL || pd->drop_nb != NULL) {
        return FALSE;
    }
    pd->drop_nb = g_object_ref(td->nb);
    pd->drop_child = g_object_ref(child);
    pd->drop = *t;
    pd->drop_timeout = g_timeout_add(DEFERRED_DROP_MS, on_deferred_drop_timeout, pd);
    return TRUE;
#else
    (void)dest;
    (void)td;
    (void)child;
    (void)t;
    return FALSE;
#endif
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

static GdkDragAction on_drop_motion(GtkDropTarget* dest, double x, double y, gpointer data) {
    TabDrop* td = data;
    td->x = x;
    td->y = y;
    td->shift = shift_held();
    DropTarget t;
    if (!find_target(td, drag_source(dest), x, y, &t)) {
        indicator_hide(GTK_WIDGET(td->nb));
        return 0;
    }
    indicator_show(GTK_WIDGET(td->nb), &t.shown, t.swap);
    return GDK_ACTION_MOVE;
}

/* GDK sends no motion when only a modifier changes: follow Shift while the
 * tab is over nb so the highlight shows move or swap at once */
static gboolean on_shift_poll(gpointer data) {
    TabDrop* td = data;
    if (td->dest == NULL) {
        td->poll = 0;
        return G_SOURCE_REMOVE;
    }
    if (shift_held() != td->shift) {
        on_drop_motion(td->dest, td->x, td->y, td);
    }
    return G_SOURCE_CONTINUE;
}

static GdkDragAction on_drop_enter(GtkDropTarget* dest, double x, double y, gpointer data) {
    TabDrop* td = data;
    if (active_hover_td != NULL && active_hover_td != td) {
        tab_drop_reset(active_hover_td);
    }
    active_hover_td = td;
    GtkNotebook* source = drag_source(dest);
    if (source != NULL) {
        watch_drag(dest, source, td);
        set_pages_targetable(td, FALSE);
        td->dest = dest;
        if (td->swap != NULL && td->poll == 0) {
            td->poll = g_timeout_add(50, on_shift_poll, td);
        }
    }
    return on_drop_motion(dest, x, y, data);
}

static void on_drop_leave(GtkDropTarget* dest, gpointer data) {
    (void)dest;
    TabDrop* td = data;
    if (active_hover_td == td) {
        active_hover_td = NULL;
    }
    tab_drop_reset(td);
}

static gboolean on_drop(GtkDropTarget* dest, const GValue* value, double x, double y,
                        gpointer data) {
    TabDrop* td = data;
    if (active_hover_td == td) {
        active_hover_td = NULL;
    }
    tab_drop_reset(td);

    if (!G_VALUE_HOLDS(value, GTK_TYPE_NOTEBOOK_PAGE)) {
        return FALSE;
    }
    GtkNotebookPage* page = g_value_get_object(value);
    GtkWidget* child = page != NULL ? gtk_notebook_page_get_child(page) : NULL;
    if (child == NULL) {
        return FALSE;
    }

    GtkNotebook* source = drag_source(dest);
    DropTarget t;
    if (!find_target(td, source, x, y, &t)) {
        if (source == td->nb) {
            /* Dropped back onto its own notebook without moving: accept as a no-op */
            return TRUE;
        }
        return FALSE;
    }

    watch_drag(dest, source, td);
    if (!defer_drop(dest, td, child, &t)) {
        apply_drop(td, child, &t);
    }
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
    g_object_set_data_full(G_OBJECT(nb), "tab-drop", td, tab_drop_free);

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

void tab_drop_set_swap(GtkNotebook* nb, TabSwapFunc swap) {
    TabDrop* td = g_object_get_data(G_OBJECT(nb), "tab-drop");
    g_return_if_fail(td != NULL);
    td->swap = swap;
}
