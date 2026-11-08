#ifndef BOOKMARK_ICONS_H
#define BOOKMARK_ICONS_H

#include "core/app_types.h"

/* Icons of the bookmarks view links. The icon depends on what the target is
 * (folder, file, missing): that is read in a worker thread (path_kind), never
 * on the UI thread, so a slow or hung mount does not freeze the panel. */

/* TRUE for a URL target ("https://..."), FALSE for a path */
gboolean bookmark_target_is_uri(const gchar* target);

/* Icon of a link row from its target's last known kind (no file access): a
 * file gets its VS Code file-type icon, other targets a glyph (folder, link,
 * missing, unknown) */
void bookmark_icon_apply(ExplorerRow* row);

/* Read the target's kind in the background; when it changes, relabel its rows
 * and preview the selected link to it again (its preview waits for the kind) */
void bookmark_icon_check(AppData* app, const gchar* target);

#endif /* BOOKMARK_ICONS_H */
