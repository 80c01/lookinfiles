/* The Canvas page of the preview notebook (see canvas_view.h): its widgets,
 * the tool bar and panel actions, and the public API. The core is in
 * canvas_page.c, the keys in canvas_keys.c, the pointer in canvas_drag.c. */
#include "canvas_view.h"
#include "canvas_bind.h"
#include "app_events.h"
#include "canvas_doc.h"
#include "canvas_drag.h"
#include "canvas_export.h"
#include "canvas_format_bar.h"
#include "canvas_geometry.h"
#include "canvas_item.h"
#include "canvas_keys.h"
#include "canvas_page_private.h"
#include "canvas_props.h"
#include "canvas_table.h"
#include "canvas_table_bar.h"
#include "canvas_text.h"
#include "canvas_toolbar.h"
#include "canvas_viewport.h"
#include "icons.h"
#include "theme.h"

#include <string.h>

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".canvas-page { background-color: @bg_view; }"
                  ".canvas-page .canvas-statusbar { padding: 0 4px 0 12px; color: @fg_muted;"
                  " background-color: @bg_bar; border-top: 1px solid @border; font-size: @size_bar; }"
                  ".canvas-page .canvas-statusbar button { padding: 0 8px; min-height: 0; }"
                  ".canvas-surface { background-color: @bg_view; }"
                  ".canvas-block { border: 1px solid transparent; border-radius: 4px; }"
                  ".canvas-block:hover { border-color: @border; }"
                  ".canvas-block:focus-within { border-color: @border_strong; }"
                  ".canvas-block:focus { border-color: @focus_ring; box-shadow: 0 0 0 1px @focus_ring; }"
                  ".canvas-block.canvas-selected { border-color: @focus_ring; }"
                  ".canvas-band { border: 1px solid @focus_ring; background-color: alpha(@focus_ring, 0.08); }"
                  ".canvas-grip { min-height: 8px; border-radius: 3px 3px 0 0; }"
                  ".canvas-block:hover .canvas-grip, .canvas-block:focus-within .canvas-grip,"
                  " .canvas-block:focus .canvas-grip { background-color: @bg_hover; }"
                  ".canvas-corner { min-width: 10px; min-height: 10px; border-radius: 2px; }"
                  ".canvas-block:hover .canvas-corner, .canvas-block:focus-within .canvas-corner,"
                  " .canvas-block:focus .canvas-corner { background-color: @border_strong; }"
                  ".canvas-block.canvas-locked .canvas-grip, .canvas-block.canvas-locked .canvas-corner"
                  " { background-color: transparent; }"
                  ".canvas-block.canvas-locked:focus { border-style: dashed; }"
                  ".canvas-text, .canvas-text text { background-color: transparent; color: @fg_text;"
                  " font-family: @font_ui; }"
                  ".canvas-text { padding: 2px 6px 6px 6px; }"
                  ".canvas-shape-label, .canvas-shape-label text { background-color: transparent;"
                  " color: @fg_text; font-family: @font_ui; }");
}

static void on_window_focus(GObject* window, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    CanvasPage* p = g_object_get_data(G_OBJECT(data), CANVAS_PAGE_KEY);
    if (p == NULL || p->closed) return;
    GtkWidget* f = gtk_root_get_focus(GTK_ROOT(window));
    canvas_page_focus_changed(p, f);
    if (f == NULL || !gtk_widget_is_ancestor(f, p->root)) canvas_viewport_set_pan_key(p->viewport, FALSE);
    if (f == p->surface) {
        g_clear_weak_pointer(&p->last_focus);
    } else if (f != NULL && gtk_widget_is_ancestor(f, p->surface)) {
        g_set_weak_pointer(&p->last_focus, f);
    }
    canvas_page_update_props(p);
}

