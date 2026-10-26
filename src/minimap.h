#ifndef MINIMAP_H
#define MINIMAP_H

#include "app_types.h"

gdouble minimap_line_height(GtkWidget* widget, GtkTextBuffer* buffer);
void minimap_viewport(GtkTextView* view, gint* first, gint* last);
void minimap_invalidate(AppData* app);
void minimap_update_visibility(AppData* app);
void minimap_goto(AppData* app, gdouble y);

void on_minimap_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data);
void on_minimap_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data);
void on_minimap_motion(GtkEventControllerMotion* ctrl, gdouble x, gdouble y, gpointer data);
void on_minimap_released(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data);
void on_minimap_buffer_changed(GtkTextBuffer* buffer, gpointer data);
void on_minimap_scroll(GtkAdjustment* adjustment, gpointer data);
void on_minimap_adj_changed(GtkAdjustment* adjustment, gpointer data);
void on_minimap_resize(GtkDrawingArea* area, gint width, gint height, gpointer data);
void on_minimap_realize(GtkWidget* widget, gpointer data);

#endif /* MINIMAP_H */
