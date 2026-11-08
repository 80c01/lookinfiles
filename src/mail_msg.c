/* Outlook messages (see mail_msg.h) */
#include "mail_msg.h"
#include "core/i18n.h"
#include "rtf.h"
#include <gio/gio.h>
#include <string.h>

#define PROP_MAX (4 * 1024 * 1024)

#define PT_STRING8 0x001E
#define PT_UNICODE 0x001F
#define PT_BINARY 0x0102
#define PT_SYSTIME 0x0040

static guint32 u32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

/* ------------------------------------------------------------------------- */
/* Compressed RTF                                                            */
/* ------------------------------------------------------------------------- */

static const gchar RTF_PREBUF[] =
    "{\\rtf1\\ansi\\mac\\deff0\\deftab720{\\fonttbl;}{\\f0\\fnil \\froman \\fswiss \\fmodern \\fscript "
    "\\fdecor MS Sans SerifSymbolArialTimes New RomanCourier{\\colortbl\\red0\\green0\\blue0\r\n\\par "
    "\\pard\\plain\\f0\\fs20\\b\\i\\u\\tab\\tx";

GBytes* mail_msg_decompress_rtf(const guint8* data, gsize len) {
    if (data == NULL || len < 16) return NULL;
    guint32 comp_size = u32(data), raw_size = u32(data + 4), type = u32(data + 8);
    if (type == 0x414C454D) {
        /* "MELA": stored */
        gsize n = MIN((gsize)raw_size, len - 16);
        return g_bytes_new(data + 16, n);
    }
    if (type != 0x75465A4C || raw_size > 64 * 1024 * 1024) return NULL;
    gsize end = MIN(len, (gsize)comp_size + 4);
    guint8 dict[4096];
    gsize pre = sizeof RTF_PREBUF - 1;
    memset(dict, 0, sizeof dict);
    memcpy(dict, RTF_PREBUF, pre);
    guint write = (guint)pre;
    GByteArray* out = g_byte_array_sized_new(raw_size);
    gsize p = 16;
    while (p < end && out->len < raw_size) {
        guint8 control = data[p++];
        for (gint bit = 0; bit < 8 && p < end && out->len < raw_size; bit++) {
            if (control & (1 << bit)) {
                if (p + 2 > end) break;
                guint ref = ((guint)data[p] << 8) | data[p + 1];
                p += 2;
                guint offset = ref >> 4, length = (ref & 0xF) + 2;
                if (offset == write) {
                    /* End marker */
                    p = end;
                    break;
                }
                for (guint i = 0; i < length && out->len < raw_size; i++) {
                    guint8 c = dict[(offset + i) % 4096];
                    g_byte_array_append(out, &c, 1);
                    dict[write] = c;
                    write = (write + 1) % 4096;
                }
            } else {
                guint8 c = data[p++];
                g_byte_array_append(out, &c, 1);
                dict[write] = c;
                write = (write + 1) % 4096;
            }
        }
    }
    return g_byte_array_free_to_bytes(out);
}

/* ------------------------------------------------------------------------- */
/* Properties                                                                */
/* ------------------------------------------------------------------------- */

/* A property stream of a storage (prefix: "" or "__attach_version1.0_#00000000/") */
static GBytes* prop(OleReader* ole, const gchar* prefix, guint16 id, guint16 type) {
    gchar* path = g_strdup_printf("%s__substg1.0_%04X%04X", prefix, id, type);
    GBytes* b = ole_reader_read(ole, path, PROP_MAX, NULL);
    g_free(path);
    return b;
}

/* A string property, Unicode first, then 8-bit (Windows-1252) */
static gchar* prop_string(OleReader* ole, const gchar* prefix, guint16 id) {
    GBytes* b = prop(ole, prefix, id, PT_UNICODE);
    gchar* s = NULL;
    if (b != NULL) {
        gsize n = 0;
        const guint8* d = g_bytes_get_data(b, &n);
        glong units = (glong)(n / 2);
        gunichar2* w = g_new(gunichar2, units + 1);
        for (glong i = 0; i < units; i++) w[i] = (gunichar2)(d[2 * i] | (d[2 * i + 1] << 8));
        while (units > 0 && w[units - 1] == 0) units--;
        s = g_utf16_to_utf8(w, units, NULL, NULL, NULL);
        g_free(w);
        g_bytes_unref(b);
    } else if ((b = prop(ole, prefix, id, PT_STRING8)) != NULL) {
        gsize n = 0;
        const gchar* d = g_bytes_get_data(b, &n);
        while (n > 0 && d[n - 1] == '\0') n--;
        s = mail_to_utf8(d, n, NULL);
        g_bytes_unref(b);
    }
    if (s != NULL && g_strstrip(s)[0] == '\0') {
        g_free(s);
        s = NULL;
    }
    return s;
}

