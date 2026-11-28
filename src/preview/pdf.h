#ifndef PDF_H
#define PDF_H

#include "core/app_types.h"

void render_pdf_preview(AppData* app, const gchar* name, const gchar* full);

/* PDF preview: scroll to the next (direction > 0) or previous page. FALSE
 * if the preview does not show rendered PDF pages. */
gboolean pdf_preview_jump(AppData* app, gint direction);

/* PDF preview: scroll to the point y (PDF points from the top) of page
 * (0-based), at height align of the view (0: top, 1: bottom). FALSE if the
 * preview does not show that page rendered (yet). */
gboolean pdf_preview_show_point(AppData* app, gint page, gdouble y, gdouble align);

/* Height in the view (0: top, 1: bottom) of the point y of page, -1 if it is
 * not on screen */
gdouble pdf_preview_point_align(AppData* app, gint page, gdouble y);

/* The rendered page under the point (x, y) of the preview's buffer
 * (gtk_text_view_window_to_buffer_coords()): its index (0-based) and the
 * point in PDF points from its top left. FALSE when no page is there. */
gboolean pdf_preview_page_at(AppData* app, gint x, gint y, gint* page, gdouble* px, gdouble* py);

/* Re-render the shown PDF if the viewport width changed since it was fitted. */
void pdf_preview_refit(AppData* app);

#endif /* PDF_H */
