/* The "+" add button (see add_button.h) */
#include "widgets/add_button.h"
#include "theme/palette.h"

/* Palette colour a, mixed with t of colour b */
static void set_source_mix(cairo_t* cr, PaletteRole a, PaletteRole b, gdouble t) {
    double ar, ag, ab, br, bg, bb;
    palette_rgb(a, &ar, &ag, &ab);
    palette_rgb(b, &br, &bg, &bb);
    cairo_set_source_rgb(cr, ar + (br - ar) * t, ag + (bg - ag) * t, ab + (bb - ab) * t);
}

void add_button_draw(cairo_t* cr, gdouble x, gdouble y, gdouble w, gdouble h, gboolean hover) {
    gdouble r = MIN(3.0, MIN(w, h) * 0.5);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI * 0.5, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, G_PI * 0.5);
    cairo_arc(cr, x + r, y + h - r, r, G_PI * 0.5, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, G_PI * 1.5);
    cairo_close_path(cr);
    if (hover) {
        set_source_mix(cr, PAL_BG_BAR, PAL_BG_SELECTED, 0.4);
    } else {
        set_source_mix(cr, PAL_BG_WINDOW, PAL_BG_WINDOW, 0.0);
    }
    cairo_fill_preserve(cr);
    set_source_mix(cr, hover ? PAL_ACCENT : PAL_BORDER_STRONG, hover ? PAL_ACCENT : PAL_BORDER_STRONG, 0.0);
    cairo_set_line_width(cr, hover ? 1.3 : 1.0);
    cairo_stroke(cr);

    gdouble cx = x + w * 0.5;
    gdouble cy = y + h * 0.5;
    gdouble arm = 4.0;
    set_source_mix(cr, hover ? PAL_FG_BRIGHT : PAL_FG_MUTED, hover ? PAL_FG_BRIGHT : PAL_FG_MUTED, 0.0);
    cairo_set_line_width(cr, 1.6);
    cairo_move_to(cr, cx - arm, cy);
    cairo_line_to(cr, cx + arm, cy);
    cairo_move_to(cr, cx, cy - arm);
    cairo_line_to(cr, cx, cy + arm);
    cairo_stroke(cr);
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    GtkWidget* button = data;
    gboolean hover = (gtk_widget_get_state_flags(button) & GTK_STATE_FLAG_PRELIGHT) != 0;
    /* Inside the area: half a pixel in, so the border is not cut */
    add_button_draw(cr, 0.5, 0.5, width - 1.0, height - 1.0, hover);
}

static void on_state_changed(GtkWidget* button, GtkStateFlags previous, gpointer data) {
    (void)previous;
    (void)button;
    gtk_widget_queue_draw(GTK_WIDGET(data));
}

GtkWidget* add_button_new(const gchar* tooltip) {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_add_css_class(button, "tab-add-btn");
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    if (tooltip != NULL) gtk_widget_set_tooltip_text(button, tooltip);
    GtkWidget* area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), ADD_BUTTON_WIDTH);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), ADD_BUTTON_HEIGHT);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, button, NULL);
    gtk_button_set_child(GTK_BUTTON(button), area);
    g_signal_connect(button, "state-flags-changed", G_CALLBACK(on_state_changed), area);
    return button;
}
