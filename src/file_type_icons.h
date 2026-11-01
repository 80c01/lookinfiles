#ifndef FILE_TYPE_ICONS_H
#define FILE_TYPE_ICONS_H

/* VS Code file-type icons (Seti theme, the VS Code default): which glyph and
 * which colour a file name gets, as in VS Code. The glyphs are bundled as
 * icons/scalable/actions/lookin-ft-<icon>-symbolic.svg and the colours are
 * the palette roles icon_<color>; both, and the lookup table, are generated
 * by tools/gen_file_icons.py. Pure GLib, unit-tested. */
#include <glib.h>

typedef struct {
    const gchar* icon;  /* glyph: "c", "markdown", "default"... */
    const gchar* color; /* colour: "blue", "purple", "white"... */
} FileTypeIcon;

/* Icon of a file name (file names first, then extensions from the longest,
 * then the default icon). Directories get the "folder" glyph. */
FileTypeIcon file_type_icon(const gchar* name, gboolean is_dir);

/* Colour names used by file_type_icon(), NULL-terminated */
extern const gchar* const FILE_TYPE_ICON_COLORS[];

#endif /* FILE_TYPE_ICONS_H */
