#ifndef PDF_H
#define PDF_H

#include "app_types.h"

void render_pdf_preview(AppData* app, const gchar* name, const gchar* full);

/* PDF preview: scroll to the next (direction > 0) or previous page. FALSE
 * if the preview does not show rendered PDF pages. */
gboolean pdf_preview_jump(AppData* app, gint direction);

#endif /* PDF_H */
