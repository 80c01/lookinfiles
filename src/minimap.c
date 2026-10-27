#include "minimap.h"

/* Pixels per buffer line: fitted into widget with a ceiling for short files */
gdouble minimap_line_height(GtkWidget* widget, GtkTextBuffer* buffer) {
    gint lines = gtk_text_buffer_get_line_count(buffer);
    if (widget == NULL || lines <= 0) {
        return 0.0;
    }
    gint height = gtk_widget_get_height(widget);
    if (height <= 0) {
        return 0.0;
    }
    return MIN(10.0, (gdouble)height / lines);
}

/* First and last line currently visible in the viewer */
void minimap_viewport(GtkTextView* view, gint* first, gint* last) {
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    GtkTextBuffer* buf = gtk_text_view_get_buffer(view);
    gint lines = gtk_text_buffer_get_line_count(buf);
    if (vadj == NULL || lines <= 0) {
        *first = 0;
        *last = 0;
        return;
    }
    gdouble upper = gtk_adjustment_get_upper(vadj);
    gdouble val = gtk_adjustment_get_value(vadj);
    gdouble page = gtk_adjustment_get_page_size(vadj);
    if (upper <= 0.0) {
        *first = 0;
        *last = lines - 1;
        return;
    }
    *first = CLAMP((gint)((val / upper) * lines), 0, MAX(0, lines - 1));
    *last = CLAMP((gint)(((val + page) / upper) * lines), *first, MAX(0, lines - 1));
}

/* Character count and leading whitespace of one line */
static void minimap_line_metrics(GtkTextBuffer* buffer, gint i, gint* length,
                                 gint* leading) {
    GtkTextIter s, e, p;
    gtk_text_buffer_get_iter_at_line(buffer, &s, i);
    e = s;
    gtk_text_iter_forward_to_line_end(&e);
    *length = gtk_text_iter_get_offset(&e) - gtk_text_iter_get_offset(&s);
    p = s;
    while (!gtk_text_iter_equal(&p, &e)) {
        gunichar c = gtk_text_iter_get_char(&p);
        if (c != ' ' && c != '\t') {
            break;
        }
        gtk_text_iter_forward_char(&p);
    }
    *leading = gtk_text_iter_get_offset(&p) - gtk_text_iter_get_offset(&s);
}

/* Draw the entire file into an offscreen image surface (cached) */
void minimap_build_cache_for_buffer(GtkWidget* widget, GtkTextBuffer* buffer,
                                    cairo_surface_t** cache, gint width, gint height) {
    if (*cache != NULL) {
        cairo_surface_destroy(*cache);
        *cache = NULL;
    }
    if (width <= 0 || height <= 0 || buffer == NULL) {
        return;
    }

    *cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cc = cairo_create(*cache);

    /* Background identical to the preview pane (#121314) */
    cairo_set_source_rgb(cc, 18.0 / 255.0, 19.0 / 255.0, 20.0 / 255.0);
    cairo_paint(cc);

    gint lines = gtk_text_buffer_get_line_count(buffer);
    if (lines <= 0) {
        cairo_destroy(cc);
        return;
    }

    gdouble mm = minimap_line_height(widget, buffer);
    PangoLayout* layout = NULL;
    if (mm >= 4.0) {
        PangoFontDescription* fd =
            pango_font_description_from_string("Monospace");
        pango_font_description_set_absolute_size(
            fd, (gint)(mm * 0.75 * PANGO_SCALE));
        layout = pango_cairo_create_layout(cc);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
    }

    cairo_set_source_rgb(cc, 0.45, 0.45, 0.45);
    for (gint i = 0; i < lines; i++) {
        gdouble py = i * mm;
        if (py >= height) {
            break;
        }

        if (layout != NULL) {
            GtkTextIter s, e;
            gtk_text_buffer_get_iter_at_line(buffer, &s, i);
            e = s;
            gtk_text_iter_forward_to_line_end(&e);
            gchar* line_text = gtk_text_buffer_get_text(buffer, &s, &e, FALSE);
            if (line_text != NULL) {
                pango_layout_set_text(layout, line_text, -1);
                cairo_move_to(cc, 2.0, py);
                pango_cairo_show_layout(cc, layout);
                g_free(line_text);
            }
        } else {
            gint length = 0;
            gint leading = 0;
            minimap_line_metrics(buffer, i, &length, &leading);
            if (length <= 0 || leading >= length) {
                continue;
            }
            cairo_rectangle(cc, 1.0 + leading * mm * 0.5, py,
                            MIN((length - leading) * mm * 0.6,
                                width - 1.0 - leading * mm * 0.5),
                            1.0);
            cairo_fill(cc);
        }
    }

    if (layout != NULL) {
        g_object_unref(layout);
    }
    cairo_destroy(cc);
}


