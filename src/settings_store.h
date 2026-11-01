#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

/* User preferences kept in settings.ini of the configuration directory
 * (written by the Preferences dialog, preferences.h):
 *
 *   [Editor]
 *   mode=vim             # notepad, vim or emacs: mode of new editor tabs
 *   line-numbers=true    # new editor tabs show line numbers
 *   wrap=false           # new editor tabs wrap long lines
 *   color-column=81      # vertical line at this column in editors (0: none)
 *   [Window]
 *   width=1000           # window size at startup
 *   height=600
 *
 * Command-line options (-e, --size) win over these values. Missing or
 * invalid values keep their defaults. Pure data and file I/O (GKeyFile),
 * no widgets: unit-tested in tests/. */
#include <glib.h>

typedef enum {
    SETTINGS_EDITOR_NOTEPAD,
    SETTINGS_EDITOR_VIM,
    SETTINGS_EDITOR_EMACS
} SettingsEditor;

#define SETTINGS_MIN_WIDTH  400
#define SETTINGS_MIN_HEIGHT 300
#define SETTINGS_MAX_SIZE   16384
#define SETTINGS_MAX_COLUMN 1000

typedef struct {
    SettingsEditor editor; /* mode of new editor tabs (Org files: Org-mode) */
    gboolean line_numbers; /* new editor tabs show line numbers */
    gboolean wrap;         /* new editor tabs wrap long lines */
    gint color_column;     /* vertical line in editors at this column, 0: none */
    gint width;            /* window size at startup */
    gint height;
} Settings;

/* Notepad, no line numbers, no wrap, no column line, 1000 x 600 */
void settings_defaults(Settings* s);

/* "notepad", "vim", "emacs" (any case): FALSE if name is none of them */
gboolean settings_editor_parse(const gchar* name, SettingsEditor* editor);
/* "notepad", "vim" or "emacs" */
const gchar* settings_editor_name(SettingsEditor editor);

/* ~/.config/lookinfiles/settings.ini. Caller frees. */
gchar* settings_store_path(void);

/* Fill s with the defaults, then the values of the file at path. FALSE if
 * the file is missing or unreadable (s holds the defaults). */
gboolean settings_store_load(const gchar* path, Settings* s);

/* Write s to path, keeping the other groups and keys of the file; creates
 * the directory if needed */
gboolean settings_store_save(const gchar* path, const Settings* s, GError** error);

#endif /* SETTINGS_STORE_H */
