#ifndef MAIL_STORE_H
#define MAIL_STORE_H

/* Mail stores (TASK.md phase 6): Outlook data files (.pst) and offline
 * stores (.ost), magic "!BDN", and Outlook Express folders (.dbx), magic
 * CF AD 12 FE. Only their header is read: a card with the format, ANSI or
 * Unicode, the size recorded in the header (a shorter file is truncated),
 * the encryption, and the message count of a .dbx folder. Their folders and
 * messages would need libpff, not used. Pure GLib, unit-tested. */
#include <glib.h>

#include "preview_doc.h"

/* Bytes of the file start to pass in */
#define MAIL_STORE_HEADER_BYTES 1024

gboolean mail_store_looks_like(const guint8* head, gsize len);

/* The header card; NULL when head is not a store */
PreviewDoc* mail_store_preview(const guint8* head, gsize len, const gchar* name, gint64 file_size);

#endif /* MAIL_STORE_H */
