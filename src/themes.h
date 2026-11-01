#ifndef THEMES_H
#define THEMES_H

/* Themes in the running application (theme files: theme_store.h).
 * Everything follows a change: stylesheets, text styles of every buffer,
 * syntax scheme, terminals, drawn widgets, explorer badges and the preview.
 *
 * The user's fonts (theme.ini [Fonts]) apply over any theme; the zoom keys
 * change their sizes. */
#include "app_types.h"
#include "dialog.h"

/* Apply the theme named name (with the user's fonts) to the whole
 * application; remember it as the chosen theme when remember is TRUE
 * (FALSE for a live preview). FALSE (and nothing changed) if the theme
 * cannot be applied. */
gboolean themes_apply(AppData* app, const gchar* name, gboolean remember, GError** error);

/* Name of the theme shown (the chosen one, or the one previewed) */
const gchar* themes_current_name(void);

/* After palette_set() / fonts_set(): redraw the application with the new
 * values (dark: the interface kind of the theme) */
void themes_refresh(AppData* app, gboolean dark);

/* Ctrl+= / Ctrl+- / Ctrl+0: interface and code fonts one pixel larger
 * (delta 1), smaller (-1), or back to the theme's sizes (0); remembered */
void themes_zoom(AppData* app, gint delta);

/* APP_EVENT_FILES_CHANGED: the file of the current theme was saved (edited
 * in a tab): apply it again, or show why it cannot be */
void themes_on_files_changed(AppData* app);

/* The Appearance page of the Preferences dialog (preferences.h): pick a
 * theme with a live preview, the interface and code fonts, Save As, Edit
 * and Delete of user themes. Its buttons report these responses on the
 * dialog; the dialog's loop passes them to themes_page_handle(). */
enum { THEMES_RESPONSE_SAVE_AS = 1, THEMES_RESPONSE_EDIT = 2, THEMES_RESPONSE_DELETE = 3 };

typedef struct ThemesPage ThemesPage;

ThemesPage* themes_page_new(AppData* app, AppDialog* dialog);
GtkWidget* themes_page_widget(ThemesPage* page);
/* Widget to focus when the page is shown (the selected theme) */
GtkWidget* themes_page_focus(ThemesPage* page);

/* Act on a page response; FALSE if response is not one of the page's.
 * *close is set when the dialog must close and keep the choices (Edit). */
gboolean themes_page_handle(ThemesPage* page, gint response, gboolean* close);

/* The dialog closes: keep (Apply, Edit) saves the theme and fonts shown,
 * else the look before the dialog comes back */
void themes_page_finish(ThemesPage* page, gboolean keep);

/* Free the page once the dialog is destroyed; opens the theme file chosen
 * with Edit in an editor tab */
void themes_page_free(ThemesPage* page);

#endif /* THEMES_H */
