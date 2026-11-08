#ifndef MAIL_BOX_H
#define MAIL_BOX_H

/* mbox mailboxes (docs/previews.md): Thunderbird / Mozilla Inbox, Sent...
 * (no extension), .mbox, .mbx, /var/mail/<user>. Messages start at "From "
 * lines after a blank line; mboxo / mboxrd ">From " lines are unquoted,
 * mboxcl Content-Length headers are honoured. Mailboxes can be gigabytes:
 * only the beginning and the end of a large file are scanned. Pure GLib,
 * unit-tested. */
#include <glib.h>

#include "mail/mail_message.h"

/* Files up to this size are scanned whole (the message count is exact) */
#define MAIL_BOX_FULL_SCAN (16 * 1024 * 1024)
/* Larger ones: this much of their beginning and of their end */
#define MAIL_BOX_PART_SCAN (4 * 1024 * 1024)

typedef struct {
    gsize offset;        /* of the "From " line */
    gsize length;        /* to the next message */
    gchar* from;         /* decoded */
    gchar* subject;
    gint64 date;         /* -1 when unknown */
    gboolean attachments;/* multipart/mixed: probably */
} MailBoxEntry;

typedef struct {
    GPtrArray* entries;  /* MailBoxEntry*, in file order */
    gboolean complete;   /* the whole file was scanned */
    guint head_count;    /* when not complete: entries from the beginning, the rest from the end */
} MailBox;

/* TRUE when text starts like an mbox: a "From " line, then headers */
gboolean mail_box_looks_like(const gchar* data, gsize len);

/* Scan data (the whole file, mapped). NULL when it is not an mbox. */
MailBox* mail_box_scan(const gchar* data, gsize len);
void mail_box_free(MailBox* box);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MailBox, mail_box_free)

/* Message i parsed (">From " lines unquoted) */
MailMessage* mail_box_message(const MailBox* box, guint i, const gchar* data, gsize len, gsize max_part);

#endif /* MAIL_BOX_H */
