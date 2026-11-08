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

/* The preview's minimap: a minimap_new_for_view() of app->text_view (already
 * in its GtkScrolledWindow) shown only on the Preview page (once, when the
 * window is built); the calls below do nothing before it exists */
GtkWidget* minimap_viewer_new(AppData* app);
/* TRUE while the preview's minimap exists but is hidden */
gboolean minimap_viewer_is_hidden(AppData* app);
/* Drop the preview minimap's render (theme changed) */
void minimap_viewer_cache_clear(AppData* app);
/* Redraw the preview's minimap / check whether it should show */
void minimap_invalidate(AppData* app);
void minimap_update_visibility(AppData* app);
/* Mouse wheel over minimap scrolls text_view */
void minimap_attach_wheel(GtkWidget* minimap, GtkWidget* text_view);

/* A minimap beside text_view (already in its GtkScrolledWindow), shown while
 * the text is taller than the view; it frees its state with the widget */
GtkWidget* minimap_new_for_view(GtkWidget* text_view);
/* Extra condition to show a minimap_new_for_view() (checked with the text
 * height), and its name in the LOOKIN_DEBUG_MINIMAP log */
typedef gboolean (*MinimapShownIf)(gpointer data);
void minimap_view_set_shown_if(GtkWidget* minimap, const gchar* who, MinimapShownIf shown_if, gpointer data);
/* Redraw / check whether to show / drop the render of a minimap_new_for_view() */
void minimap_view_invalidate(GtkWidget* minimap);
void minimap_view_update_visibility(GtkWidget* minimap);
void minimap_view_cache_clear(GtkWidget* minimap);

/* Editor tab minimap support */
void editor_minimap_init(EditorTab* tab);
void editor_minimap_invalidate(EditorTab* tab);
void editor_minimap_update_visibility(EditorTab* tab);

#endif /* MINIMAP_H */
