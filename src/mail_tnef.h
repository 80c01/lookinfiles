#ifndef MAIL_TNEF_H
#define MAIL_TNEF_H

/* TNEF (Transport Neutral Encapsulation Format, docs/previews.md): the
 * "winmail.dat" attachment Outlook adds to mails. Signature 78 9F 3E 22,
 * then attributes (level, id, length, data, checksum): message attributes
 * (subject, sender, date, body) and attachments, each opened by
 * attAttachRenddata and described by its title, data and MAPI properties
 * (long file name, MIME type). The MAPI property blocks give the HTML or
 * compressed RTF body. Read into a MailMessage, so it is shown like an
 * .eml file. Pure GLib, unit-tested. */
#include <glib.h>

#include "mail_message.h"

#define MAIL_TNEF_SIGNATURE 0x223E9F78u

gboolean mail_tnef_looks_like(const guint8* data, gsize len);

/* The message (never NULL for a TNEF signature: a truncated stream gives
 * what was read before the damage); NULL with error otherwise. Attachments
 * larger than max_part are listed with their size, not kept. */
MailMessage* mail_tnef_read(const guint8* data, gsize len, gsize max_part, GError** error);

#endif /* MAIL_TNEF_H */
