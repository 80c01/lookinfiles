/* Previews of mail messages (see mail_view.h) */
#include "mail/mail_view.h"
#include "core/i18n.h"
#include <string.h>

#define MAX_FORWARD_DEPTH 3
#define MAX_BODY_CHARS (256 * 1024)

gchar* mail_view_format_date(gint64 t) {
    if (t < 0) return NULL;
    GDateTime* dt = g_date_time_new_from_unix_local(t);
    if (dt == NULL) return NULL;
    gchar* s = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    g_date_time_unref(dt);
    return s;
}

static gchar* attachment_label(const MailPart* p) {
    gchar* size = g_format_size(p->size);
    const gchar* name = p->filename != NULL ? p->filename
                        : p->message != NULL && p->message->subject != NULL ? p->message->subject
                                                                            : _("(unnamed)");
    gchar* label = g_strdup_printf("%s · %s · %s", name, p->content_type, size);
    g_free(size);
    return label;
}

static void header_fields(PreviewDoc* doc, const MailMessage* msg) {
    if (msg->from != NULL) preview_doc_field(doc, _("From"), msg->from);
    if (msg->to != NULL) preview_doc_field(doc, _("To"), msg->to);
    if (msg->cc != NULL) preview_doc_field(doc, _("Cc"), msg->cc);
    gchar* date = mail_view_format_date(msg->date);
    if (date != NULL) preview_doc_field(doc, _("Date"), date);
    else if (msg->date_raw != NULL) preview_doc_field(doc, _("Date"), msg->date_raw);
    g_free(date);
}

/* Plain text body: quoted lines and the signature dimmed */
static void plain_body(PreviewDoc* doc, const gchar* text) {
    gboolean signature = FALSE;
    gsize total = 0;
    gchar** lines = g_strsplit(text, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = lines[i];
        gsize n = strlen(line);
        if (n > 0 && line[n - 1] == '\r') line[--n] = '\0';
        if (strcmp(line, "-- ") == 0 || strcmp(line, "--") == 0) signature = TRUE;
        const gchar* t = line;
        while (*t == ' ') t++;
        gboolean quoted = *t == '>';
        if (signature || quoted) preview_doc_append(doc, PREVIEW_STYLE_DIM, line);
        else preview_doc_append_len(doc, line, -1);
        preview_doc_append_len(doc, "\n", 1);
        total += n + 1;
        if (total > MAX_BODY_CHARS) {
            preview_doc_note(doc, _("The message continues; the preview stops here."));
            break;
        }
    }
    g_strfreev(lines);
}

static void attachments_section(PreviewDoc* doc, GPtrArray* parts) {
    if (parts->len == 0) return;
    preview_doc_section(doc, parts->len == 1 ? _("Attachment") : _("Attachments"));
    for (guint i = 0; i < parts->len; i++) {
        gchar* label = attachment_label(g_ptr_array_index(parts, i));
        preview_doc_item(doc, label);
        g_free(label);
    }
}

static void text_message(PreviewDoc* doc, const MailMessage* msg, gint depth);

static void forwarded(PreviewDoc* doc, GPtrArray* parts, gint depth) {
    for (guint i = 0; i < parts->len && depth < MAX_FORWARD_DEPTH; i++) {
        const MailPart* p = g_ptr_array_index(parts, i);
        if (p->message == NULL) continue;
        preview_doc_section(doc, _("Forwarded message"));
        if (p->message->subject != NULL) preview_doc_field(doc, _("Subject"), p->message->subject);
        text_message(doc, p->message, depth + 1);
    }
}

/* Header fields (a nested message also has its subject), body,
 * attachments, forwarded messages */
static void text_message(PreviewDoc* doc, const MailMessage* msg, gint depth) {
    header_fields(doc, msg);
    preview_doc_append_len(doc, "\n", 1);
    if (msg->encrypted) {
        preview_doc_note(doc, _("Encrypted message: the body needs the recipient's key."));
        return;
    }
    const MailPart* plain = mail_message_body(msg, FALSE);
    const MailPart* html = mail_message_body(msg, TRUE);
    gchar* text = mail_part_text(plain);
    if (text == NULL && html != NULL) {
        /* A nested HTML-only message: its text without the tags */
        gchar* h = mail_part_text(html);
        GString* s = g_string_new(NULL);
        gboolean tag = FALSE;
        for (const gchar* c = h; c != NULL && *c != '\0'; c++) {
            if (*c == '<') tag = TRUE;
            else if (*c == '>') tag = FALSE;
            else if (!tag) g_string_append_c(s, *c);
        }
        g_free(h);
        text = g_string_free(s, FALSE);
    }
    if (text != NULL) plain_body(doc, text);
    g_free(text);
    GPtrArray* parts = mail_message_attachments(msg);
    attachments_section(doc, parts);
    forwarded(doc, parts, depth);
    g_ptr_array_unref(parts);
}

/* --- HTML bodies --- */

