#ifndef PATH_COPY_H
#define PATH_COPY_H

#include "core/app_types.h"

/* Copy Name / Copy Git Relative Path / Copy Path: the items of the explorer,
 * editor tab and Preview tab menus, copying a file's name, its path from
 * the root of its git repository, or its absolute path */

typedef enum {
    PATH_COPY_NAME,
    PATH_COPY_RELATIVE,
    PATH_COPY_ABSOLUTE,
} PathCopyKind;

/* Whether path is in a git repository (Copy Git Relative Path is shown) */
gboolean path_copy_has_relative(const gchar* path);

/* Copy that form of path to the clipboard, with a notice */
void path_copy(AppData* app, const gchar* path, PathCopyKind kind);

#endif /* PATH_COPY_H */
