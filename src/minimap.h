#ifndef MINIMAP_H
#define MINIMAP_H

#include "app_types.h"

/* Scaled drawing of text_view as displayed (wrapped lines, sizes, backgrounds,
 * tables, images) with the visible range; scrolls over long documents, as in
 * VS Code. The drawn part is cached in *cache. */
void minimap_draw_content(GtkWidget* widget, cairo_t* cr, int width, int height,
                          GtkTextBuffer* buffer, GtkTextView* text_view, MinimapCache* cache);
void minimap_cache_clear(MinimapCache* cache);
/* Press at y: drag the slider, or center the view on the line clicked;
 * *grab is then passed to minimap_drag_view() on each pointer motion */
void minimap_press_view(GtkWidget* minimap, GtkTextView* text_view, gdouble y, gdouble* grab);
void minimap_drag_view(GtkWidget* minimap, GtkTextView* text_view, gdouble y, gdouble grab);

void minimap_invalidate(AppData* app);
void minimap_update_visibility(AppData* app);
/* Mouse wheel over minimap scrolls text_view */
void minimap_attach_wheel(GtkWidget* minimap, GtkWidget* text_view);

/* A minimap beside text_view (already in its GtkScrolledWindow), shown while
 * the text is taller than the view; it frees its state with the widget */
GtkWidget* minimap_new_for_view(GtkWidget* text_view);

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
