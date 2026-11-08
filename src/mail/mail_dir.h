#ifndef MAIL_DIR_H
#define MAIL_DIR_H

/* Maildir and MH mail folders in the directory preview (docs/previews.md):
 * a folder holding cur/ and new/ (Maildir, with Maildir++ ".Folder"
 * subfolders) or numbered files with .mh_sequences (MH) is shown as a
 * table of its newest messages (date, from, subject, status from the
 * ":2,FRS" flags or the unseen sequence) instead of the plain listing.
 * Reads the headers of at most max_messages files. Blocking: worker
 * thread. Pure GLib, unit-tested. */
#include <gio/gio.h>

#include "preview/preview_doc.h"

typedef enum {
    MAIL_DIR_NONE = 0,
    MAIL_DIR_MAILDIR,
    MAIL_DIR_MH
} MailDirKind;

MailDirKind mail_dir_kind(const gchar* path);

/* The message table of a mail folder, NULL when path is not one */
PreviewDoc* mail_dir_preview(const gchar* path, const gchar* name, guint max_messages, GCancellable* cancellable);

#endif /* MAIL_DIR_H */
