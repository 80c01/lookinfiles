#ifndef IMAGE_PREVIEW_H
#define IMAGE_PREVIEW_H

#include "app_types.h"

/* Image preview in the preview text view: decoded in a worker thread at the
 * size it is shown (fitted to the panel, or 1:1 with m), turned upright from
 * its EXIF orientation, with the photo details (camera, exposure, date, GPS)
 * under it. Zoom: + / - / 0 and Ctrl+wheel (app->preview_zoom, relative to
 * the fitted size; PDF pages use it too). */

/* Render the image preview of a file (cancels the previous preview) */
void render_image_preview(AppData* app, const gchar* name, const gchar* full);

/* Zoom keys for images and PDFs: + / = zoom in, - zoom out, 0 back to the
 * fitted size. TRUE if the key was used (the previewed file zooms). */
gboolean image_preview_zoom_key(AppData* app, guint keyval);

/* Ctrl+wheel over the preview zooms images and PDFs */
void image_preview_attach_zoom(AppData* app, GtkWidget* text_view);

/* Most zoom in / out, relative to the fitted size */
#define PREVIEW_ZOOM_MIN 0.1
#define PREVIEW_ZOOM_MAX 8.0

#endif /* IMAGE_PREVIEW_H */
