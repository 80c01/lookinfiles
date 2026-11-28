#ifndef FOLDER_COMPLETE_H
#define FOLDER_COMPLETE_H

/* Text of a folder field split for completion (path_suggest.c): the
 * folder to list and the start of the name typed after its last '/', and
 * how the subfolder names found match it. Pure, unit-tested. */
#include <glib.h>

/* text is a path ("/usr/lo", "~/pro", "~"): *dir is the folder to list
 * ('~' expanded with home), *prefix the part after the last '/' (both newly
 * allocated). FALSE (nothing set) for any other text: a name to match
 * against the recent folders. */
gboolean folder_complete_split(const gchar* text, const gchar* home, gchar** dir, gchar** prefix);

/* The field text for the subfolder name of the folder typed in text: text
 * up to its last '/' (kept as typed, '~' included), then name ("~/pro",
 * "projects" -> "~/projects"; "~" -> "~/projects"). Newly allocated. */
gchar* folder_complete_join(const gchar* text, const gchar* name);

/* How the subfolder name matches prefix: 2 starts with it (same case), 1
 * starts with it (any case), 0 holds it elsewhere (any case), -1 no match.
 * Hidden names (".git") match only a prefix starting with '.'. */
gint folder_complete_name_score(const gchar* name, const gchar* prefix);

/* TRUE for the name of a Python program: python, python3, python3.12,
 * pypy3 (and with .exe) */
gboolean folder_complete_is_python(const gchar* name);

/* path with home shown as '~' ("/home/me/src" -> "~/src"), newly allocated */
gchar* folder_complete_tilde(const gchar* path, const gchar* home);

#endif /* FOLDER_COMPLETE_H */
