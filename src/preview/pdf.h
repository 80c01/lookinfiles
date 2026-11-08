#ifndef PDF_H
#define PDF_H

#include "core/app_types.h"

void render_pdf_preview(AppData* app, const gchar* name, const gchar* full);

/* PDF preview: scroll to the next (direction > 0) or previous page. FALSE
 * if the preview does not show rendered PDF pages. */
gboolean pdf_preview_jump(AppData* app, gint direction);

/* Re-render the shown PDF if the viewport width changed since it was fitted. */
void pdf_preview_refit(AppData* app);

#endif /* PDF_H */