/* A PT_SYSTIME of the fixed property table, as Unix time; -1 when absent */
static gint64 prop_time(OleReader* ole, const gchar* prefix, guint16 id, gsize header) {
    gchar* path = g_strdup_printf("%s__properties_version1.0", prefix);
    GBytes* b = ole_reader_read(ole, path, PROP_MAX, NULL);
    g_free(path);
    if (b == NULL) return -1;
    gsize n = 0;
    const guint8* d = g_bytes_get_data(b, &n);
    gint64 t = -1;
    for (gsize p = header; p + 16 <= n; p += 16) {
        guint32 tag = u32(d + p);
        if ((tag & 0xFFFF) == PT_SYSTIME && (tag >> 16) == id) {
            guint64 ft = (guint64)u32(d + p + 8) | ((guint64)u32(d + p + 12) << 32);
            if (ft != 0) t = (gint64)(ft / 10000000ULL) - 11644473600LL;
            break;
        }
    }
    g_bytes_unref(b);
    return t;
}

MailPart* mail_msg_part_new(const gchar* type, const gchar* charset, GBytes* data) {
    MailPart* p = g_new0(MailPart, 1);
    p->content_type = g_strdup(type);
    p->charset = g_strdup(charset);
    p->data = data;
    p->size = data != NULL ? g_bytes_get_size(data) : 0;
    return p;
}

/* MailPart free for parts built here: mail_message_free() frees the tree */
static void part_free_shallow(gpointer p) {
    MailPart* part = p;
    if (part == NULL) return;
    g_free(part->content_type);
    g_free(part->charset);
    g_free(part->filename);
    g_free(part->content_id);
    if (part->data != NULL) g_bytes_unref(part->data);
    if (part->children != NULL) g_ptr_array_unref(part->children);
    mail_message_free(part->message);
    g_free(part);
}

MailMessage* mail_msg_message_new(const gchar* headers) {
    MailMessage* msg = mail_message_parse(headers != NULL ? headers : "", headers != NULL ? strlen(headers) : 0, 0);
    /* The parsed header block has a text body: replaced by the caller's parts */
    part_free_shallow(msg->root);
    msg->root = mail_msg_part_new("multipart/mixed", NULL, NULL);
    msg->root->children = g_ptr_array_new_with_free_func(part_free_shallow);
    return msg;
}

MailPart* mail_msg_rtf_part(const guint8* data, gsize len) {
    GBytes* raw = mail_msg_decompress_rtf(data, len);
    if (raw == NULL) return NULL;
    gsize rn = 0;
    const gchar* r = g_bytes_get_data(raw, &rn);
    gchar* h = rtf_to_html(r, rn, 256 * 1024, NULL);
    g_bytes_unref(raw);
    return h != NULL ? mail_msg_part_new("text/html", "utf-8", g_bytes_new_take(h, strlen(h))) : NULL;
}

gchar* mail_msg_guess_type(const gchar* name) {
    gboolean uncertain = FALSE;
    gchar* ct = g_content_type_guess(name, NULL, 0, &uncertain);
    gchar* mime = ct != NULL ? g_content_type_get_mime_type(ct) : NULL;
    g_free(ct);
    return mime != NULL ? mime : g_strdup("application/octet-stream");
}

