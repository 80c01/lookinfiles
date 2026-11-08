/* The preview of a Canvas note (see canvas_preview.h) */
#include "canvas/canvas_preview.h"
#include "canvas/canvas_bind.h"
#include "canvas/canvas_doc.h"
#include "canvas/canvas_item.h"
#include "canvas/canvas_view.h"
#include "canvas/canvas_viewport.h"

#define STACK_NAME "canvas"
#define PAGE_MARGIN 400 /* room around the blocks, as on the Canvas tab */

static void on_preview_destroy(GtkWidget* widget, gpointer data) {
    (void)widget;
    canvas_viewport_free(data);
}

/* Nothing on the page reacts: no hover frame, grip, tooltip or focus */
static void make_read_only(GtkWidget* w) {
    for (GtkWidget* c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        if (GTK_IS_TEXT_VIEW(c)) {
            gtk_text_view_set_editable(GTK_TEXT_VIEW(c), FALSE);
            gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(c), FALSE);
        } else if (GTK_IS_EDITABLE(c)) {
            gtk_editable_set_editable(GTK_EDITABLE(c), FALSE);
        }
        make_read_only(c);
    }
}

gboolean canvas_preview_show(AppData* app, const gchar* dir) {
    if (app == NULL || app->preview_stack == NULL || dir == NULL) return FALSE;
    g_autoptr(GError) error = NULL;
    g_autoptr(CanvasDoc) doc = canvas_doc_load(dir, &error);
    if (doc == NULL || doc->blocks->len == 0) return FALSE;

    canvas_view_ensure_css();
    GtkWidget* surface = gtk_fixed_new();
    gtk_widget_add_css_class(surface, "canvas-surface");
    gtk_widget_set_hexpand(surface, TRUE);
    gtk_widget_set_vexpand(surface, TRUE);
    gint w = 0, h = 0;
    for (guint i = 0; i < doc->blocks->len; i++) {
        CanvasItem* it = canvas_item_add(NULL, surface, dir, g_ptr_array_index(doc->blocks, i), NULL, TRUE);
        if (it == NULL) continue;
        CanvasRect b;
        canvas_item_bounds(it, &b);
        w = MAX(w, b.x + b.w);
        h = MAX(h, b.y + b.h);
    }
    canvas_bind_follow(surface);
    make_read_only(surface);
    gtk_widget_set_can_target(surface, FALSE);
    gtk_widget_set_can_focus(surface, FALSE);
    gtk_widget_set_size_request(surface, w + PAGE_MARGIN, h + PAGE_MARGIN);

    CanvasViewport* viewport = canvas_viewport_new(surface, NULL, NULL);
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "canvas-page");
    gtk_box_append(GTK_BOX(page), canvas_viewport_get_widget(viewport));
    g_signal_connect(page, "destroy", G_CALLBACK(on_preview_destroy), viewport);

    GtkStack* stack = GTK_STACK(app->preview_stack);
    /* The new page shown before the previous one goes (the stack never
     * falls back to another page meanwhile), then named in its place */
    GtkWidget* old = gtk_stack_get_child_by_name(stack, STACK_NAME);
    gtk_stack_add_child(stack, page);
    gtk_stack_set_visible_child(stack, page);
    if (old != NULL) gtk_stack_remove(stack, old);
    gtk_stack_page_set_name(gtk_stack_get_page(stack, page), STACK_NAME);
    return TRUE;
}
