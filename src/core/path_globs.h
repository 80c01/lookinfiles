#ifndef PATH_GLOBS_H
#define PATH_GLOBS_H

/* A list of path patterns separated by commas ("*.c, src/, tests"), matched
 * against paths relative to a project root, as in .gitignore:
 * - `*` and `?` are wildcards (`*` also crosses folders);
 * - a pattern without `/` matches a file or folder name at any depth
 *   (`*.md`, `tests`);
 * - a pattern with a `/` (a leading one included) matches from the root
 *   (`src/ui`, `/build`), the path itself or one of its folders;
 * - a trailing `/` matches folders only (`build/`).
 * Pure GLib, unit-tested. */
#include <glib.h>

typedef struct PathGlobs PathGlobs;

/* NULL when the list has no pattern (empty or only commas and spaces) */
PathGlobs* path_globs_new(const gchar* list);
void path_globs_free(PathGlobs* globs);

/* rel ("dir/file.c", no leading slash) matches one of the patterns */
gboolean path_globs_match(const PathGlobs* globs, const gchar* rel);

#endif /* PATH_GLOBS_H */
