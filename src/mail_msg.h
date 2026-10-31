#ifndef MAIL_MSG_H
#define MAIL_MSG_H

/* Outlook messages (.msg, docs/previews.md): OLE2 compound files whose MAPI
 * properties are streams "__substg1.0_<id><type>" (subject, sender,
 * recipients, plain / HTML / compressed RTF body, transport headers) and
 * the "__properties_version1.0" table (dates); attachments are the
 * "__attach_version1.0_#..." storages. Read into a MailMessage, so they are
 * shown like .eml files. Also the compressed RTF (LZFu) decoder. Pure GLib,
 * unit-tested. */
#include <glib.h>

#include "mail_message.h"
#include "ole_reader.h"

/* The message; NULL with error set when it has no message properties */
MailMessage* mail_msg_read(OleReader* ole, gsize max_part, GError** error);

/* Helpers shared with mail_tnef: a message with the given header block (or
 * NULL) and an empty multipart/mixed root to add parts to; a part (takes
 * data); the HTML part of a compressed RTF body, NULL when damaged */
MailMessage* mail_msg_message_new(const gchar* headers);
MailPart* mail_msg_part_new(const gchar* type, const gchar* charset, GBytes* data);
MailPart* mail_msg_rtf_part(const guint8* data, gsize len);
/* MIME type from a file name (application/octet-stream when unknown) */
gchar* mail_msg_guess_type(const gchar* name);

/* Compressed RTF ("LZFu", or "MELA" stored) to RTF; NULL when damaged */
GBytes* mail_msg_decompress_rtf(const guint8* data, gsize len);

#endif /* MAIL_MSG_H */