void minimap_invalidate(AppData* app) {
    if (app->mm_cache != NULL) {
        cairo_surface_destroy(app->mm_cache);
        app->mm_cache = NULL;
        app->mm_cache_w = 0;
        app->mm_cache_h = 0;
    }
    if (app->minimap != NULL && gtk_widget_get_visible(app->minimap)) {
        gtk_widget_queue_draw(app->minimap);
    }
}

static gboolean minimap_is_useful(AppData* app) {
    if (app->minimap == NULL || app->viewer_vadj == NULL) {
        return FALSE;
    }
    if (!app->editor_dedicated && app->notebook != NULL &&
        gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook)) != 0) {
        return FALSE;
    }
    GtkAdjustment* adj = app->viewer_vadj;
    return gtk_adjustment_get_upper(adj) >
           gtk_adjustment_get_page_size(adj) + 1.5;
}

static gboolean minimap_vis_idle(gpointer data) {
    AppData* app = (AppData*)data;
    app->mm_vis_source = 0;
    if (app->minimap == NULL) {
        return G_SOURCE_REMOVE;
    }
    gboolean useful = minimap_is_useful(app);
    if (gtk_widget_get_visible(app->minimap) != useful) {
        if (!useful) {
            minimap_invalidate(app);
        }
        gtk_widget_set_visible(app->minimap, useful);
    }
    return G_SOURCE_REMOVE;
}

/* Hide the minimap when the whole file is visible in the viewer */
void minimap_update_visibility(AppData* app) {
    if (app->minimap == NULL || app->viewer_vadj == NULL) {
        return;
    }
    /* Defer visibility change to idle so it never runs mid-size_allocate */
    if (app->mm_vis_source == 0) {
        app->mm_vis_source = g_idle_add(minimap_vis_idle, app);
    }
}

void on_minimap_adj_changed(GtkAdjustment* adjustment, gpointer data) {
    (void)adjustment;
    minimap_update_visibility((AppData*)data);
}

void minimap_draw_content(GtkWidget* widget, cairo_t* cr, int width, int height,
                          GtkTextBuffer* buffer, GtkTextView* text_view,
                          cairo_surface_t** cache, gint* cache_w, gint* cache_h) {
    if (width <= 0 || height <= 0 || buffer == NULL || text_view == NULL) {
        return;
    }
    if (*cache == NULL || *cache_w != width || *cache_h != height) {
        minimap_build_cache_for_buffer(widget, buffer, cache, width, height);
        *cache_w = width;
        *cache_h = height;
    }

    if (*cache != NULL) {
        cairo_set_source_surface(cr, *cache, 0, 0);
        cairo_paint(cr);
    }

    /* Slider: the view's visible range */
    gdouble mm = minimap_line_height(widget, buffer);
    if (mm > 0.0) {
        gint first = 0, last = 0;
        minimap_viewport(text_view, &first, &last);
        cairo_set_source_rgba(cr, 0.47, 0.47, 0.47, 0.35);
        cairo_rectangle(cr, 0, first * mm, width,
                        (last - first + 1) * mm);
        cairo_fill(cr);
    }

    /* Thin border on the viewer/editor side */
    cairo_set_source_rgb(cr, 40.0 / 255.0, 42.0 / 255.0, 44.0 / 255.0);
    cairo_rectangle(cr, 0, 0, 1, height);
    cairo_fill(cr);
}

void on_minimap_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    AppData* app = (AppData*)data;
    if (app == NULL || app->buffer == NULL || app->text_view == NULL) {
        return;
    }
    minimap_draw_content(GTK_WIDGET(area), cr, width, height,
                         app->buffer, GTK_TEXT_VIEW(app->text_view),
                         &app->mm_cache, &app->mm_cache_w, &app->mm_cache_h);
}

