#ifndef CANVAS_CROP_H
#define CANVAS_CROP_H

#include "canvas_doc.h"
#include <cairo.h>
#include <gtk/gtk.h>

/* Draw image with relative crop (0..1) into destination rectangle */
void canvas_draw_cropped_image(cairo_t* cr, const gchar* full_path,
                               double crop_x, double crop_y, double crop_w, double crop_h,
                               int dest_w, int dest_h);

typedef void (*CanvasCropCallback)(CanvasBlock* b, gpointer user_data);

/* Show the modal Crop dialog (AppDialog) for an image block */
void canvas_crop_dialog_show(GtkWindow* parent_window, const gchar* full_path,
                             CanvasBlock* b, CanvasCropCallback on_done, gpointer user_data);

#endif /* CANVAS_CROP_H */
