#ifndef NOTEBOOK_ICONS_H
#define NOTEBOOK_ICONS_H

/* Icons of the notebooks of the notes view: theme glyph names (icons.h),
 * chosen from a fixed set. Pure GLib, unit-tested. */
#include <glib.h>

#define NOTEBOOK_ICON_DEFAULT "notebook"

/* Icons offered by the notebook dialog, NULL-terminated */
extern const gchar* const NOTEBOOK_ICONS[];

/* Icon to use for an icon stored in notes.xml: one of NOTEBOOK_ICONS as is,
 * an emoji written by older versions mapped to the matching icon, anything
 * else (or NULL) the default. Static string. */
const gchar* notebook_icon_normalize(const gchar* stored);

#endif /* NOTEBOOK_ICONS_H */
