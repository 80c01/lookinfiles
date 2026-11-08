#ifndef MAIL_MESSAGE_H
#define MAIL_MESSAGE_H

/* Mail messages (RFC 5322 / MIME) for the preview (docs/previews.md):
 * headers (folded lines, RFC 2047 encoded words in any charset), the MIME
 * tree (multipart/mixed, alternative, related, nested message/rfc822),
 * base64 and quoted-printable bodies, RFC 2231 parameters (file names),
 * text converted to UTF-8 from the declared charset (Windows-1252 when it
 * is missing or wrong). Bounded: parts larger than max_part bytes are
 * listed, not decoded. Pure GLib, unit-tested. */
#include <glib.h>

typedef struct MailMessage MailMessage;
typedef struct MailPart MailPart;

struct MailPart {
    gchar* content_type;   /* lower case, "text/plain" by default */
    gchar* charset;        /* lower case or NULL */
    gchar* filename;       /* decoded (name / filename parameters) or NULL */
    gchar* content_id;     /* without < > or NULL */
    gboolean attachment;   /* Content-Disposition: attachment */
    gsize size;            /* decoded size (estimated when not decoded) */
    GBytes* data;          /* decoded body; NULL for multiparts and parts too large */
    GPtrArray* children;   /* MailPart* of a multipart */
    MailMessage* message;  /* message/rfc822 */
};

struct MailMessage {
    gchar* from;
    gchar* to;
    gchar* cc;
    gchar* subject;
    gchar* date_raw;
    gint64 date;           /* Unix time, -1 when missing or unreadable */
    gchar* message_id;
    gboolean encrypted;    /* multipart/encrypted or S/MIME enveloped */
    MailPart* root;
    GPtrArray* headers;    /* "Name: value" unfolded, raw */
};

/* Parse a message (emlx: the leading byte count line is skipped). Never
 * fails: a message without headers has an empty header set and a text
 * body. */
MailMessage* mail_message_parse(const gchar* data, gsize len, gsize max_part);
void mail_message_free(MailMessage* msg);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MailMessage, mail_message_free)

/* First header named so (case-insensitive), raw (not decoded), or NULL */
const gchar* mail_message_header(const MailMessage* msg, const gchar* name);

/* A header value with its RFC 2047 encoded words decoded, as UTF-8 */
gchar* mail_decode_header(const gchar* raw);

/* RFC 5322 date as Unix time, -1 when unreadable */
gint64 mail_parse_date(const gchar* text);

/* The body part shown: the first text/plain (or text/html when html is
 * TRUE) that is not an attachment, searching alternatives and related
 * parts; NULL when there is none */
const MailPart* mail_message_body(const MailMessage* msg, gboolean html);

/* Parts to list as attachments (MailPart*, owned by the message): parts
 * with a file name or an attachment disposition, not the body */
GPtrArray* mail_message_attachments(const MailMessage* msg);

/* The part with this Content-ID (inline images of HTML bodies), or NULL */
const MailPart* mail_message_find_cid(const MailMessage* msg, const gchar* cid);

/* Text of a part as UTF-8 (its charset, Windows-1252 when missing or
 * wrong); NULL when the part has no data */
gchar* mail_part_text(const MailPart* part);

/* Bytes in a charset as UTF-8 (the same fallbacks) */
gchar* mail_to_utf8(const gchar* data, gsize len, const gchar* charset);

#endif /* MAIL_MESSAGE_H */