MailMessage* mail_msg_read(OleReader* ole, gsize max_part, GError** error) {
    gchar* subject = prop_string(ole, "", 0x0037);
    gchar* klass = prop_string(ole, "", 0x001A);
    GBytes* body = prop(ole, "", 0x1000, PT_UNICODE);
    if (body == NULL) body = prop(ole, "", 0x1000, PT_STRING8);
    if (subject == NULL && klass == NULL && body == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("no message properties"));
        return NULL;
    }
    /* The transport headers, when the message came by SMTP: a whole header block */
    gchar* headers = prop_string(ole, "", 0x007D);
    MailMessage* msg = mail_msg_message_new(headers);
    g_free(headers);
    if (subject != NULL) {
        g_free(msg->subject);
        msg->subject = subject;
    }
    gchar* sender = prop_string(ole, "", 0x0C1A);
    gchar* email = prop_string(ole, "", 0x5D01);
    if (email == NULL) email = prop_string(ole, "", 0x0C1F);
    if (email != NULL && strchr(email, '@') == NULL) g_clear_pointer(&email, g_free); /* Exchange DN */
    if (sender != NULL || email != NULL) {
        g_free(msg->from);
        msg->from = sender != NULL && email != NULL ? g_strdup_printf("%s <%s>", sender, email)
                                                    : g_strdup(sender != NULL ? sender : email);
    }
    g_free(sender);
    g_free(email);
    gchar* to = prop_string(ole, "", 0x0E04);
    if (to != NULL) {
        g_free(msg->to);
        msg->to = to;
    }
    gchar* cc = prop_string(ole, "", 0x0E03);
    if (cc != NULL) {
        g_free(msg->cc);
        msg->cc = cc;
    }
    gint64 t = prop_time(ole, "", 0x0039, 32);
    if (t < 0) t = prop_time(ole, "", 0x0E06, 32);
    if (t >= 0) msg->date = t;
    msg->encrypted = klass != NULL && g_str_has_prefix(klass, "IPM.Note.SMIME") && strstr(klass, "Signed") == NULL;
    g_free(klass);

    /* Body: plain text, else HTML, else the compressed RTF */
    if (body != NULL) {
        GBytes* text;
        gsize n = 0;
        const guint8* d = g_bytes_get_data(body, &n);
        if (ole_reader_has(ole, "__substg1.0_1000001F")) {
            /* UTF-16 to UTF-8 */
            glong units = (glong)(n / 2);
            gunichar2* w = g_new(gunichar2, units + 1);
            for (glong i = 0; i < units; i++) w[i] = (gunichar2)(d[2 * i] | (d[2 * i + 1] << 8));
            while (units > 0 && w[units - 1] == 0) units--;
            gchar* s = g_utf16_to_utf8(w, units, NULL, NULL, NULL);
            g_free(w);
            text = s != NULL ? g_bytes_new_take(s, strlen(s)) : g_bytes_new("", 0);
            g_ptr_array_add(msg->root->children, mail_msg_part_new("text/plain", "utf-8", text));
        } else {
            g_ptr_array_add(msg->root->children, mail_msg_part_new("text/plain", "windows-1252", g_bytes_ref(body)));
        }
        g_bytes_unref(body);
    } else {
        GBytes* html = prop(ole, "", 0x1013, PT_BINARY);
        if (html == NULL) html = prop(ole, "", 0x1013, PT_UNICODE);
        GBytes* rtf = html == NULL ? prop(ole, "", 0x1009, PT_BINARY) : NULL;
        if (html != NULL) {
            g_ptr_array_add(msg->root->children, mail_msg_part_new("text/html", NULL, html));
        } else if (rtf != NULL) {
            gsize n = 0;
            const guint8* d = g_bytes_get_data(rtf, &n);
            MailPart* part = mail_msg_rtf_part(d, n);
            if (part != NULL) g_ptr_array_add(msg->root->children, part);
            g_bytes_unref(rtf);
        }
    }

    /* Attachments */
    for (guint i = 0; i < ole_reader_get_n_entries(ole); i++) {
        OleEntryInfo info;
        if (!ole_reader_get_info(ole, i, &info) || info.depth != 0 || info.type != OLE_ENTRY_STORAGE ||
            !g_str_has_prefix(info.name, "__attach_version1.0_#")) {
            continue;
        }
        gchar* prefix = g_strdup_printf("%s/", info.name);
        gchar* name = prop_string(ole, prefix, 0x3707);
        if (name == NULL) name = prop_string(ole, prefix, 0x3704);
        if (name == NULL) name = prop_string(ole, prefix, 0x3001);
        gchar* mime = prop_string(ole, prefix, 0x370E);
        gchar* data_path = g_strdup_printf("%s__substg1.0_37010102", prefix);
        gint idx = ole_reader_find(ole, data_path);
        OleEntryInfo di = {0};
        if (idx >= 0) ole_reader_get_info(ole, (guint)idx, &di);
        GBytes* data = idx >= 0 && di.size <= max_part ? ole_reader_read_entry(ole, (guint)idx, max_part, NULL) : NULL;
        MailPart* part = mail_msg_part_new(mime != NULL ? mime : NULL, NULL, data);
        if (part->content_type == NULL) part->content_type = mail_msg_guess_type(name != NULL ? name : "file");
        part->filename = name != NULL ? name : g_strdup(_("(attachment)"));
        part->attachment = TRUE;
        if (data == NULL) part->size = (gsize)di.size;
        g_ptr_array_add(msg->root->children, part);
        g_free(mime);
        g_free(data_path);
        g_free(prefix);
    }
    return msg;
}
