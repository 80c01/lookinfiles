#ifndef THEME_STORE_H
#define THEME_STORE_H

/* Themes: named sets of palette colours and fonts (palette.h, fonts.h),
 * built in or written by the user as INI files in
 * ~/.config/lookinfiles/themes/:
 *
 *   [Theme]
 *   name=Light Modern
 *   dark=false
 *   inherits=Dark Modern
 *   icons=desktop
 *   # values a theme leaves out come from the theme it inherits
 *   # icons: name of an icon set (icon_sets.h), "default" for none
 *
 *   [Colors]
 *   bg_window=#f8f8f8      (palette role names, palette.h)
 *
 *   [Fonts]
 *   ui=Segoe UI, Ubuntu, sans-serif
 *   ui_size=13
 *   code=JetBrains Mono, monospace
 *   code_size=14
 *
 * A theme without "inherits" starts from the default values (Dark Modern).
 * The chosen theme is remembered in ~/.config/lookinfiles/theme.ini.
 *
 * Pure data and file I/O (GKeyFile), no widgets: unit-tested in tests/. */
#include <glib.h>

#include "theme/fonts.h"

/* Name of the default theme: the palette and font defaults */
#define THEME_DEFAULT_NAME "Dark Modern"

typedef struct {
    gchar* name;
    gchar* inherits;     /* NULL: starts from the defaults */
    gboolean dark;       /* dark interface (GTK widgets, syntax scheme base) */
    gboolean has_dark;   /* "dark" given in the file (else inherited) */
    gchar* icon_set;     /* "icons" key: set name or "default"; NULL: inherited */
    gchar* path;         /* file of a user theme, NULL for a built-in one */
    GHashTable* colors;  /* role name -> "#rrggbb", the file's own values */
    gchar* font_family[FONT_COUNT]; /* NULL: inherited */
    gint font_size[FONT_COUNT];     /* 0: inherited */
} Theme;

void theme_free(gpointer theme);

/* A theme from INI text; path is kept as its file (NULL for a built-in).
 * Unknown groups, keys or colour roles, invalid colours or fonts are
 * errors naming the group and key. */
Theme* theme_parse(const gchar* text, const gchar* path, GError** error);

/* Built-in themes, then the user themes of dir (*.ini, by name; a user
 * theme with a built-in theme's name replaces it). Files that do not parse
 * are skipped and described in errors (strings, may be NULL). */
GPtrArray* theme_store_list(const gchar* dir, GPtrArray* errors);
Theme* theme_store_find(GPtrArray* themes, const gchar* name);

/* Set the palette and fonts to a theme: the defaults, then each theme of
 * its inheritance chain from the root. dark receives its interface kind
 * (may be NULL). FALSE (and nothing changed) on an unknown theme or
 * parent, or an inheritance loop. */
gboolean theme_store_apply(GPtrArray* themes, const gchar* name, gboolean* dark, GError** error);

/* Icon set of the theme applied last: NULL for the default icons. Owned by
 * the store. */
const gchar* theme_store_icon_set(void);
/* TRUE for a usable icon set name: letters, digits, '-' and '_' */
gboolean theme_store_icon_set_valid(const gchar* name);

/* The current palette and fonts as a self-contained theme file */
gchar* theme_store_current_to_ini(const gchar* name, gboolean dark);
/* Write it to dir/<name>.ini (name lowercased, other characters as '-'),
 * creating dir. Returns the path (caller frees), NULL on error. */
gchar* theme_store_save_current(const gchar* dir, const gchar* name, gboolean dark, GError** error);

/* The user's fonts, set over any theme's (theme.ini [Fonts], same keys
 * as a theme file): NULL / 0 keeps the theme's value */
typedef struct {
    gchar* family[FONT_COUNT];
    gint size[FONT_COUNT];
} FontChoice;

void font_choice_clear(FontChoice* fonts);
/* Read them (invalid values are ignored); fonts is cleared first */
void theme_store_load_fonts(const gchar* path, FontChoice* fonts);
/* Write them, keeping the chosen theme */
gboolean theme_store_save_fonts(const gchar* path, const FontChoice* fonts, GError** error);
/* fonts_set() each value given */
void theme_store_apply_fonts(const FontChoice* fonts);

/* ~/.config/lookinfiles/themes and ~/.config/lookinfiles/theme.ini. Caller frees. */
gchar* theme_store_user_dir(void);
gchar* theme_store_choice_path(void);
/* The remembered theme name (THEME_DEFAULT_NAME if none). Caller frees. */
gchar* theme_store_load_choice(const gchar* path);
/* Write the name, keeping the user's fonts */
gboolean theme_store_save_choice(const gchar* path, const gchar* name, GError** error);

#endif /* THEME_STORE_H */
