#ifndef MINIMAP_H
#define MINIMAP_H

#include "app_types.h"

gdouble minimap_line_height(GtkWidget* widget, GtkTextBuffer* buffer);
void minimap_viewport(GtkTextView* view, gint* first, gint* last);
void minimap_invalidate(AppData* app);
void minimap_update_visibility(AppData* app);
void minimap_goto(AppData* app, gdouble y);

gboolean on_minimap_draw(GtkWidget* widget, cairo_t* cr, gpointer data);
gboolean on_minimap_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data);
gboolean on_minimap_motion(GtkWidget* widget, GdkEventMotion* event, gpointer data);
gboolean on_minimap_button_release(GtkWidget* widget, GdkEventButton* event, gpointer data);
void on_minimap_buffer_changed(GtkTextBuffer* buffer, gpointer data);
void on_minimap_scroll(GtkAdjustment* adjustment, gpointer data);
void on_minimap_adj_changed(GtkAdjustment* adjustment, gpointer data);
void on_minimap_size_allocate(GtkWidget* widget, GtkAllocation* allocation, gpointer data);
void on_minimap_realize(GtkWidget* widget, gpointer data);

#endif /* MINIMAP_H */
