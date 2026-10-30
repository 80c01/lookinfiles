#ifndef THEMES_H
#define THEMES_H

/* Theme switching in the running application (theme files: theme_store.h).
 * Everything follows: stylesheets, text styles of every buffer, syntax
 * scheme, terminals, drawn widgets, explorer badges and the preview. */
#include "app_types.h"

/* Apply the theme named name to the whole application; remember it as the
 * chosen theme when remember is TRUE (FALSE for a live preview). FALSE
 * (and nothing changed) if the theme cannot be applied. */
gboolean themes_apply(AppData* app, const gchar* name, gboolean remember, GError** error);

/* Name of the theme shown (the chosen one, or the one previewed) */
const gchar* themes_current_name(void);

/* After palette_set() / fonts_set(): redraw the application with the new
 * values (dark: the interface kind of the theme) */
void themes_refresh(AppData* app, gboolean dark);

#endif /* THEMES_H */
