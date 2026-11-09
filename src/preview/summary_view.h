#ifndef SUMMARY_VIEW_H
#define SUMMARY_VIEW_H

/* Draws a preview_summary (notebook, bookmark folder, trash, remote machine
 * or folder) in a text view, in the style of the directory preview: title
 * with the row's icon, dimmed fields, section bands, items with their icons,
 * keys in bold. Columns are aligned with tab stops measured in the view's
 * font, so the view keeps its proportional font. */
#include <gtk/gtk.h>

#include "preview/preview_summary.h"

/* Replace the text of view with lines (PreviewSummaryLine); empty for NULL */
void summary_view_render(GtkWidget* view, GPtrArray* lines);

#endif /* SUMMARY_VIEW_H */