static void on_root_map(GtkWidget* widget, gpointer data) {
    CanvasPage* p = data;
    GtkRoot* window = gtk_widget_get_root(widget);
    if (p->focus_watched || window == NULL) return;
    p->focus_watched = TRUE;
    g_signal_connect_object(window, "notify::focus-widget", G_CALLBACK(on_window_focus), p->root, 0);
}

/* ------------------------------------------------------------------------- */
/* Tool Bar, Panel and Format Bar Callbacks (a text change saves by itself)  */
/* ------------------------------------------------------------------------- */

static CanvasItem* current_text_item(CanvasPage* p) {
    CanvasItem* it = canvas_page_current_item(p);
    return (it != NULL && it->kind == CANVAS_BLOCK_TEXT) ? it : NULL;
}

static void on_toolbar_tool(CanvasTool tool, gpointer udata) {
    canvas_page_set_tool(udata, tool);
}

static void on_toolbar_crop(gpointer udata) {
    CanvasPage* p = udata;
    canvas_page_open_crop(p, canvas_page_current_item(p));
}

static void on_format_style(guint style_flag, gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_toggle_style(it->buffer, &it->pending, style_flag);
    canvas_page_restore_focus(udata);
    canvas_page_update_format(udata);
}

static void on_format_preset(CanvasPreset preset, gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_apply_preset(it->buffer, preset);
    canvas_page_restore_focus(udata);
}

static void on_format_list(const gchar* prefix, gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_insert_list(it->buffer, prefix);
    canvas_page_restore_focus(udata);
}

static void on_format_indent(gint step, gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_indent(it->buffer, step);
    canvas_page_restore_focus(udata);
}

static void on_format_checkbox(gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_toggle_checkbox(it->buffer);
    canvas_page_restore_focus(udata);
}

/* A row or column added or deleted from the table bar: undone with Ctrl+Z */
static void on_table_action(CanvasTableAction action, gpointer udata) {
    CanvasPage* p = udata;
    CanvasItem* it = canvas_page_current_item(p);
    if (it == NULL || it->kind != CANVAS_BLOCK_TABLE) return;
    if (!canvas_table_widget_can(it->content, action)) return;
    canvas_page_remember(p);
    /* What the frame adds around the cells (border, resize corner) */
    gint extra = MAX(gtk_widget_get_height(it->frame) - gtk_widget_get_height(it->content), 0);
    canvas_table_widget_apply(it->content, action);
    if (action == CANVAS_TABLE_ROW_ABOVE || action == CANVAS_TABLE_ROW_BELOW || action == CANVAS_TABLE_ROW_DELETE) {
        /* The frame height is a minimum: fit it to the rows left, so a
         * deleted row leaves no gap at the bottom */
        gint min_h = 0;
        gtk_widget_measure(it->content, GTK_ORIENTATION_VERTICAL, -1, &min_h, NULL, NULL, NULL);
        it->h = MAX(min_h + extra, 40);
        canvas_item_place(it);
        canvas_page_update_extent(p);
    }
    canvas_page_update_props(p);
}

/* A style picked in the panel: for the next shapes, and the selected ones */
static void on_props_shape_style(const CanvasShapeStyle* style, gpointer udata) {
    CanvasPage* p = udata;
    p->style = *style;
    if (canvas_tool_draws(p->tool)) return;
    GPtrArray* items = canvas_page_selected(p);
    gboolean changed = FALSE;
    for (guint i = 0; i < items->len; i++) {
        CanvasItem* it = g_ptr_array_index(items, i);
        if (it->kind != CANVAS_BLOCK_SHAPE) continue;
        if (!changed) canvas_page_remember(p);
        changed = TRUE;
        canvas_page_apply_style(it, style);
    }
    g_ptr_array_unref(items);
    if (changed) canvas_page_schedule_save(p);
    canvas_page_restore_focus(p);
}

static void on_toolbar_toggle_grid(gboolean enabled, gpointer udata) {
    ((CanvasPage*)udata)->snap_grid = enabled;
}

