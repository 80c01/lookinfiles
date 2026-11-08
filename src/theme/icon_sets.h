#ifndef ICON_SETS_H
#define ICON_SETS_H

/* Icon sets: alternative looks for the interface and file icons, chosen by a
 * theme (theme_store.h, "icons=<set>" in [Theme]).
 *
 * A set is a folder of SVG files named lookin-set-<icon>.svg (drawn as they
 * are) or lookin-set-<icon>-symbolic.svg (drawn in the text colour), where
 * <icon> is the name of the icon it replaces: the files of icons/scalable/actions
 * without the "lookin-" prefix and the "-symbolic" suffix (edit-delete,
 * folder, ft-folder...). An icon the set does not have keeps the default look.
 *
 * Sets are built into the binary (icons/sets/<set>/) or written by the user
 * in ~/.config/lookinfiles/icons/<set>/; a user set replaces a built-in one
 * of the same name. Everything showing an icon follows a change. */
#include <glib.h>

/* Use the set named id (NULL or "default": the default icons). An unknown
 * set leaves the default icons. Does nothing when already in use. */
void icon_sets_use(const gchar* id);

/* Name of the set in use, NULL for the default icons */
const gchar* icon_sets_current(void);

/* Names of the available sets (built-in, then user sets, sorted, no
 * duplicates), as a NULL-terminated vector. Free with g_strfreev(). */
gchar** icon_sets_list(void);

/* ~/.config/lookinfiles/icons. Caller frees. */
gchar* icon_sets_user_dir(void);

#endif /* ICON_SETS_H */
