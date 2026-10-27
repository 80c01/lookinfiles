#ifndef MINIMAP_H
#define MINIMAP_H

#include "app_types.h"

gdouble minimap_line_height(GtkWidget* widget, GtkTextBuffer* buffer);
void minimap_viewport(GtkTextView* view, gint* first, gint* last);
void minimap_draw_content(GtkWidget* widget, cairo_t* cr, int width, int height,
                          GtkTextBuffer* buffer, GtkTextView* text_view,
                          cairo_surface_t** cache, gint* cache_w, gint* cache_h);
void minimap_goto_view(GtkWidget* minimap, GtkTextBuffer* buffer, GtkTextView* text_view, gdouble y);

void minimap_invalidate(AppData* app);
void minimap_update_visibility(AppData* app);
void minimap_goto(AppData* app, gdouble y);

/* Editor tab minimap support */
void editor_minimap_init(EditorTab* tab);
void editor_minimap_invalidate(EditorTab* tab);
void editor_minimap_update_visibility(EditorTab* tab);

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
