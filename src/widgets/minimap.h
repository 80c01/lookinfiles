#ifndef MINIMAP_H
#define MINIMAP_H

#include "core/app_types.h"

/* The preview's minimap: a minimap_new_for_view() of app->text_view (already
 * in its GtkScrolledWindow) shown only on the Preview page (once, when the
 * window is built); the calls below do nothing before it exists */
GtkWidget* minimap_viewer_new(AppData* app);
/* TRUE while the preview's minimap exists but is hidden (even when
 * minimap_enabled() is FALSE) */
gboolean minimap_viewer_is_hidden(AppData* app);
/* Drop the preview minimap's render (theme changed) */
void minimap_viewer_cache_clear(AppData* app);
/* Redraw the preview's minimap / check whether it should show */
void minimap_invalidate(AppData* app);
void minimap_update_visibility(AppData* app);

/* A minimap beside text_view (already in its GtkScrolledWindow), shown while
 * the text is taller than the view; it frees its state with the widget. who
 * names it in the LOOKIN_DEBUG_MINIMAP log ("preview", "help", "editor"). */
GtkWidget* minimap_new_for_view(GtkWidget* text_view, const gchar* who);
/* Extra condition to show a minimap_new_for_view() (checked with the text height) */
typedef gboolean (*MinimapShownIf)(gpointer data);
void minimap_view_set_shown_if(GtkWidget* minimap, MinimapShownIf shown_if, gpointer data);
/* Check whether to show / drop the render of a minimap_new_for_view() */
void minimap_view_update_visibility(GtkWidget* minimap);
void minimap_view_cache_clear(GtkWidget* minimap);

/* Show (TRUE, the default) or hide every minimap, the ones already made too
 * (settings.ini: minimap); a hidden minimap gives back the scrollbar */
void minimap_set_enabled(gboolean enabled);
gboolean minimap_enabled(void);

#endif /* MINIMAP_H */
