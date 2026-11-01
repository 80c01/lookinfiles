#ifndef NOTEBOOK_ICONS_H
#define NOTEBOOK_ICONS_H

/* Icons of the notebooks of the notes view and of the bookmark folders:
 * theme glyph names (icons.h), chosen from a fixed set. Pure GLib,
 * unit-tested. */
#include <glib.h>

#define NOTEBOOK_ICON_DEFAULT "notebook"
#define BOOKMARK_FOLDER_ICON_DEFAULT "folder"

/* Icons offered by the notebook and bookmark folder dialogs, NULL-terminated */
extern const gchar* const NOTEBOOK_ICONS[];

/* Icon to use for an icon stored in notes.xml: one of NOTEBOOK_ICONS as is,
 * an emoji written by older versions mapped to the matching icon, anything
 * else (or NULL) the default. Static string. */
const gchar* notebook_icon_normalize(const gchar* stored);

/* Icon to use for a folder icon stored in bookmarks.xml: one of
 * NOTEBOOK_ICONS as is, anything else (or NULL) the default. Static string. */
const gchar* bookmark_folder_icon_normalize(const gchar* stored);

#endif /* NOTEBOOK_ICONS_H */
