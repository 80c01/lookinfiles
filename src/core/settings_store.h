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
 *   tagbar=false         # code and Markdown editor tabs show the tagbar
 *   minimap=true         # editor tabs, the preview and Help show a minimap
 *   [Preview]
 *   tagbar=false         # previews with headings show the tagbar
 *   [Layouts]
 *   auto-terminal=true   # the Auto layout shows (true) or hides (false) the
 *   auto-preview=false   # terminal / preview / editor tagbar; missing: as
 *   auto-tagbar=false    # they are
 *   auto-terminal-place=below  # the Auto layout puts the terminal below the
 *                              # preview or beside it; missing: as it is
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

/* What the Auto layout does with a panel */
typedef enum {
    SETTINGS_PANEL_KEEP,  /* as it is (the key is missing) */
    SETTINGS_PANEL_SHOW,
    SETTINGS_PANEL_HIDE
} SettingsPanel;

/* Where the Auto layout puts the terminal (Ctrl+Shift+L) */
typedef enum {
    SETTINGS_PLACE_KEEP,  /* where it is (the key is missing) */
    SETTINGS_PLACE_BELOW, /* below the preview and the editor panel */
    SETTINGS_PLACE_BESIDE /* on their right */
} SettingsPlace;

typedef struct {
    SettingsEditor editor; /* mode of new editor tabs (Org files: Org-mode) */
    gboolean line_numbers; /* new editor tabs show line numbers */
    gboolean wrap;         /* new editor tabs wrap long lines */
    gint color_column;     /* vertical line in editors at this column, 0: none */
    gboolean tagbar;       /* code and Markdown editor tabs show the tagbar */
    gboolean preview_tagbar; /* previews with headings show the tagbar */
    gboolean minimap;      /* editor tabs, the preview and Help show a minimap */
    SettingsPanel auto_terminal; /* terminal panel of the Auto layout */
    SettingsPanel auto_preview;  /* preview panel of the Auto layout */
    SettingsPanel auto_tagbar;   /* editor tagbar of the Auto layout */
    SettingsPlace auto_terminal_place; /* terminal place of the Auto layout */
    gint width;            /* window size at startup */
    gint height;
} Settings;

/* Notepad, no line numbers, no wrap, column line at 81, no tagbars, minimap, Auto
 * keeping the panels, 1000 x 600 */
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
