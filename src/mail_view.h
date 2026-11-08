#ifndef MAIL_VIEW_H
#define MAIL_VIEW_H

/* Previews of mail messages (docs/previews.md), whatever their source
 * (.eml, mbox entry, Maildir file, Outlook .msg): a header card (from, to,
 * cc, date in local time, subject), then the body: text/plain with quoted
 * lines and the signature dimmed, or text/html through the HTML renderer
 * (inline cid: images as data: URIs, no remote content fetched) when there
 * is no plain part; attachments listed (name, type, size) and forwarded
 * messages shown below. Pure GLib, unit-tested. */
#include <glib.h>

#include "mail_message.h"
#include "preview/preview_doc.h"

/* The largest part decoded for a preview */
#define MAIL_VIEW_MAX_PART (8 * 1024 * 1024)

/* Preview of msg; subtitle: kind and size ("Mail message · 4 kB"), or NULL */
PreviewDoc* mail_view_message(const MailMessage* msg, const gchar* subtitle);

/* "2026-10-30 09:16" in local time, NULL for -1 */
gchar* mail_view_format_date(gint64 t);

#endif /* MAIL_VIEW_H */
