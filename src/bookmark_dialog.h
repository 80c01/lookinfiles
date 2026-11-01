#ifndef BOOKMARK_DIALOG_H
#define BOOKMARK_DIALOG_H

#include "app_types.h"

/* Dialogs of the bookmarks view (bookmarks.c) and the target helpers they
 * share with it. */

/* "~/..." for paths under the home directory, unchanged otherwise */
gchar* bookmark_display_location(const gchar* target);
/* User input as a canonical absolute path (relative to base_dir), a URI
 * kept as is, or NULL when blank */
gchar* bookmark_normalize_target(const gchar* text, const gchar* base_dir);
/* Name of a new bookmark: the file name, or the host of a URL */
gchar* bookmark_default_name(const gchar* target);

/* Message / confirmation dialog (cancel_label may be NULL); TRUE when
 * ok_label was chosen */
gboolean bookmark_message(AppData* app, const gchar* title, const gchar* message,
                          const gchar* ok_label, const gchar* cancel_label);

typedef struct {
    const gchar* title;
    const gchar* ok_label;
    gboolean is_folder;           /* folder: name, icon, parent folder */
    gboolean allow_remove;        /* adds "Remove Bookmark" */
    const gchar* name;            /* initial values, may be NULL */
    const gchar* target;
    const gchar* icon;            /* folders: one of NOTEBOOK_ICONS */
    const gchar* const* folders;  /* parent folder choices, NULL-terminated */
    guint folder;                 /* initially selected index of folders */
} BookmarkDialogSpec;

/* Add / edit dialog for a bookmark or a folder. Returns APP_RESPONSE_OK
 * (outputs filled: out_target NULL for a folder, out_icon NULL for a link),
 * APP_RESPONSE_REJECT (remove requested) or another response for cancel. */
gint bookmark_item_dialog(AppData* app, const BookmarkDialogSpec* spec,
                          gchar** out_name, gchar** out_target, const gchar** out_icon,
                          guint* out_folder);

#endif /* BOOKMARK_DIALOG_H */