/* Align or distribute the selected blocks */
static void on_toolbar_align(CanvasAlignOp op, gpointer udata) {
    CanvasPage* p = udata;
    GPtrArray* items = canvas_page_selected_unlocked(p);
    if (items->len >= 2) {
        GArray* rects = g_array_new(FALSE, FALSE, sizeof(CanvasRect));
        for (guint i = 0; i < items->len; i++) {
            CanvasItem* it = g_ptr_array_index(items, i);
            CanvasRect r = { it->x, it->y, it->w, MAX(it->h, gtk_widget_get_height(it->frame)) };
            g_array_append_val(rects, r);
        }
        canvas_page_remember(p);
        canvas_align_boxes((CanvasRect*)rects->data, rects->len, op);
        for (guint i = 0; i < items->len; i++) {
            CanvasItem* it = g_ptr_array_index(items, i);
            const CanvasRect* r = &g_array_index(rects, CanvasRect, i);
            it->x = r->x;
            it->y = r->y;
            canvas_item_place(it);
        }
        canvas_bind_release(items);
        g_array_unref(rects);
        canvas_page_update_extent(p);
        canvas_page_schedule_save(p);
    }
    g_ptr_array_unref(items);
    canvas_page_restore_focus(p);
}

/* An export waiting for the page to be drawn without the selection; the
 * page root kept alive meanwhile, dropped if another note was opened */
typedef struct {
    GtkWidget* root;
    guint generation;
    gboolean svg;
} ExportJob;

static gboolean on_export_idle(gpointer data) {
    ExportJob* job = data;
    CanvasPage* p = g_object_get_data(G_OBJECT(job->root), CANVAS_PAGE_KEY);
    if (p != NULL && !p->closed && p->dir != NULL && p->generation == job->generation) {
        gchar* path = canvas_doc_export_path(p->dir, job->svg ? "svg" : "png");
        g_autoptr(GError) error = NULL;
        gchar* name = g_path_get_basename(path);
        gchar* text;
        if (canvas_export_picture(p->surface, path, job->svg, &error)) {
            text = g_strdup_printf("Canvas exported to %s", name);
        } else {
            text = g_strdup_printf("Canvas export failed: %s", error->message);
        }
        app_notice(p->app, text, FALSE);
        g_free(text);
        g_free(name);
        g_free(path);
    }
    g_object_unref(job->root);
    g_free(job);
    return G_SOURCE_REMOVE;
}

/* Export next to the note, once the page is drawn again unselected (the
 * idle comes after the redraw) */
static void on_toolbar_export(gboolean svg, gpointer udata) {
    CanvasPage* p = udata;
    if (p->dir == NULL) return;
    canvas_page_deselect(p);
    canvas_page_update_props(p);
    ExportJob* job = g_new0(ExportJob, 1);
    job->root = g_object_ref(p->root);
    job->generation = p->generation;
    job->svg = svg;
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, on_export_idle, job, NULL);
}

static void on_toolbar_lock(gpointer udata) {
    CanvasPage* p = udata;
    canvas_page_toggle_lock(p);
    canvas_page_restore_focus(p);
}

static void on_toolbar_order(gint where, gpointer udata) {
    CanvasPage* p = udata;
    canvas_page_reorder_selected(p, where);
    canvas_page_restore_focus(p);
}

/* ------------------------------------------------------------------------- */
/* Page Setup and API                                                        */
/* ------------------------------------------------------------------------- */

static void close_page(CanvasPage* p) {
    canvas_page_save_now(p);
    canvas_page_prune_media(p);
    GtkNotebook* nb = GTK_NOTEBOOK(p->app->notebook);
    gint page = gtk_notebook_page_num(nb, p->root);
    if (page >= 0) gtk_notebook_remove_page(nb, page);
}

