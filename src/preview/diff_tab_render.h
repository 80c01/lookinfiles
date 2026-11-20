/* Rendering helpers of Diff tabs (split/unified buffers and source line-number
 * gutters with change bars). Used only by diff_tab.c. */
#ifndef DIFF_TAB_RENDER_H
#define DIFF_TAB_RENDER_H

#include <gtk/gtk.h>
#include "formats/side_diff.h"

typedef struct {
    SideDiffDoc* const* doc_ptr; /* points to owner's SideDiffDoc* */
    gboolean is_right;
    GtkWidget* area;
    GtkWidget* view;
    GtkTextBuffer* buf;
    GtkAdjustment* vadj;
    gint width;
} DiffGutterPane;

void diff_gutter_pane_init(DiffGutterPane* gp, SideDiffDoc* const* doc_ptr,
                           GtkWidget* view, GtkTextBuffer* buf, GtkWidget* scroll,
                           gboolean is_right);
void diff_gutter_pane_refresh(DiffGutterPane* gp);

void diff_render_split(SideDiffDoc* doc, GtkTextBuffer* left_buf, GtkTextBuffer* right_buf,
                       DiffGutterPane* left_gutter, DiffGutterPane* right_gutter);

void diff_render_unified(SideDiffDoc* doc, GtkTextBuffer* unified_buf,
                         GArray* unified_hunks, GArray* unified_src_lines,
                         GArray* unified_src_right);

#endif /* DIFF_TAB_RENDER_H */