static void append_escaped(GString* out, const gchar* s) {
    gchar* e = g_markup_escape_text(s != NULL ? s : "", -1);
    g_string_append(out, e);
    g_free(e);
}

/* The body's inner HTML, cid: images replaced by data: URIs */
static gchar* html_inner(const MailMessage* msg, const gchar* html) {
    const gchar* start = html;
    const gchar* body = g_strstr_len(html, -1, "<body");
    if (body == NULL) body = g_strstr_len(html, -1, "<BODY");
    if (body != NULL) {
        const gchar* gt = strchr(body, '>');
        if (gt != NULL) start = gt + 1;
    }
    gchar* copy = g_strdup(start);
    gchar* close = strstr(copy, "</body>");
    if (close == NULL) close = strstr(copy, "</BODY>");
    if (close != NULL) *close = '\0';
    GString* out = g_string_new(NULL);
    const gchar* p = copy;
    const gchar* hit;
    while ((hit = strstr(p, "cid:")) != NULL) {
        g_string_append_len(out, p, (gssize)(hit - p));
        const gchar* e = hit + 4;
        while (*e != '\0' && *e != '"' && *e != '\'' && *e != ' ' && *e != '>' && *e != ')') e++;
        gchar* cid = g_strndup(hit + 4, (gsize)(e - hit - 4));
        const MailPart* part = mail_message_find_cid(msg, cid);
        if (part != NULL && part->data != NULL && g_str_has_prefix(part->content_type, "image/")) {
            gsize n = 0;
            const guchar* d = g_bytes_get_data(part->data, &n);
            gchar* b64 = g_base64_encode(d, n);
            g_string_append_printf(out, "data:%s;base64,%s", part->content_type, b64);
            g_free(b64);
        } else {
            g_string_append_len(out, hit, (gssize)(e - hit));
        }
        g_free(cid);
        p = e;
    }
    g_string_append(out, p);
    g_free(copy);
    return g_string_free(out, FALSE);
}

static PreviewDoc* html_message(const MailMessage* msg, const MailPart* html, const gchar* subtitle) {
    GString* out = g_string_new("<html><head><title>");
    append_escaped(out, msg->subject != NULL ? msg->subject : _("(no subject)"));
    g_string_append(out, "</title>");
    if (msg->from != NULL) {
        g_string_append(out, "<meta name=\"author\" content=\"");
        append_escaped(out, msg->from);
        g_string_append(out, "\">");
    }
    if (subtitle != NULL) {
        g_string_append(out, "<meta name=\"description\" content=\"");
        append_escaped(out, subtitle);
        g_string_append(out, "\">");
    }
    g_string_append(out, "</head><body>\n<p>");
    static const gchar* const LABELS[] = {N_("From"), N_("To"), N_("Cc"), N_("Date")};
    gchar* date = mail_view_format_date(msg->date);
    const gchar* values[] = {msg->from, msg->to, msg->cc, date != NULL ? date : msg->date_raw};
    for (gsize i = 0; i < G_N_ELEMENTS(LABELS); i++) {
        if (values[i] == NULL) continue;
        g_string_append_printf(out, "<b>%s:</b> ", _(LABELS[i]));
        append_escaped(out, values[i]);
        g_string_append(out, "<br>\n");
    }
    g_free(date);
    g_string_append(out, "</p>\n<hr>\n");
    gchar* text = mail_part_text(html);
    gchar* inner = html_inner(msg, text != NULL ? text : "");
    if (strlen(inner) > MAX_BODY_CHARS * 4) inner[MAX_BODY_CHARS * 4] = '\0';
    g_string_append(out, inner);
    g_free(inner);
    g_free(text);
    GPtrArray* parts = mail_message_attachments(msg);
    if (parts->len > 0) {
        g_string_append_printf(out, "\n<hr>\n<p><b>%s</b></p>\n<ul>\n", _("Attachments"));
        for (guint i = 0; i < parts->len; i++) {
            gchar* label = attachment_label(g_ptr_array_index(parts, i));
            g_string_append(out, "<li>");
            append_escaped(out, label);
            g_string_append(out, "</li>\n");
            g_free(label);
        }
        g_string_append(out, "</ul>\n");
    }
    g_ptr_array_unref(parts);
    g_string_append(out, "</body></html>");
    return preview_doc_new_source(PREVIEW_DOC_HTML, g_string_free(out, FALSE));
}

PreviewDoc* mail_view_message(const MailMessage* msg, const gchar* subtitle) {
    const MailPart* plain = mail_message_body(msg, FALSE);
    const MailPart* html = mail_message_body(msg, TRUE);
    if (plain == NULL && html != NULL && !msg->encrypted) return html_message(msg, html, subtitle);
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE; /* aligned header fields, plain text as mail clients show it */
    preview_doc_title(doc, msg->subject != NULL && msg->subject[0] != '\0' ? msg->subject : _("(no subject)"), subtitle);
    text_message(doc, msg, 0);
    return doc;
}