void minimap_goto_view(GtkWidget* minimap, GtkTextBuffer* buffer, GtkTextView* text_view, gdouble y) {
    if (minimap == NULL || buffer == NULL || text_view == NULL) {
        return;
    }
    gdouble mm = minimap_line_height(minimap, buffer);
    if (mm <= 0.0) {
        return;
    }
    gint lines = gtk_text_buffer_get_line_count(buffer);
    if (lines <= 0) {
        return;
    }

    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(text_view));
    if (vadj == NULL) {
        return;
    }

    /* Keep horizontal scroll locked to origin so left padding is never scrolled away */
    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(text_view));
    if (hadj != NULL) {
        gdouble min_h = gtk_adjustment_get_lower(hadj);
        if (gtk_adjustment_get_value(hadj) != min_h) {
            gtk_adjustment_set_value(hadj, min_h);
        }
    }

    gint line = CLAMP((gint)(y / mm), 0, lines - 1);
    gdouble min_v = gtk_adjustment_get_lower(vadj);
    gdouble max_v = gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj);
    if (max_v < min_v) {
        max_v = min_v;
    }

    if (line <= 0) {
        gtk_adjustment_set_value(vadj, min_v);
        gtk_widget_queue_draw(minimap);
        return;
    }
    if (line >= lines - 1) {
        gtk_adjustment_set_value(vadj, max_v);
        gtk_widget_queue_draw(minimap);
        return;
    }

    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(buffer, &it, line);
    GdkRectangle iter_loc;
    gtk_text_view_get_iter_location(text_view, &it, &iter_loc);

    GdkRectangle vis;
    gtk_text_view_get_visible_rect(text_view, &vis);

    gdouble target_v = (gdouble)iter_loc.y - (vis.height - iter_loc.height) / 2.0;
    target_v = CLAMP(target_v, min_v, max_v);

    gtk_adjustment_set_value(vadj, target_v);
    gtk_widget_queue_draw(minimap);
}

void minimap_goto(AppData* app, gdouble y) {
    if (app == NULL || app->minimap == NULL || app->buffer == NULL || app->text_view == NULL) {
        return;
    }
    minimap_goto_view(app->minimap, app->buffer, GTK_TEXT_VIEW(app->text_view), y);
}

/* =========================================================================
 * Editor Tab Minimap Implementation
 * ========================================================================= */

void editor_minimap_invalidate(EditorTab* tab) {
    if (tab == NULL) {
        return;
    }
    if (tab->mm_cache != NULL) {
        cairo_surface_destroy(tab->mm_cache);
        tab->mm_cache = NULL;
        tab->mm_cache_w = 0;
        tab->mm_cache_h = 0;
    }
    if (tab->minimap != NULL && gtk_widget_get_visible(tab->minimap)) {
        gtk_widget_queue_draw(tab->minimap);
    }
}

static gboolean editor_minimap_is_useful(EditorTab* tab) {
    if (tab == NULL || tab->minimap == NULL || tab->view == NULL) {
        return FALSE;
    }
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tab->view));
    if (adj == NULL) {
        return FALSE;
    }
    return gtk_adjustment_get_upper(adj) >
           gtk_adjustment_get_page_size(adj) + 1.5;
}

static gboolean editor_minimap_vis_idle(gpointer data) {
    EditorTab* tab = (EditorTab*)data;
    if (tab == NULL) {
        return G_SOURCE_REMOVE;
    }
    tab->mm_vis_source = 0;
    if (tab->minimap == NULL) {
        return G_SOURCE_REMOVE;
    }
    gboolean useful = editor_minimap_is_useful(tab);
    if (gtk_widget_get_visible(tab->minimap) != useful) {
        if (!useful) {
            editor_minimap_invalidate(tab);
        }
        gtk_widget_set_visible(tab->minimap, useful);
    }
    return G_SOURCE_REMOVE;
}

void editor_minimap_update_visibility(EditorTab* tab) {
    if (tab == NULL || tab->minimap == NULL || tab->view == NULL) {
        return;
    }
    if (tab->mm_vis_source == 0) {
        tab->mm_vis_source = g_idle_add(editor_minimap_vis_idle, tab);
    }
}

static void on_editor_minimap_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    EditorTab* tab = (EditorTab*)data;
    if (tab == NULL || tab->buffer == NULL || tab->view == NULL) {
        return;
    }
    minimap_draw_content(GTK_WIDGET(area), cr, width, height,
                         tab->buffer, GTK_TEXT_VIEW(tab->view),
                         &tab->mm_cache, &tab->mm_cache_w, &tab->mm_cache_h);
}

static void on_editor_minimap_pressed(GtkGestureClick* gesture, gint n_press,
                                      gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->buffer != NULL && tab->view != NULL) {
        tab->mm_dragging = TRUE;
        minimap_goto_view(tab->minimap, tab->buffer, GTK_TEXT_VIEW(tab->view), y);
    }
}

static void on_editor_minimap_motion(GtkEventControllerMotion* ctrl,
                                     gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    (void)x;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->mm_dragging && tab->buffer != NULL && tab->view != NULL) {
        minimap_goto_view(tab->minimap, tab->buffer, GTK_TEXT_VIEW(tab->view), y);
    }
}

