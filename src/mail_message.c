/* Mail messages (see mail_message.h) */
#include "mail_message.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 10
#define MAX_PARTS 500

/* ------------------------------------------------------------------------- */
/* Charsets                                                                  */
/* ------------------------------------------------------------------------- */

gchar* mail_to_utf8(const gchar* data, gsize len, const gchar* charset) {
    gchar* out = NULL;
    gchar* cs = charset != NULL ? g_ascii_strdown(charset, -1) : NULL;
    if (cs != NULL) {
        g_strstrip(cs);
        /* Mail clients write Windows-1252 and call it ISO-8859-1 or ASCII */
        if (strcmp(cs, "iso-8859-1") == 0 || strcmp(cs, "latin1") == 0 || strcmp(cs, "us-ascii") == 0 ||
            strcmp(cs, "ascii") == 0 || strcmp(cs, "iso8859-1") == 0) {
            g_free(cs);
            cs = g_strdup("windows-1252");
        }
        if (strcmp(cs, "utf8") == 0) {
            g_free(cs);
            cs = g_strdup("utf-8");
        }
    }
    if (cs == NULL || strcmp(cs, "utf-8") == 0) {
        if (g_utf8_validate(data, (gssize)len, NULL)) out = g_strndup(data, len);
    } else {
        out = g_convert(data, (gssize)len, "UTF-8", cs, NULL, NULL, NULL);
    }
    if (out == NULL && g_utf8_validate(data, (gssize)len, NULL)) out = g_strndup(data, len);
    if (out == NULL) out = g_convert(data, (gssize)len, "UTF-8", "WINDOWS-1252", NULL, NULL, NULL);
    if (out == NULL) out = g_utf8_make_valid(data, (gssize)len);
    g_free(cs);
    return out;
}

/* ------------------------------------------------------------------------- */
/* Transfer encodings                                                        */
/* ------------------------------------------------------------------------- */

