#ifndef THEME_H
#define THEME_H

/* Application-wide CSS theme. Colours come from the palette (palette.h):
 * write them as named colours (@bg_window, alpha(@accent, 0.25)...). */
#include <glib.h>

/* Install the global stylesheet (call once at startup) */
void apply_panel_borders(void);

/* Add a stylesheet for the display, with the palette's named colours
 * defined. css must stay valid (a static string): it is reloaded by
 * theme_reload(). */
void theme_add_css(const gchar* css);

/* Reload every stylesheet with the palette's current values (after
 * palette_set()) */
void theme_reload(void);

#endif /* THEME_H */
