#ifndef CANVAS_CROP_H
#define CANVAS_CROP_H

#include <cairo.h>
#include <gtk/gtk.h>

/* Draw image with relative crop (0..1) into destination rectangle */
void canvas_draw_cropped_image(cairo_t* cr, const gchar* full_path,
                               double crop_x, double crop_y, double crop_w, double crop_h,
                               int dest_w, int dest_h);

/* Called on Apply with the new crop rectangle (0..1) */
typedef void (*CanvasCropCallback)(double x, double y, double w, double h, gpointer user_data);

/* Show the Crop dialog (AppDialog) for a picture, starting from the crop
 * rectangle given (w or h <= 0: full picture). The dialog owns user_data
 * and calls destroy on it when it closes (destroy may be NULL). */
void canvas_crop_dialog_show(GtkWindow* parent_window, const gchar* full_path,
                             double x, double y, double w, double h,
                             CanvasCropCallback on_done, gpointer user_data, GDestroyNotify destroy);

#endif /* CANVAS_CROP_H */
