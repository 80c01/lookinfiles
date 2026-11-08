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

/* The preview's minimap, beside text_view in scroll (once, when the window
 * is built); NULL-safe calls below before it exists and after its free */
GtkWidget* minimap_viewer_new(AppData* app, GtkWidget* scroll);
/* TRUE while the preview's minimap exists but is hidden */
gboolean minimap_viewer_is_hidden(AppData* app);
/* Drop the preview minimap's render (theme changed) */
void minimap_viewer_cache_clear(AppData* app);
/* Free the preview minimap's state (at exit) */
void minimap_viewer_free(AppData* app);

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

#endif /* MINIMAP_H */
