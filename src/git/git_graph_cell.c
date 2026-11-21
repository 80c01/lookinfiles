/* The cell of the Graph column of the History view (see git_graph_cell.h) */
#include "git/git_graph_cell.h"
#include "theme/palette.h"
#include <math.h>
#include <string.h>

#define ROW_KEY "lookin-git-graph-row"
#define LANE_W 14.0      /* room of a lane */
#define LINE_W 1.6       /* thickness of its line */
#define DOT_R 3.6        /* radius of a commit's dot */
#define CELL_PAD_X 12    /* the horizontal padding of the table's cells (theme.c) */

/* The colours of the lanes, in turn */
static const PaletteRole LANE_COLORS[] = {
    PAL_ACCENT_FG, PAL_GIT_ADDED,  PAL_ICON_ORANGE,   PAL_ICON_PURPLE,
    PAL_GIT_MODIFIED, PAL_ICON_PINK, PAL_DIFF_HUNK_FG, PAL_ICON_YELLOW,
};

static void set_lane_color(cairo_t* cr, guint color) {
    double r, g, b;
    palette_rgb(LANE_COLORS[color % G_N_ELEMENTS(LANE_COLORS)], &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
}

static double lane_x(guint col) {
    return LANE_W * col + LANE_W / 2.0;
}

/* A line from (x1, y1) down to (x2, y2): straight in one lane, else a
 * curve leaving and reaching its lanes upright */
static void draw_line(cairo_t* cr, double x1, double y1, double x2, double y2) {
    cairo_move_to(cr, x1, y1);
    if (x1 == x2) {
        cairo_line_to(cr, x2, y2);
    } else {
        double ym = (y1 + y2) / 2.0;
        cairo_curve_to(cr, x1, ym, x2, ym, x2, y2);
    }
    cairo_stroke(cr);
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)width;
    (void)data;
    const GitGraphRow* row = g_object_get_data(G_OBJECT(area), ROW_KEY);
    if (row == NULL) return;
    double mid = height / 2.0;
    cairo_set_line_width(cr, LINE_W);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    for (guint i = 0; i < row->n_lines; i++) {
        const GitGraphLine* l = &row->lines[i];
        set_lane_color(cr, l->color);
        if (l->bottom) {
            draw_line(cr, lane_x(l->from), mid, lane_x(l->to), height);
        } else {
            draw_line(cr, lane_x(l->from), 0.0, lane_x(l->to), mid);
        }
    }
    set_lane_color(cr, row->node_color);
    cairo_arc(cr, lane_x(row->node), mid, DOT_R, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
}

GtkWidget* git_graph_cell_new(void) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);
    return area;
}

void git_graph_cell_set_row(GtkWidget* cell, const GitGraphRow* row) {
    GitGraphRow* copy = NULL;
    if (row != NULL) {
        gsize size = sizeof(GitGraphRow) + row->n_lines * sizeof(GitGraphLine);
        copy = g_malloc(size);
        memcpy(copy, row, size);
    }
    g_object_set_data_full(G_OBJECT(cell), ROW_KEY, copy, g_free);
    gtk_widget_queue_draw(cell);
}

gint git_graph_cell_column_width(guint lanes) {
    lanes = CLAMP(lanes, 1, GIT_GRAPH_CELL_MAX_LANES);
    return (gint)ceil(LANE_W * lanes) + CELL_PAD_X;
}
