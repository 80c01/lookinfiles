#include "widgets/tab_drop.h"

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
    apply_drop(td, child, &t);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Tab drag without GDK drag and drop (Windows)                              */
/* ------------------------------------------------------------------------- */

/* On Windows, GTK's drag thread crashes in DoDragDrop when a notebook tab is
 * dragged. There each notebook follows the drag of its tabs itself: once
 * the pointer leaves the tab bar, a capture gesture claims the press, which
 * stops GtkNotebook from starting its own drag, and the drop goes through
 * find_target() and apply_drop() as above. LOOKIN_INTERNAL_TAB_DRAG=1 turns
 * it on elsewhere, to test it. */

#define INTERNAL_DRAG_KEY "tab-drop-internal-drag"

typedef struct {
    TabDrop* td;          /* the notebook the tab is dragged from */
    GtkWidget* child;     /* the pressed page, ref */
    double start_x, start_y;
    double last_x, last_y; /* pointer, in td->nb coordinates */
    gboolean dragging;
    GtkNotebook* over;    /* notebook under the pointer, ref */
    double over_x, over_y; /* pointer, in `over` coordinates */
    gboolean shift;       /* Shift state at the last refresh */
    guint poll;           /* refreshes the highlight when Shift changes */
    GdkCursor* saved_cursor; /* cursor of the window before the drag, ref */
} InternalDrag;

static gboolean internal_tab_drag_enabled(void) {
#if defined(G_OS_WIN32) || defined(_WIN32)
    return TRUE;
#else
    static gint enabled = -1;
    if (enabled < 0) {
        enabled = g_strcmp0(g_getenv("LOOKIN_INTERNAL_TAB_DRAG"), "1") == 0;
    }
    return enabled;
#endif
}

/* The detachable page whose tab is at (x, y) of nb, else NULL */
static GtkWidget* tab_page_at(GtkNotebook* nb, double x, double y) {
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < n; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        PaneRect r;
        if (bounds_in(gtk_notebook_get_tab_label(nb, page), GTK_WIDGET(nb), &r) &&
            x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
            return gtk_notebook_get_tab_detachable(nb, page) ? page : NULL;
        }
    }
    return NULL;
}

/* Whether (x, y) of nb is far enough from the tabs to start a drag: sooner
 * than GtkNotebook would start its own */
static gboolean away_from_tab_bar(GtkNotebook* nb, double x, double y) {
    gint threshold = 8;
    g_object_get(gtk_widget_get_settings(GTK_WIDGET(nb)), "gtk-dnd-drag-threshold", &threshold,
                 NULL);
    double margin = threshold * 2.0; /* within GtkNotebook's own margin (4x) */
    gboolean have_bar = FALSE;
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < n; i++) {
        PaneRect r;
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        if (!bounds_in(gtk_notebook_get_tab_label(nb, page), GTK_WIDGET(nb), &r)) {
            continue;
        }
        left = have_bar ? MIN(left, r.x) : r.x;
        right = have_bar ? MAX(right, r.x + r.width) : r.x + r.width;
        top = have_bar ? MIN(top, r.y) : r.y;
        bottom = have_bar ? MAX(bottom, r.y + r.height) : r.y + r.height;
        have_bar = TRUE;
    }
    if (!have_bar) {
        return FALSE;
    }
    return y < top - margin || y > bottom + margin || x < left - margin || x > right + margin;
}

static void internal_set_over(InternalDrag* ig, GtkNotebook* over) {
    if (ig->over == over) {
        return;
    }
    if (ig->over != NULL) {
        indicator_hide(GTK_WIDGET(ig->over));
        g_object_unref(ig->over);
    }
    ig->over = over != NULL ? g_object_ref(over) : NULL;
}

/* The notebook of ours under (x, y) of the source notebook, and the point in
 * its coordinates */
static GtkNotebook* notebook_under(GtkWidget* src, double x, double y, double* ox, double* oy) {
    GtkRoot* root = gtk_widget_get_root(src);
    graphene_point_t p, q;
    if (root == NULL ||
        !gtk_widget_compute_point(src, GTK_WIDGET(root), &GRAPHENE_POINT_INIT((float)x, (float)y), &p)) {
        return NULL;
    }
    GtkWidget* w = gtk_widget_pick(GTK_WIDGET(root), p.x, p.y, GTK_PICK_DEFAULT);
    while (w != NULL && !(GTK_IS_NOTEBOOK(w) && g_object_get_data(G_OBJECT(w), "tab-drop") != NULL)) {
        w = gtk_widget_get_parent(w);
    }
    if (w == NULL || !gtk_widget_compute_point(GTK_WIDGET(root), w, &p, &q)) {
        return NULL;
    }
    *ox = q.x;
    *oy = q.y;
    return GTK_NOTEBOOK(w);
}

/* Follow the pointer at (x, y) of the source notebook: highlight the target */
static void internal_track(InternalDrag* ig, double x, double y) {
    ig->last_x = x;
    ig->last_y = y;
    ig->shift = shift_held();
    double ox = 0.0, oy = 0.0;
    GtkNotebook* over = notebook_under(GTK_WIDGET(ig->td->nb), x, y, &ox, &oy);
    internal_set_over(ig, over);
    ig->over_x = ox;
    ig->over_y = oy;
    if (over == NULL) {
        return;
    }
    DropTarget t;
    TabDrop* target = g_object_get_data(G_OBJECT(over), "tab-drop");
    if (find_target(target, ig->td->nb, ox, oy, &t)) {
        indicator_show(GTK_WIDGET(over), &t.shown, t.swap);
    } else {
        indicator_hide(GTK_WIDGET(over));
    }
}

