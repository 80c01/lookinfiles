#include "minimap.h"

/* Pixels per buffer line: fitted into widget with a ceiling for short files */
gdouble minimap_line_height(GtkWidget* widget, GtkTextBuffer* buffer) {
    GtkAllocation a;
    gint lines = gtk_text_buffer_get_line_count(buffer);
    if (widget == NULL || lines <= 0) {
        return 0.0;
    }
    gtk_widget_get_allocation(widget, &a);
    if (a.height <= 0) {
        return 0.0;
    }
    return MIN(10.0, (gdouble)a.height / lines);
}

/* First and last line currently visible in the viewer */
void minimap_viewport(GtkTextView* view, gint* first, gint* last) {
    GdkRectangle vis;
    GtkTextIter it;
    gint line_y;
    gtk_text_view_get_visible_rect(view, &vis);
    gtk_text_view_get_line_at_y(view, &it, vis.y, &line_y);
    *first = gtk_text_iter_get_line(&it);
    gtk_text_view_get_line_at_y(view, &it, vis.y + vis.height - 1, &line_y);
    *last = gtk_text_iter_get_line(&it);
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
    if (app->minimap != NULL) {
        gtk_widget_queue_draw(app->minimap);
    }
}

/* Hide the minimap when the whole file is visible in the viewer */
void minimap_update_visibility(AppData* app) {
    GtkAdjustment* adj;
    gboolean useful;
    if (app->minimap == NULL || app->viewer_vadj == NULL) {
        return;
    }
    adj = app->viewer_vadj;
    useful = gtk_adjustment_get_upper(adj) >
             gtk_adjustment_get_page_size(adj) + 1.5;
    if (gtk_widget_get_visible(app->minimap) == useful) {
        return;
    }
    if (!useful) {
        minimap_invalidate(app);
    }
    gtk_widget_set_visible(app->minimap, useful);
}

void on_minimap_adj_changed(GtkAdjustment* adjustment, gpointer data) {
    (void)adjustment;
    minimap_update_visibility((AppData*)data);
}

gboolean on_minimap_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    AppData* app = (AppData*)data;
    GtkAllocation a;
    gdouble mm;
    gint first, last;

    gtk_widget_get_allocation(widget, &a);
    if (a.width <= 0 || a.height <= 0) {
        return FALSE;
    }
    if (app->mm_cache == NULL || app->mm_cache_w != a.width ||
        app->mm_cache_h != a.height) {
        minimap_build_cache(widget, app, a.width, a.height);
        app->mm_cache_w = a.width;
        app->mm_cache_h = a.height;
    }

    cairo_set_source_surface(cr, app->mm_cache, 0, 0);
    cairo_paint(cr);

    /* Slider: the viewer's visible range */
    mm = minimap_line_height(widget, app->buffer);
    if (mm > 0.0) {
        minimap_viewport(GTK_TEXT_VIEW(app->text_view), &first, &last);
        cairo_set_source_rgba(cr, 0.47, 0.47, 0.47, 0.35);
        cairo_rectangle(cr, 0, first * mm, a.width,
                        (last - first + 1) * mm);
        cairo_fill(cr);
    }

    /* Thin border on the viewer side */
    cairo_set_source_rgb(cr, 40.0 / 255.0, 42.0 / 255.0, 44.0 / 255.0);
    cairo_rectangle(cr, 0, 0, 1, a.height);
    cairo_fill(cr);

    return FALSE;
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

gboolean on_minimap_button_press(GtkWidget* widget,
                                 GdkEventButton* event,
                                 gpointer data) {
    (void)widget;
    AppData* app = (AppData*)data;
    if (event->type != GDK_BUTTON_PRESS || event->button != 1) {
        return FALSE;
    }
    app->mm_dragging = TRUE;
    minimap_goto(app, event->y);
    return TRUE;
}

gboolean on_minimap_motion(GtkWidget* widget, GdkEventMotion* event,
                           gpointer data) {
    (void)widget;
    AppData* app = (AppData*)data;
    if (app->mm_dragging) {
        minimap_goto(app, event->y);
        return TRUE;
    }
    return FALSE;
}

gboolean on_minimap_button_release(GtkWidget* widget,
                                   GdkEventButton* event,
                                   gpointer data) {
    (void)widget;
    (void)event;
    AppData* app = (AppData*)data;
    if (app->mm_dragging) {
        app->mm_dragging = FALSE;
        return TRUE;
    }
    return FALSE;
}

void on_minimap_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    minimap_invalidate((AppData*)data);
    minimap_update_visibility((AppData*)data);
}

void on_minimap_scroll(GtkAdjustment* adjustment, gpointer data) {
    (void)adjustment;
    gtk_widget_queue_draw(((AppData*)data)->minimap);
}

void on_minimap_size_allocate(GtkWidget* widget, GtkAllocation* allocation,
                              gpointer data) {
    (void)widget;
    (void)allocation;
    minimap_invalidate((AppData*)data);
}

void on_minimap_realize(GtkWidget* widget, gpointer data) {
    (void)widget;
    minimap_update_visibility((AppData*)data);
}
