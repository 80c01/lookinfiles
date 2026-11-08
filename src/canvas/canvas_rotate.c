/* Shapes turned on the Canvas page (see canvas_rotate.h) */
#include "canvas/canvas_rotate.h"
#include "canvas/canvas_geometry.h"
#include "core/i18n.h"
#include "canvas/canvas_page_private.h"
#include "theme.h"

#include <math.h>

#define HANDLE_SIZE 10 /* the round handle */
#define HANDLE_GAP 18  /* from the top of the shape to the handle centre */
#define HANDLE_GRAB 9  /* the pointer this close to the centre grabs it */

struct CanvasRotate {
    CanvasPage* page;
    GtkWidget* handle; /* referenced; on the surface while shown */
    GtkWidget* target; /* weak: the frame of the shape */
    guint tick;
    guint idle_hide;
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".canvas-rotate { border-radius: 50%;"
                  " border: 1px solid @focus_ring; background-color: @bg_view; }"
                  ".canvas-rotate:hover { background-color: @focus_ring; }");
}

/* The block that shows the handle: the current one, selected alone, not
 * locked, with the Select tool */
static CanvasItem* wanted(CanvasRotate* r) {
    CanvasPage* p = r->page;
    if (p->closed || p->dir == NULL || p->tool != CANVAS_TOOL_SELECT) return NULL;
    CanvasItem* it = canvas_page_current_item(p);
    if (!canvas_item_rotatable(it) || it->locked) return NULL;
    GPtrArray* sel = p->selection;
    if (sel->len > 1 || (sel->len == 1 && g_ptr_array_index(sel, 0) != it->frame)) return NULL;
    return it;
}

/* Centre of the handle of it on the page */
static void handle_point(const CanvasItem* it, gdouble* x, gdouble* y) {
    *x = it->x + it->w / 2.0;
    *y = it->y - HANDLE_GAP;
    canvas_rotate_point(it->x + it->w / 2.0, it->y + it->h / 2.0, it->angle, x, y);
}

static void place(CanvasRotate* r, const CanvasItem* it) {
    GtkWidget* surface = r->page->surface;
    if (gtk_widget_get_parent(r->handle) != surface) {
        gtk_fixed_put(GTK_FIXED(surface), r->handle, 0, 0);
    } else if (gtk_widget_get_next_sibling(r->handle) != NULL) {
        gtk_widget_insert_before(r->handle, surface, NULL); /* above the blocks */
    }
    gdouble x, y;
    handle_point(it, &x, &y);
    GskTransform* t = gsk_transform_translate(
        NULL, &GRAPHENE_POINT_INIT((float)(x - HANDLE_SIZE / 2.0), (float)(y - HANDLE_SIZE / 2.0)));
    gtk_fixed_set_child_transform(GTK_FIXED(surface), r->handle, t);
    gsk_transform_unref(t);
}

static gboolean on_idle_hide(gpointer data) {
    CanvasRotate* r = data;
    r->idle_hide = 0;
    canvas_rotate_update(r);
    return G_SOURCE_REMOVE;
}

/* Each frame while shown: the shape may move, turn, grow or be locked */
static gboolean on_tick(GtkWidget* widget, GdkFrameClock* clock, gpointer data) {
    (void)widget;
    (void)clock;
    CanvasRotate* r = data;
    CanvasItem* it = wanted(r);
    if (it == NULL || r->target == NULL || it->frame != r->target) {
        /* Not removed from the surface inside its own tick */
        r->tick = 0;
        if (r->idle_hide == 0) r->idle_hide = g_idle_add(on_idle_hide, r);
        return G_SOURCE_REMOVE;
    }
    place(r, it);
    return G_SOURCE_CONTINUE;
}

void canvas_rotate_hide(CanvasRotate* r) {
    if (r == NULL) return;
    g_clear_weak_pointer(&r->target);
    if (r->tick != 0) gtk_widget_remove_tick_callback(r->handle, r->tick);
    r->tick = 0;
    GtkWidget* parent = gtk_widget_get_parent(r->handle);
    if (parent != NULL) gtk_fixed_remove(GTK_FIXED(parent), r->handle);
}

void canvas_rotate_update(CanvasRotate* r) {
    if (r == NULL) return;
    CanvasItem* it = wanted(r);
    if (it == NULL) {
        canvas_rotate_hide(r);
        return;
    }
    g_set_weak_pointer(&r->target, it->frame);
    place(r, it);
    if (r->tick == 0) r->tick = gtk_widget_add_tick_callback(r->handle, on_tick, r, NULL);
}

CanvasItem* canvas_rotate_handle_at(CanvasRotate* r, gdouble x, gdouble y) {
    if (r == NULL || r->target == NULL || gtk_widget_get_parent(r->handle) == NULL) return NULL;
    CanvasItem* it = canvas_item_of(r->target);
    if (it == NULL || it != wanted(r)) return NULL;
    gdouble hx, hy;
    handle_point(it, &hx, &hy);
    return hypot(x - hx, y - hy) <= HANDLE_GRAB ? it : NULL;
}

void canvas_rotate_drag(CanvasItem* it, gdouble x, gdouble y, gboolean snap) {
    if (!canvas_item_rotatable(it)) return;
    it->angle = canvas_handle_angle(it->x + it->w / 2.0, it->y + it->h / 2.0, x, y, snap);
    canvas_item_place(it);
}

CanvasRotate* canvas_rotate_new(CanvasPage* p) {
    ensure_css();
    CanvasRotate* r = g_new0(CanvasRotate, 1);
    r->page = p;
    r->handle = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_widget_add_css_class(r->handle, "canvas-rotate");
    gtk_widget_set_size_request(r->handle, HANDLE_SIZE, HANDLE_SIZE);
    gtk_widget_set_cursor_from_name(r->handle, "grab");
    gtk_widget_set_tooltip_text(r->handle, _("Drag to Rotate (Shift: 15° Steps)"));
    return r;
}

void canvas_rotate_free(CanvasRotate* r) {
    if (r == NULL) return;
    if (r->idle_hide != 0) g_source_remove(r->idle_hide);
    canvas_rotate_hide(r);
    g_object_unref(r->handle);
    g_free(r);
}