static gint hexval(gchar c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static GByteArray* decode_qp(const gchar* s, gsize len, gboolean header) {
    GByteArray* out = g_byte_array_sized_new((guint)len);
    for (gsize i = 0; i < len; i++) {
        guint8 c = (guint8)s[i];
        if (c == '=' && !header && i + 1 < len && (s[i + 1] == '\n' || s[i + 1] == '\r')) {
            /* Soft line break */
            i++;
            if (s[i] == '\r' && i + 1 < len && s[i + 1] == '\n') i++;
            continue;
        }
        if (c == '=' && i + 2 < len && hexval(s[i + 1]) >= 0 && hexval(s[i + 2]) >= 0) {
            c = (guint8)(hexval(s[i + 1]) * 16 + hexval(s[i + 2]));
            i += 2;
        } else if (c == '_' && header) {
            c = ' ';
        }
        g_byte_array_append(out, &c, 1);
    }
    return out;
}

static gint b64val(gchar c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

/* Lenient base64: other characters are skipped, padding ends nothing */
static GByteArray* decode_b64(const gchar* s, gsize len) {
    GByteArray* out = g_byte_array_sized_new((guint)(len * 3 / 4 + 3));
    guint32 acc = 0;
    gint bits = 0;
    for (gsize i = 0; i < len; i++) {
        gint v = b64val(s[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (guint32)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            guint8 b = (guint8)(acc >> bits);
            g_byte_array_append(out, &b, 1);
        }
    }
    return out;
}

/* ------------------------------------------------------------------------- */
/* Headers                                                                   */
/* ------------------------------------------------------------------------- */

gchar* mail_decode_header(const gchar* raw) {
    if (raw == NULL) return NULL;
    GString* out = g_string_new(NULL);
    const gchar* p = raw;
    gboolean last_encoded = FALSE;
    GString* pending_ws = g_string_new(NULL);
    while (*p != '\0') {
        if (p[0] == '=' && p[1] == '?') {
            const gchar* cs = p + 2;
            const gchar* q1 = strchr(cs, '?');
            if (q1 != NULL && (q1[1] == 'B' || q1[1] == 'b' || q1[1] == 'Q' || q1[1] == 'q') && q1[2] == '?') {
                const gchar* text = q1 + 3;
                const gchar* end = strstr(text, "?=");
                if (end != NULL) {
                    gchar* charset = g_strndup(cs, (gsize)(q1 - cs));
                    gchar* star = strchr(charset, '*'); /* RFC 2231 language */
                    if (star != NULL) *star = '\0';
                    GByteArray* bytes = (q1[1] == 'B' || q1[1] == 'b') ? decode_b64(text, (gsize)(end - text))
                                                                       : decode_qp(text, (gsize)(end - text), TRUE);
                    gchar* utf8 = mail_to_utf8((const gchar*)bytes->data, bytes->len, charset);
                    /* White space between encoded words is dropped */
                    if (!last_encoded) g_string_append(out, pending_ws->str);
                    g_string_truncate(pending_ws, 0);
                    g_string_append(out, utf8);
                    g_free(utf8);
                    g_byte_array_free(bytes, TRUE);
                    g_free(charset);
                    p = end + 2;
                    last_encoded = TRUE;
                    continue;
                }
            }
        }
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (pending_ws->len == 0) g_string_append_c(pending_ws, ' '); /* folding: one space */
            p++;
            continue;
        }
        g_string_append(out, pending_ws->str);
        g_string_truncate(pending_ws, 0);
        last_encoded = FALSE;
        g_string_append_c(out, *p);
        p++;
    }
    g_string_free(pending_ws, TRUE);
    /* Raw 8-bit headers (not allowed, common): UTF-8 or Windows-1252 */
    gchar* s = g_string_free(out, FALSE);
    if (!g_utf8_validate(s, -1, NULL)) {
        gchar* v = mail_to_utf8(s, strlen(s), NULL);
        g_free(s);
        s = v;
    }
    return g_strstrip(s);
}

static const gchar* const MONTHS[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                      "jul", "aug", "sep", "oct", "nov", "dec"};

gint64 mail_parse_date(const gchar* text) {
    if (text == NULL) return -1;
    gchar** tok = g_strsplit_set(text, " \t,", -1);
    gint day = -1, month = -1, year = -1, hh = -1, mm = -1, ss = 0;
    gint zone = 0;
    for (gint i = 0; tok[i] != NULL; i++) {
        const gchar* t = tok[i];
        if (t[0] == '\0') continue;
        if (strchr(t, ':') != NULL && hh < 0) {
            sscanf(t, "%d:%d:%d", &hh, &mm, &ss);
        } else if ((t[0] == '+' || t[0] == '-') && strlen(t) == 5 && g_ascii_isdigit(t[1])) {
            gint v = atoi(t + 1);
            zone = (v / 100 * 60 + v % 100) * (t[0] == '-' ? -1 : 1);
        } else if (g_ascii_isdigit(t[0])) {
            gint v = atoi(t);
            if (day < 0 && v >= 1 && v <= 31 && strlen(t) <= 2) day = v;
            else if (year < 0) year = v < 50 ? 2000 + v : v < 100 ? 1900 + v : v;
        } else if (g_ascii_strcasecmp(t, "EST") == 0 || g_ascii_strcasecmp(t, "CDT") == 0) {
            zone = -300;
        } else if (g_ascii_strcasecmp(t, "EDT") == 0) {
            zone = -240;
        } else if (g_ascii_strcasecmp(t, "CST") == 0 || g_ascii_strcasecmp(t, "MDT") == 0) {
            zone = -360;
        } else if (g_ascii_strcasecmp(t, "MST") == 0 || g_ascii_strcasecmp(t, "PDT") == 0) {
            zone = -420;
        } else if (g_ascii_strcasecmp(t, "PST") == 0) {
            zone = -480;
        } else if (month < 0) {
            for (gint m = 0; m < 12; m++) {
                if (g_ascii_strncasecmp(t, MONTHS[m], 3) == 0) month = m + 1;
            }
        }
    }
    g_strfreev(tok);
    if (day < 0 || month < 0 || year < 0 || hh < 0 || mm < 0) return -1;
    GDateTime* dt = g_date_time_new_utc(year, month, day, hh, mm, ss);
    if (dt == NULL) return -1;
    gint64 t = g_date_time_to_unix(dt) - (gint64)zone * 60;
    g_date_time_unref(dt);
    return t;
}

const gchar* mail_message_header(const MailMessage* msg, const gchar* name) {
    gsize n = strlen(name);
    for (guint i = 0; msg != NULL && i < msg->headers->len; i++) {
        const gchar* h = g_ptr_array_index(msg->headers, i);
        if (g_ascii_strncasecmp(h, name, n) == 0 && h[n] == ':') {
            const gchar* v = h + n + 1;
            while (*v == ' ' || *v == '\t') v++;
            return v;
        }
    }
    return NULL;
}

/* Header lines of [p, end) up to the blank line; *body: the body start */
static GPtrArray* read_headers(const gchar* p, const gchar* end, const gchar** body) {
    GPtrArray* headers = g_ptr_array_new_with_free_func(g_free);
    GString* cur = NULL;
    while (p < end) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        const gchar* le = nl != NULL ? nl : end;
        gsize ll = (gsize)(le - p);
        if (ll > 0 && p[ll - 1] == '\r') ll--;
        if (ll == 0) {
            p = nl != NULL ? nl + 1 : end;
            break;
        }
        if ((p[0] == ' ' || p[0] == '\t') && cur != NULL) {
            g_string_append_c(cur, ' ');
            g_string_append_len(cur, p, (gssize)ll);
        } else {
            const gchar* colon = memchr(p, ':', ll);
            gboolean header = colon != NULL && colon > p;
            for (const gchar* c = p; header && c < colon; c++) {
                if (*c <= ' ' || *c > '~') header = FALSE;
            }
            if (!header) {
                /* Not a header: the body starts here (no blank line) */
                break;
            }
            if (cur != NULL) g_ptr_array_add(headers, g_string_free(cur, FALSE));
            cur = g_string_new_len(p, (gssize)ll);
        }
        p = nl != NULL ? nl + 1 : end;
    }
    if (cur != NULL) g_ptr_array_add(headers, g_string_free(cur, FALSE));
    *body = p;
    return headers;
}

/* Value of a parameter of a structured header (Content-Type...), with
 * quotes, RFC 2231 encodings and continuations; NULL when absent */
static gchar* header_param(const gchar* value, const gchar* name) {
    if (value == NULL) return NULL;
    gsize n = strlen(name);
    GString* plain = NULL;
    GString* parts = g_string_new(NULL);
    gboolean encoded = FALSE;
    gchar* charset = NULL;
    const gchar* p = strchr(value, ';');
    while (p != NULL && *p != '\0') {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        const gchar* eq = strchr(p, '=');
        const gchar* semi = strchr(p, ';');
        if (eq == NULL || (semi != NULL && semi < eq)) {
            p = semi;
            continue;
        }
        gchar* key = g_strstrip(g_strndup(p, (gsize)(eq - p)));
        const gchar* v = eq + 1;
        while (*v == ' ') v++;
        gchar* val;
        const gchar* next;
        if (*v == '"') {
            GString* q = g_string_new(NULL);
            v++;
            while (*v != '\0' && *v != '"') {
                if (*v == '\\' && v[1] != '\0') v++;
                g_string_append_c(q, *v++);
            }
            val = g_string_free(q, FALSE);
            next = strchr(*v == '"' ? v + 1 : v, ';');
        } else {
            const gchar* e = strchr(v, ';');
            val = g_strstrip(g_strndup(v, e != NULL ? (gsize)(e - v) : strlen(v)));
            next = e;
        }
        if (g_ascii_strncasecmp(key, name, n) == 0) {
            const gchar* rest = key + n;
            if (rest[0] == '\0' && plain == NULL) {
                plain = g_string_new(val);
            } else if (rest[0] == '*') {
                /* name* (encoded), name*0, name*1*... (continuations) */
                gboolean enc = rest[strlen(rest) - 1] == '*';
                gboolean first = rest[1] == '\0' || (rest[1] == '0' && (rest[2] == '\0' || rest[2] == '*'));
                const gchar* text = val;
                if (enc && first) {
                    const gchar* q1 = strchr(val, '\'');
                    const gchar* q2 = q1 != NULL ? strchr(q1 + 1, '\'') : NULL;
                    if (q1 != NULL && q2 != NULL) {
                        g_free(charset);
                        charset = g_strndup(val, (gsize)(q1 - val));
                        text = q2 + 1;
                    }
                    encoded = TRUE;
                }
                if (enc) {
                    for (const gchar* c = text; *c != '\0'; c++) {
                        if (*c == '%' && hexval(c[1]) >= 0 && hexval(c[2]) >= 0) {
                            g_string_append_c(parts, (gchar)(hexval(c[1]) * 16 + hexval(c[2])));
                            c += 2;
                        } else {
                            g_string_append_c(parts, *c);
                        }
                    }
                } else {
                    g_string_append(parts, text);
                }
            }
        }
        g_free(key);
        g_free(val);
        p = next;
    }
    gchar* result = NULL;
    if (parts->len > 0) {
        result = encoded ? mail_to_utf8(parts->str, parts->len, charset) : g_strdup(parts->str);
    } else if (plain != NULL) {
        result = mail_decode_header(plain->str);
    }
    if (plain != NULL) g_string_free(plain, TRUE);
    g_string_free(parts, TRUE);
    g_free(charset);
    return result;
}

static gchar* main_value(const gchar* value) {
    if (value == NULL) return NULL;
    const gchar* semi = strchr(value, ';');
    gchar* v = g_strndup(value, semi != NULL ? (gsize)(semi - value) : strlen(value));
    gchar* low = g_ascii_strdown(g_strstrip(v), -1);
    g_free(v);
    return low;
}

/* ------------------------------------------------------------------------- */
/* MIME tree                                                                 */
/* ------------------------------------------------------------------------- */

static void part_free(gpointer p);
static MailMessage* parse_message(const gchar* data, gsize len, gsize max_part, gint depth, guint* count);

static const gchar* header_in(GPtrArray* headers, const gchar* name) {
    MailMessage tmp = {0};
    tmp.headers = headers;
    return mail_message_header(&tmp, name);
}

static MailPart* parse_part(GPtrArray* headers, const gchar* body, gsize body_len, gsize max_part, gint depth,
                            guint* count) {
    MailPart* part = g_new0(MailPart, 1);
    (*count)++;
    const gchar* ctype = header_in(headers, "Content-Type");
    part->content_type = ctype != NULL ? main_value(ctype) : g_strdup("text/plain");
    if (part->content_type[0] == '\0' || strchr(part->content_type, '/') == NULL) {
        g_free(part->content_type);
        part->content_type = g_strdup("text/plain");
    }
    gchar* charset = header_param(ctype, "charset");
    part->charset = charset != NULL ? g_ascii_strdown(charset, -1) : NULL;
    g_free(charset);
    const gchar* disp = header_in(headers, "Content-Disposition");
    gchar* disp_main = main_value(disp);
    part->attachment = disp_main != NULL && strcmp(disp_main, "attachment") == 0;
    g_free(disp_main);
    part->filename = header_param(disp, "filename");
    if (part->filename == NULL) part->filename = header_param(ctype, "name");
    const gchar* cid = header_in(headers, "Content-ID");
    if (cid != NULL) {
        gchar* c = g_strstrip(g_strdup(cid));
        gsize n = strlen(c);
        if (n >= 2 && c[0] == '<' && c[n - 1] == '>') {
            memmove(c, c + 1, n - 2);
            c[n - 2] = '\0';
        }
        part->content_id = c;
    }
    if (g_str_has_prefix(part->content_type, "multipart/") && depth < MAX_DEPTH) {
        gchar* boundary = header_param(ctype, "boundary");
        part->children = g_ptr_array_new_with_free_func(part_free);
        if (boundary != NULL && boundary[0] != '\0') {
            gchar* delim = g_strconcat("--", boundary, NULL);
            gsize dl = strlen(delim);
            const gchar* p = body;
            const gchar* end = body + body_len;
            const gchar* part_start = NULL;
            while (p < end && *count < MAX_PARTS) {
                const gchar* nl = memchr(p, '\n', (gsize)(end - p));
                const gchar* le = nl != NULL ? nl : end;
                gboolean is_delim = (gsize)(le - p) >= dl && memcmp(p, delim, dl) == 0;
                if (is_delim) {
                    if (part_start != NULL) {
                        /* The line break before the delimiter belongs to it */
                        const gchar* pe = p;
                        if (pe > part_start && pe[-1] == '\n') pe--;
                        if (pe > part_start && pe[-1] == '\r') pe--;
                        const gchar* sub_body;
                        GPtrArray* sub_headers = read_headers(part_start, pe, &sub_body);
                        g_ptr_array_add(part->children,
                                        parse_part(sub_headers, sub_body, (gsize)(pe - sub_body), max_part, depth + 1, count));
                        g_ptr_array_unref(sub_headers);
                    }
                    gboolean closing = (gsize)(le - p) >= dl + 2 && p[dl] == '-' && p[dl + 1] == '-';
                    if (closing) {
                        part_start = NULL;
                        break;
                    }
                    part_start = nl != NULL ? nl + 1 : end;
                }
                p = nl != NULL ? nl + 1 : end;
            }
            if (part_start != NULL && part_start < end) {
                /* Truncated: the last part runs to the end */
                const gchar* sub_body;
                GPtrArray* sub_headers = read_headers(part_start, end, &sub_body);
                g_ptr_array_add(part->children,
                                parse_part(sub_headers, sub_body, (gsize)(end - sub_body), max_part, depth + 1, count));
                g_ptr_array_unref(sub_headers);
            }
            g_free(delim);
        }
        g_free(boundary);
        part->size = body_len;
        return part;
    }
    const gchar* cte = header_in(headers, "Content-Transfer-Encoding");
    gchar* enc = main_value(cte);
    gboolean b64 = enc != NULL && strcmp(enc, "base64") == 0;
    gboolean qp = enc != NULL && strcmp(enc, "quoted-printable") == 0;
    g_free(enc);
    part->size = b64 ? body_len * 3 / 4 : body_len;
    if (body_len <= max_part * (b64 ? 2 : 1)) {
        GByteArray* bytes = b64 ? decode_b64(body, body_len) : qp ? decode_qp(body, body_len, FALSE) : NULL;
        if (bytes == NULL) {
            bytes = g_byte_array_sized_new((guint)body_len);
            g_byte_array_append(bytes, (const guint8*)body, (guint)body_len);
        }
        part->size = bytes->len;
        part->data = g_byte_array_free_to_bytes(bytes);
        if (g_str_has_prefix(part->content_type, "message/rfc822") && depth < MAX_DEPTH) {
            gsize n = 0;
            const gchar* d = g_bytes_get_data(part->data, &n);
            part->message = parse_message(d, n, max_part, depth + 1, count);
        }
    }
    return part;
}

static void part_free(gpointer p) {
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

static MailMessage* parse_message(const gchar* data, gsize len, gsize max_part, gint depth, guint* count) {
    MailMessage* msg = g_new0(MailMessage, 1);
    const gchar* end = data + len;
    const gchar* body;
    msg->headers = read_headers(data, end, &body);
    msg->from = mail_decode_header(mail_message_header(msg, "From"));
    msg->to = mail_decode_header(mail_message_header(msg, "To"));
    msg->cc = mail_decode_header(mail_message_header(msg, "Cc"));
    msg->subject = mail_decode_header(mail_message_header(msg, "Subject"));
    msg->date_raw = g_strdup(mail_message_header(msg, "Date"));
    msg->date = mail_parse_date(msg->date_raw);
    msg->message_id = g_strdup(mail_message_header(msg, "Message-ID"));
    msg->root = parse_part(msg->headers, body, (gsize)(end - body), max_part, depth, count);
    const gchar* ct = msg->root->content_type;
    msg->encrypted = strcmp(ct, "multipart/encrypted") == 0 || strcmp(ct, "application/pkcs7-mime") == 0 ||
                     strcmp(ct, "application/x-pkcs7-mime") == 0;
    return msg;
}

MailMessage* mail_message_parse(const gchar* data, gsize len, gsize max_part) {
    /* emlx: a byte count line comes first */
    gsize i = 0;
    while (i < len && i < 12 && g_ascii_isdigit(data[i])) i++;
    if (i > 0 && i < len && (data[i] == '\n' || (data[i] == ' ' && i + 1 < len))) {
        const gchar* nl = memchr(data, '\n', MIN(len, 64));
        if (nl != NULL) {
            gsize skip = (gsize)(nl - data) + 1;
            gsize declared = (gsize)g_ascii_strtoull(data, NULL, 10);
            data += skip;
            len -= skip;
            if (declared > 0 && declared < len) len = declared;
        }
    }
    guint count = 0;
    return parse_message(data, len, max_part, 0, &count);
}

void mail_message_free(MailMessage* msg) {
    if (msg == NULL) return;
    g_free(msg->from);
    g_free(msg->to);
    g_free(msg->cc);
    g_free(msg->subject);
    g_free(msg->date_raw);
    g_free(msg->message_id);
    part_free(msg->root);
    g_ptr_array_unref(msg->headers);
    g_free(msg);
}

/* ------------------------------------------------------------------------- */
/* Queries                                                                   */
/* ------------------------------------------------------------------------- */

static const MailPart* find_body(const MailPart* part, const gchar* type, gint depth) {
    if (part == NULL || depth > MAX_DEPTH) return NULL;
    if (part->children != NULL) {
        /* message/rfc822 children are attachments: not searched */
        for (guint i = 0; i < part->children->len; i++) {
            const MailPart* hit = find_body(g_ptr_array_index(part->children, i), type, depth + 1);
            if (hit != NULL) return hit;
        }
        return NULL;
    }
    if (!part->attachment && strcmp(part->content_type, type) == 0 && part->data != NULL) return part;
    return NULL;
}

const MailPart* mail_message_body(const MailMessage* msg, gboolean html) {
    return msg != NULL ? find_body(msg->root, html ? "text/html" : "text/plain", 0) : NULL;
}

static void collect_attachments(const MailPart* part, const MailPart* plain, const MailPart* html, GPtrArray* out,
                                gint depth) {
    if (part == NULL || depth > MAX_DEPTH) return;
    if (part->children != NULL) {
        for (guint i = 0; i < part->children->len; i++) {
            collect_attachments(g_ptr_array_index(part->children, i), plain, html, out, depth + 1);
        }
        return;
    }
    if (part == plain || part == html) return;
    gboolean text_body = g_str_has_prefix(part->content_type, "text/") && part->filename == NULL && !part->attachment;
    if (part->filename != NULL || part->attachment || part->message != NULL ||
        (!text_body && part->content_id == NULL)) {
        g_ptr_array_add(out, (gpointer)part);
    }
}

GPtrArray* mail_message_attachments(const MailMessage* msg) {
    GPtrArray* out = g_ptr_array_new();
    if (msg != NULL) {
        collect_attachments(msg->root, mail_message_body(msg, FALSE), mail_message_body(msg, TRUE), out, 0);
    }
    return out;
}

static const MailPart* find_cid(const MailPart* part, const gchar* cid, gint depth) {
    if (part == NULL || depth > MAX_DEPTH) return NULL;
    if (part->content_id != NULL && strcmp(part->content_id, cid) == 0) return part;
    for (guint i = 0; part->children != NULL && i < part->children->len; i++) {
        const MailPart* hit = find_cid(g_ptr_array_index(part->children, i), cid, depth + 1);
        if (hit != NULL) return hit;
    }
    return NULL;
}

const MailPart* mail_message_find_cid(const MailMessage* msg, const gchar* cid) {
    return msg != NULL && cid != NULL ? find_cid(msg->root, cid, 0) : NULL;
}

gchar* mail_part_text(const MailPart* part) {
    if (part == NULL || part->data == NULL) return NULL;
    gsize n = 0;
    const gchar* d = g_bytes_get_data(part->data, &n);
    return mail_to_utf8(d, n, part->charset);
}