static void on_zoom_changed(gdouble scale, gpointer data) {
    CanvasPage* p = data;
    gchar* text = g_strdup_printf("%d%%", (gint)(scale * 100 + 0.5));
    gtk_button_set_label(GTK_BUTTON(p->zoom_button), text);
    g_free(text);
}

static void on_zoom_clicked(GtkButton* button, gpointer data) {
    (void)button;
    CanvasPage* p = data;
    canvas_viewport_zoom_step(p->viewport, 0);
    gtk_widget_grab_focus(p->surface);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_page(data);
}

static void on_root_destroy(GtkWidget* widget, gpointer data) {
    (void)widget;
    CanvasPage* p = data;
    p->closed = TRUE;
    if (p->save_source != 0) {
        g_source_remove(p->save_source);
        p->save_source = 0;
    }
    if (p->app->canvas_page == p->root) p->app->canvas_page = NULL;
}

static void page_free(gpointer data) {
    canvas_page_free(data);
}

static CanvasPage* page_of(AppData* app) {
    return app->canvas_page != NULL ? g_object_get_data(G_OBJECT(app->canvas_page), CANVAS_PAGE_KEY) : NULL;
}

static CanvasPage* ensure_page(AppData* app) {
    CanvasPage* p = page_of(app);
    if (p != NULL || app->notebook == NULL) return p;
    ensure_css();
    p = canvas_page_new(app);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "canvas-page");
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    p->root = root;

    /* Floating tool bar (top) and properties panel (left) over the page */
    CanvasToolbarCallbacks cbs = {
        .on_tool = on_toolbar_tool,
        .on_crop_image = on_toolbar_crop,
        .on_toggle_grid = on_toolbar_toggle_grid,
        .on_align = on_toolbar_align,
        .on_order = on_toolbar_order,
        .on_lock = on_toolbar_lock,
        .on_export = on_toolbar_export
    };
    p->toolbar = canvas_toolbar_new(&cbs, p);
    CanvasPropsCallbacks pcbs = {.on_shape_style = on_props_shape_style};
    p->props = canvas_props_new(&pcbs, p);
    CanvasFormatBarCallbacks fcbs = {
        .on_style = on_format_style,
        .on_preset = on_format_preset,
        .on_list = on_format_list,
        .on_indent = on_format_indent,
        .on_checkbox = on_format_checkbox
    };
    p->format_bar = canvas_format_bar_new(&fcbs, p);
    p->table_bar = canvas_table_bar_new(on_table_action, p);
    canvas_props_default_style(&p->style);

    p->surface = gtk_fixed_new();
    gtk_widget_add_css_class(p->surface, "canvas-surface");
    gtk_widget_set_focusable(p->surface, TRUE);
    gtk_widget_set_hexpand(p->surface, TRUE);
    gtk_widget_set_vexpand(p->surface, TRUE);
    p->viewport = canvas_viewport_new(p->surface, on_zoom_changed, p);
    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), canvas_viewport_get_widget(p->viewport));
    GtkWidget* bar_widget = canvas_toolbar_get_widget(p->toolbar);
    gtk_widget_set_margin_top(bar_widget, 10);
    gtk_widget_set_margin_start(bar_widget, 12);
    gtk_widget_set_margin_end(bar_widget, 12);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), bar_widget);
    GtkWidget* props_widget = canvas_props_get_widget(p->props);
    gtk_widget_set_margin_top(props_widget, 64);
    gtk_widget_set_margin_start(props_widget, 12);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), props_widget);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), canvas_format_bar_get_widget(p->format_bar));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), canvas_table_bar_get_widget(p->table_bar));
    gtk_box_append(GTK_BOX(root), overlay);

    /* Status bar (bottom): canvas name and zoom */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "canvas-statusbar");
    p->title = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(p->title), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(p->title), 0.0f);
    gtk_widget_set_hexpand(p->title, TRUE);
    gtk_box_append(GTK_BOX(bar), p->title);
    p->zoom_button = gtk_button_new_with_label("100%");
    gtk_widget_add_css_class(p->zoom_button, "flat");
    gtk_widget_set_tooltip_text(p->zoom_button, "Zoom: Ctrl+Wheel, Ctrl+= / Ctrl+-; Click: 100% (Ctrl+0)");
    g_signal_connect(p->zoom_button, "clicked", G_CALLBACK(on_zoom_clicked), p);
    gtk_box_append(GTK_BOX(bar), p->zoom_button);
    gtk_box_append(GTK_BOX(root), bar);

    canvas_drag_attach(p);
    canvas_keys_attach(p, root);

    g_object_set_data_full(G_OBJECT(root), CANVAS_PAGE_KEY, p, page_free);
    g_signal_connect(root, "destroy", G_CALLBACK(on_root_destroy), p);
    g_signal_connect(root, "map", G_CALLBACK(on_root_map), p);
    app->canvas_page = root;

    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint after = app->git_tab_box != NULL ? gtk_notebook_page_num(nb, app->git_tab_box) : 0;
    if (app->refs_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->refs_page));
    if (app->search_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->search_page));
    GtkWidget* label = icon_label_new("document-edit", "Canvas");
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), p);
    gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(mclick));
    gtk_notebook_insert_page(nb, root, label, after + 1);
    return p;
}