static gboolean on_internal_shift_poll(gpointer data) {
    InternalDrag* ig = data;
    if (!ig->dragging) {
        ig->poll = 0;
        return G_SOURCE_REMOVE;
    }
    if (shift_held() != ig->shift) {
        internal_track(ig, ig->last_x, ig->last_y);
    }
    return G_SOURCE_CONTINUE;
}

/* End the drag (dropped or not) */
static void internal_stop(InternalDrag* ig) {
    if (ig->dragging) {
        GtkRoot* root = gtk_widget_get_root(GTK_WIDGET(ig->td->nb));
        if (root != NULL) {
            gtk_widget_set_cursor(GTK_WIDGET(root), ig->saved_cursor);
        }
    }
    g_clear_object(&ig->saved_cursor);
    ig->dragging = FALSE;
    g_clear_handle_id(&ig->poll, g_source_remove);
    internal_set_over(ig, NULL);
    g_clear_object(&ig->child);
}

static void internal_drag_free(gpointer data) {
    InternalDrag* ig = data;
    internal_stop(ig);
    g_free(ig);
}

static void on_internal_drag_begin(GtkGestureDrag* gesture, double x, double y, gpointer data) {
    InternalDrag* ig = data;
    internal_stop(ig);
    GtkWidget* page = tab_page_at(ig->td->nb, x, y);
    if (page == NULL) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    ig->child = g_object_ref(page);
    ig->start_x = x;
    ig->start_y = y;
}

static void on_internal_drag_update(GtkGestureDrag* gesture, double dx, double dy, gpointer data) {
    InternalDrag* ig = data;
    GtkNotebook* nb = ig->td->nb;
    double x = ig->start_x + dx;
    double y = ig->start_y + dy;
    if (ig->child == NULL) {
        return;
    }
    if (!ig->dragging) {
        gint num = gtk_notebook_page_num(nb, ig->child);
        if (!away_from_tab_bar(nb, x, y)) {
            return;
        }
        if (num < 0) {
            gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
            return;
        }
        /* Claiming cancels GtkNotebook's press: it starts no drag of its own */
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        ig->dragging = TRUE;
        gtk_notebook_set_current_page(nb, num); /* find_target() drags the shown page */
        GtkRoot* root = gtk_widget_get_root(GTK_WIDGET(nb));
        if (root != NULL) {
            GdkCursor* cursor = gtk_widget_get_cursor(GTK_WIDGET(root));
            ig->saved_cursor = cursor != NULL ? g_object_ref(cursor) : NULL;
            gtk_widget_set_cursor_from_name(GTK_WIDGET(root), "grabbing");
        }
        ig->poll = g_timeout_add(50, on_internal_shift_poll, ig);
    }
    internal_track(ig, x, y);
}

static void on_internal_drag_end(GtkGestureDrag* gesture, double dx, double dy, gpointer data) {
    (void)gesture;
    InternalDrag* ig = data;
    if (!ig->dragging || ig->child == NULL) {
        internal_stop(ig);
        return;
    }
    internal_track(ig, ig->start_x + dx, ig->start_y + dy);
    GtkNotebook* source = g_object_ref(ig->td->nb);
    TabDragDoneFunc on_done = ig->td->on_done;
    gpointer done_data = ig->td->data;
    GtkWidget* child = g_object_ref(ig->child);
    GtkNotebook* over = ig->over != NULL ? g_object_ref(ig->over) : NULL;
    double ox = ig->over_x, oy = ig->over_y;
    internal_stop(ig);

    TabDrop* target = over != NULL ? g_object_get_data(G_OBJECT(over), "tab-drop") : NULL;
    DropTarget t;
    if (target != NULL && find_target(target, source, ox, oy, &t)) {
        apply_drop(target, child, &t);
    }
    if (on_done != NULL) {
        on_done(source, done_data);
    }
    if (over != NULL) {
        g_object_unref(over);
    }
    g_object_unref(child);
    g_object_unref(source);
}

static void on_internal_drag_cancel(GtkGesture* gesture, GdkEventSequence* sequence,
                                    gpointer data) {
    (void)gesture;
    (void)sequence;
    internal_stop(data);
}

static void internal_drag_attach(TabDrop* td) {
    GtkGesture* gesture = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_PRIMARY);
    /* Capture: sees the press and the motion before GtkNotebook */
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
    InternalDrag* ig = g_new0(InternalDrag, 1);
    ig->td = td;
    g_object_set_data_full(G_OBJECT(gesture), INTERNAL_DRAG_KEY, ig, internal_drag_free);
    g_signal_connect(gesture, "drag-begin", G_CALLBACK(on_internal_drag_begin), ig);
    g_signal_connect(gesture, "drag-update", G_CALLBACK(on_internal_drag_update), ig);
    g_signal_connect(gesture, "drag-end", G_CALLBACK(on_internal_drag_end), ig);
    g_signal_connect(gesture, "cancel", G_CALLBACK(on_internal_drag_cancel), ig);
    gtk_widget_add_controller(GTK_WIDGET(td->nb), GTK_EVENT_CONTROLLER(gesture));
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

    if (internal_tab_drag_enabled()) {
        internal_drag_attach(td);
    }
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