static void on_editor_minimap_released(GtkGestureClick* gesture, gint n_press,
                                       gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        tab->mm_dragging = FALSE;
    }
}

static void on_editor_minimap_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        editor_minimap_invalidate(tab);
        editor_minimap_update_visibility(tab);
    }
}

static void on_editor_minimap_scroll(GtkAdjustment* adjustment, gpointer data) {
    (void)adjustment;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->minimap != NULL && gtk_widget_get_visible(tab->minimap)) {
        gtk_widget_queue_draw(tab->minimap);
    }
}

static void on_editor_minimap_adj_changed(GtkAdjustment* adjustment, gpointer data) {
    (void)adjustment;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        editor_minimap_update_visibility(tab);
    }
}

static void on_editor_minimap_resize(GtkDrawingArea* area, gint width, gint height, gpointer data) {
    (void)area;
    (void)width;
    (void)height;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        editor_minimap_invalidate(tab);
    }
}

static void on_editor_minimap_realize(GtkWidget* widget, gpointer data) {
    (void)widget;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL) {
        editor_minimap_update_visibility(tab);
    }
}

void editor_minimap_init(EditorTab* tab) {
    if (tab == NULL || tab->view == NULL || tab->buffer == NULL) {
        return;
    }

    GtkWidget* minimap = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(minimap), MINIMAP_WIDTH);
    gtk_widget_set_size_request(minimap, MINIMAP_WIDTH, -1);
    gtk_widget_set_vexpand(minimap, TRUE);
    gtk_widget_set_visible(minimap, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(minimap),
                                   on_editor_minimap_draw, tab, NULL);

    GtkGesture* mm_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mm_click), GDK_BUTTON_PRIMARY);
    g_signal_connect(mm_click, "pressed", G_CALLBACK(on_editor_minimap_pressed), tab);
    g_signal_connect(mm_click, "released", G_CALLBACK(on_editor_minimap_released), tab);
    gtk_widget_add_controller(minimap, GTK_EVENT_CONTROLLER(mm_click));

    GtkEventController* mm_motion = gtk_event_controller_motion_new();
    g_signal_connect(mm_motion, "motion", G_CALLBACK(on_editor_minimap_motion), tab);
    gtk_widget_add_controller(minimap, mm_motion);

    g_signal_connect(minimap, "resize", G_CALLBACK(on_editor_minimap_resize), tab);
    g_signal_connect(minimap, "realize", G_CALLBACK(on_editor_minimap_realize), tab);
    g_signal_connect(tab->buffer, "changed", G_CALLBACK(on_editor_minimap_buffer_changed), tab);

    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tab->view));
    if (vadj != NULL) {
        g_signal_connect(vadj, "value-changed", G_CALLBACK(on_editor_minimap_scroll), tab);
        g_signal_connect(vadj, "changed", G_CALLBACK(on_editor_minimap_adj_changed), tab);
    }

    tab->minimap = minimap;
    tab->mm_cache = NULL;
    tab->mm_cache_w = 0;
    tab->mm_cache_h = 0;
    tab->mm_dragging = FALSE;
    tab->mm_vis_source = 0;
}

void on_minimap_pressed(GtkGestureClick* gesture, gint n_press,
                        gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    AppData* app = (AppData*)data;
    app->mm_dragging = TRUE;
    minimap_goto(app, y);
}

void on_minimap_motion(GtkEventControllerMotion* ctrl,
                       gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    (void)x;
    AppData* app = (AppData*)data;
    if (app->mm_dragging) {
        minimap_goto(app, y);
    }
}

void on_minimap_released(GtkGestureClick* gesture, gint n_press,
                         gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    AppData* app = (AppData*)data;
    app->mm_dragging = FALSE;
}

void on_minimap_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    minimap_invalidate((AppData*)data);
    minimap_update_visibility((AppData*)data);
}

void on_minimap_scroll(GtkAdjustment* adjustment, gpointer data) {
    (void)adjustment;
    AppData* app = (AppData*)data;
    if (app->minimap != NULL && gtk_widget_get_visible(app->minimap)) {
        gtk_widget_queue_draw(app->minimap);
    }
}

void on_minimap_resize(GtkDrawingArea* area, gint width, gint height,
                       gpointer data) {
    (void)area;
    (void)width;
    (void)height;
    minimap_invalidate((AppData*)data);
}

void on_minimap_realize(GtkWidget* widget, gpointer data) {
    (void)widget;
    minimap_update_visibility((AppData*)data);
}
