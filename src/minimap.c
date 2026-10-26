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
static void minimap_build_cache(GtkWidget* widget, AppData* app,
                                gint width, gint height) {
    if (app->mm_cache != NULL) {
        cairo_surface_destroy(app->mm_cache);
        app->mm_cache = NULL;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    app->mm_cache =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cc = cairo_create(app->mm_cache);

    /* Background identical to the preview pane (#121314) */
    cairo_set_source_rgb(cc, 18.0 / 255.0, 19.0 / 255.0, 20.0 / 255.0);
    cairo_paint(cc);

    gint lines = gtk_text_buffer_get_line_count(app->buffer);
    if (lines <= 0) {
        cairo_destroy(cc);
        return;
    }

    gdouble mm = minimap_line_height(widget, app->buffer);
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
            gtk_text_buffer_get_iter_at_line(app->buffer, &s, i);
            e = s;
            gtk_text_iter_forward_to_line_end(&e);
            gchar* line_text = gtk_text_buffer_get_text(app->buffer, &s, &e, FALSE);
            if (line_text != NULL) {
                pango_layout_set_text(layout, line_text, -1);
                cairo_move_to(cc, 2.0, py);
                pango_cairo_show_layout(cc, layout);
                g_free(line_text);
            }
        } else {
            gint length = 0;
            gint leading = 0;
            minimap_line_metrics(app->buffer, i, &length, &leading);
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

void on_minimap_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    GtkWidget* widget = GTK_WIDGET(area);
    AppData* app = (AppData*)data;
    gdouble mm;
    gint first, last;

    if (width <= 0 || height <= 0) {
        return;
    }
    if (app->mm_cache == NULL || app->mm_cache_w != width ||
        app->mm_cache_h != height) {
        minimap_build_cache(widget, app, width, height);
        app->mm_cache_w = width;
        app->mm_cache_h = height;
    }

    cairo_set_source_surface(cr, app->mm_cache, 0, 0);
    cairo_paint(cr);

    /* Slider: the viewer's visible range */
    mm = minimap_line_height(widget, app->buffer);
    if (mm > 0.0) {
        minimap_viewport(GTK_TEXT_VIEW(app->text_view), &first, &last);
        cairo_set_source_rgba(cr, 0.47, 0.47, 0.47, 0.35);
        cairo_rectangle(cr, 0, first * mm, width,
                        (last - first + 1) * mm);
        cairo_fill(cr);
    }

    /* Thin border on the viewer side */
    cairo_set_source_rgb(cr, 40.0 / 255.0, 42.0 / 255.0, 44.0 / 255.0);
    cairo_rectangle(cr, 0, 0, 1, height);
    cairo_fill(cr);
}

void minimap_goto(AppData* app, gdouble y) {
    if (app->minimap == NULL || app->buffer == NULL || app->text_view == NULL) {
        return;
    }
    gdouble mm = minimap_line_height(app->minimap, app->buffer);
    if (mm <= 0.0) {
        return;
    }
    gint lines = gtk_text_buffer_get_line_count(app->buffer);
    if (lines <= 0) {
        return;
    }

    GtkAdjustment* vadj = app->viewer_vadj ? app->viewer_vadj
        : gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj == NULL) {
        return;
    }

    /* Keep horizontal scroll locked to origin so left padding is never scrolled away */
    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
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
        gtk_widget_queue_draw(app->minimap);
        return;
    }
    if (line >= lines - 1) {
        gtk_adjustment_set_value(vadj, max_v);
        gtk_widget_queue_draw(app->minimap);
        return;
    }

    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(app->buffer, &it, line);
    GdkRectangle iter_loc;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(app->text_view), &it, &iter_loc);

    GdkRectangle vis;
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(app->text_view), &vis);

    gdouble target_v = (gdouble)iter_loc.y - (vis.height - iter_loc.height) / 2.0;
    target_v = CLAMP(target_v, min_v, max_v);

    gtk_adjustment_set_value(vadj, target_v);
    gtk_widget_queue_draw(app->minimap);
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