void canvas_view_open(AppData* app, const gchar* dir, const gchar* title) {
    if (app == NULL || dir == NULL) return;
    CanvasPage* p = ensure_page(app);
    if (p == NULL) return;
    if (g_strcmp0(p->dir, dir) != 0) {
        canvas_page_load(p, dir, title);
    } else if (title != NULL) {
        gtk_label_set_text(GTK_LABEL(p->title), title);
    }
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        gtk_widget_set_visible(app->viewer_box, TRUE);
    }
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, p->root));
    GtkWidget* first = gtk_widget_get_first_child(p->surface);
    if (first != NULL && canvas_item_of(first) != NULL) {
        canvas_item_select(canvas_item_of(first));
    } else {
        gtk_widget_grab_focus(p->surface);
    }
}

void canvas_view_save(AppData* app) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p != NULL) canvas_page_save_now(p);
}

gboolean canvas_view_close_path(AppData* app, const gchar* path) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p == NULL || p->dir == NULL || path == NULL) return FALSE;
    gchar* under = g_strconcat(path, "/", NULL);
    gboolean inside = g_strcmp0(p->dir, path) == 0 || g_str_has_prefix(p->dir, under);
    g_free(under);
    if (!inside) return FALSE;
    close_page(p);
    return TRUE;
}

void canvas_view_path_moved(AppData* app, const gchar* old_path, const gchar* new_path, const gchar* title) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p == NULL || p->dir == NULL || old_path == NULL || new_path == NULL) return;
    gsize n = strlen(old_path);
    if (strncmp(p->dir, old_path, n) != 0 || (p->dir[n] != '\0' && p->dir[n] != '/')) return;
    gchar* dir = g_strconcat(new_path, p->dir + n, NULL);
    g_free(p->dir);
    p->dir = dir;
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        canvas_item_set_dir(canvas_item_of(c), p->dir);
    }
    if (title != NULL && p->dir[strlen(new_path)] == '\0') gtk_label_set_text(GTK_LABEL(p->title), title);
}

gboolean canvas_view_close_shown(AppData* app) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p == NULL || app->notebook == NULL || !gtk_widget_get_mapped(p->root)) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, p->root)) return FALSE;
    close_page(p);
    return TRUE;
}

gboolean canvas_view_claims_key(AppData* app, guint keyval, GdkModifierType state) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p == NULL || p->dir == NULL || canvas_keys_zoom_step(keyval, state) == G_MININT) return FALSE;
    GtkWidget* f = canvas_page_focused_widget(p);
    return f != NULL && (f == p->root || gtk_widget_is_ancestor(f, p->root));
}
