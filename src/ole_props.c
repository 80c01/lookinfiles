/* Document properties of compound files (see ole_props.h) */
#include "ole_props.h"
#include <string.h>

#define PROPS_MAX_BYTES (1024 * 1024)

static const guint8 FMTID_SUMMARY[16] = {0xE0, 0x85, 0x9F, 0xF2, 0xF9, 0x4F, 0x68, 0x10,
                                         0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9};
static const guint8 FMTID_DOC_SUMMARY[16] = {0x02, 0xD5, 0xCD, 0xD5, 0x9C, 0x2E, 0x1B, 0x10,
                                             0x93, 0x97, 0x08, 0x00, 0x2B, 0x2C, 0xF9, 0xAE};

/* Variant types */
#define VT_I2 2
#define VT_I4 3
#define VT_BOOL 11
#define VT_LPSTR 30
#define VT_LPWSTR 31
#define VT_FILETIME 64

static guint16 u16(const guint8* p) {
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 u32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

OleProperties* ole_properties_new(void) {
    OleProperties* p = g_new0(OleProperties, 1);
    p->created = p->modified = -1;
    p->pages = p->words = p->chars = p->slides = p->notes = -1;
    return p;
}

void ole_properties_free(OleProperties* p) {
    if (p == NULL) return;
    g_free(p->title);
    g_free(p->subject);
    g_free(p->author);
    g_free(p->keywords);
    g_free(p->comments);
    g_free(p->last_author);
    g_free(p->revision);
    g_free(p->app_name);
    g_free(p->category);
    g_free(p->manager);
    g_free(p->company);
    g_free(p);
}

/* 8-bit text in a Windows code page as UTF-8 */
static gchar* from_codepage(const gchar* s, gsize len, gint codepage) {
    gchar* out = NULL;
    if (codepage == 65001) {
        out = g_strndup(s, len);
    } else {
        gchar* name = codepage == 1252 || codepage <= 0 ? g_strdup("WINDOWS-1252")
                      : codepage == 10000           ? g_strdup("MACINTOSH")
                                                    : g_strdup_printf("CP%d", codepage);
        out = g_convert(s, (gssize)len, "UTF-8", name, NULL, NULL, NULL);
        if (out == NULL) out = g_convert(s, (gssize)len, "UTF-8", "WINDOWS-1252", NULL, NULL, NULL);
        g_free(name);
    }
    if (out == NULL) out = g_strndup(s, len);
    if (!g_utf8_validate(out, -1, NULL)) {
        gchar* valid = g_utf8_make_valid(out, -1);
        g_free(out);
        out = valid;
    }
    return g_strstrip(out);
}

/* The string value at p (end: end of the section), NULL when empty */
static gchar* string_value(const guint8* p, const guint8* end, guint32 type, gint codepage) {
    if (p + 8 > end) return NULL;
    guint32 n = u32(p + 4);
    const guint8* s = p + 8;
    gchar* out = NULL;
    if (type == VT_LPSTR) {
        if (n > (guint32)(end - s)) return NULL;
        gsize len = n;
        while (len > 0 && s[len - 1] == 0) len--;
        if (codepage == 1200) {
            /* Unicode code page: the "8-bit" string is UTF-16 */
            out = g_utf16_to_utf8((const gunichar2*)s, (glong)(len / 2), NULL, NULL, NULL);
        } else {
            out = from_codepage((const gchar*)s, len, codepage);
        }
    } else if (type == VT_LPWSTR) {
        if ((guint64)n * 2 > (guint64)(end - s)) return NULL;
        gunichar2* w = g_new(gunichar2, n + 1);
        for (guint32 i = 0; i < n; i++) w[i] = u16(s + i * 2);
        glong len = (glong)n;
        while (len > 0 && w[len - 1] == 0) len--;
        out = g_utf16_to_utf8(w, len, NULL, NULL, NULL);
        g_free(w);
    }
    if (out != NULL) {
        g_strstrip(out);
        if (out[0] == '\0') {
            g_free(out);
            out = NULL;
        }
    }
    return out;
}

static void set_string(gchar** field, gchar* value) {
    if (value == NULL) return;
    g_free(*field);
    *field = value;
}

/* FILETIME (100 ns since 1601) as Unix time; -1 when zero or absurd */
static gint64 filetime(const guint8* p) {
    guint64 ft = (guint64)u32(p) | ((guint64)u32(p + 4) << 32);
    if (ft == 0) return -1;
    gint64 t = (gint64)(ft / 10000000ULL) - 11644473600LL;
    return t > 0 && t < 32503680000LL ? t : -1;
}

static gboolean parse_section(const guint8* sec, const guint8* end, gboolean doc_summary, OleProperties* props) {
    if (sec + 8 > end) return FALSE;
    guint32 size = u32(sec);
    guint32 count = u32(sec + 4);
    if (size < 8 || size > (guint32)(end - sec) || count > (size - 8) / 8) return FALSE;
    end = sec + size;
    /* The code page first: it decodes the 8-bit strings */
    gint codepage = 1252;
    for (guint32 i = 0; i < count; i++) {
        const guint8* e = sec + 8 + i * 8;
        guint32 off = u32(e + 4);
        if (u32(e) == 1 && off + 8 <= size && (u32(sec + off) & 0xFFFF) == VT_I2) codepage = u16(sec + off + 4);
    }
    for (guint32 i = 0; i < count; i++) {
        const guint8* e = sec + 8 + i * 8;
        guint32 id = u32(e);
        guint32 off = u32(e + 4);
        if (off + 4 > size) continue;
        const guint8* v = sec + off;
        guint32 type = u32(v) & 0xFFFF;
        gboolean text = type == VT_LPSTR || type == VT_LPWSTR;
        gint num = -1;
        if ((type == VT_I4 || type == VT_I2) && v + 8 <= end) num = type == VT_I4 ? (gint)u32(v + 4) : (gint16)u16(v + 4);
        if (!doc_summary) {
            switch (id) {
                case 2: if (text) set_string(&props->title, string_value(v, end, type, codepage)); break;
                case 3: if (text) set_string(&props->subject, string_value(v, end, type, codepage)); break;
                case 4: if (text) set_string(&props->author, string_value(v, end, type, codepage)); break;
                case 5: if (text) set_string(&props->keywords, string_value(v, end, type, codepage)); break;
                case 6: if (text) set_string(&props->comments, string_value(v, end, type, codepage)); break;
                case 8: if (text) set_string(&props->last_author, string_value(v, end, type, codepage)); break;
                case 9: if (text) set_string(&props->revision, string_value(v, end, type, codepage)); break;
                case 12: if (type == VT_FILETIME && v + 12 <= end) props->created = filetime(v + 4); break;
                case 13: if (type == VT_FILETIME && v + 12 <= end) props->modified = filetime(v + 4); break;
                case 14: if (num >= 0) props->pages = num; break;
                case 15: if (num >= 0) props->words = num; break;
                case 16: if (num >= 0) props->chars = num; break;
                case 18: if (text) set_string(&props->app_name, string_value(v, end, type, codepage)); break;
                case 19: if (num >= 0) props->password = (num & 1) != 0; break;
                default: break;
            }
        } else {
            switch (id) {
                case 2: if (text) set_string(&props->category, string_value(v, end, type, codepage)); break;
                case 7: if (num >= 0) props->slides = num; break;
                case 8: if (num >= 0) props->notes = num; break;
                case 14: if (text) set_string(&props->manager, string_value(v, end, type, codepage)); break;
                case 15: if (text) set_string(&props->company, string_value(v, end, type, codepage)); break;
                default: break;
            }
        }
    }
    return TRUE;
}

gboolean ole_properties_parse(const guint8* data, gsize len, OleProperties* props) {
    if (data == NULL || len < 48 || u16(data) != 0xFFFE) return FALSE;
    guint32 n_sets = u32(data + 24);
    if (n_sets == 0 || n_sets > 4) return FALSE;
    const guint8* end = data + len;
    gboolean ok = FALSE;
    for (guint32 i = 0; i < n_sets && 28 + (i + 1) * 20 <= len; i++) {
        const guint8* set = data + 28 + i * 20;
        guint32 off = u32(set + 16);
        if (off >= len) continue;
        if (memcmp(set, FMTID_SUMMARY, 16) == 0) ok |= parse_section(data + off, end, FALSE, props);
        else if (memcmp(set, FMTID_DOC_SUMMARY, 16) == 0) ok |= parse_section(data + off, end, TRUE, props);
    }
    return ok;
}

OleProperties* ole_properties_read(OleReader* ole) {
    OleProperties* props = ole_properties_new();
    gboolean any = FALSE;
    static const gchar* const STREAMS[] = {"\005SummaryInformation", "\005DocumentSummaryInformation"};
    for (gsize i = 0; i < G_N_ELEMENTS(STREAMS); i++) {
        GBytes* b = ole_reader_read(ole, STREAMS[i], PROPS_MAX_BYTES, NULL);
        if (b == NULL) continue;
        gsize len = 0;
        const guint8* data = g_bytes_get_data(b, &len);
        any |= ole_properties_parse(data, len, props);
        g_bytes_unref(b);
    }
    if (!any) {
        ole_properties_free(props);
        return NULL;
    }
    return props;
}

static gchar* format_time(gint64 t) {
    GDateTime* dt = g_date_time_new_from_unix_local(t);
    if (dt == NULL) return NULL;
    gchar* s = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    g_date_time_unref(dt);
    return s;
}

void ole_properties_card(PreviewDoc* doc, const OleProperties* p) {
    if (doc == NULL || p == NULL) return;
    if (p->title != NULL) preview_doc_field(doc, "Title", p->title);
    if (p->subject != NULL) preview_doc_field(doc, "Subject", p->subject);
    if (p->author != NULL) preview_doc_field(doc, "Author", p->author);
    if (p->company != NULL) preview_doc_field(doc, "Company", p->company);
    if (p->keywords != NULL) preview_doc_field(doc, "Keywords", p->keywords);
    if (p->comments != NULL) preview_doc_field(doc, "Comments", p->comments);
    gchar* t = p->created >= 0 ? format_time(p->created) : NULL;
    if (t != NULL) preview_doc_field(doc, "Created", t);
    g_free(t);
    t = p->modified >= 0 ? format_time(p->modified) : NULL;
    if (t != NULL) preview_doc_field(doc, "Modified", t);
    g_free(t);
    if (p->last_author != NULL) preview_doc_field(doc, "Last saved by", p->last_author);
    GString* counts = g_string_new(NULL);
    if (p->pages > 0) g_string_append_printf(counts, "%d page%s", p->pages, p->pages == 1 ? "" : "s");
    if (p->words > 0) g_string_append_printf(counts, "%s%d words", counts->len ? " · " : "", p->words);
    if (p->slides > 0) g_string_append_printf(counts, "%s%d slide%s", counts->len ? " · " : "", p->slides, p->slides == 1 ? "" : "s");
    if (counts->len > 0) preview_doc_field(doc, "Contents", counts->str);
    g_string_free(counts, TRUE);
    if (p->app_name != NULL) preview_doc_field(doc, "Application", p->app_name);
    if (p->password) preview_doc_field(doc, "Security", "Password protected");
}

gchar* ole_properties_html_head(const OleProperties* p, const gchar* kind) {
    GString* head = g_string_new("<head>");
    if (p != NULL && p->title != NULL) {
        gchar* t = g_markup_escape_text(p->title, -1);
        g_string_append_printf(head, "<title>%s</title>", t);
        g_free(t);
    }
    if (p != NULL && p->author != NULL) {
        gchar* a = g_markup_escape_text(p->author, -1);
        g_string_append_printf(head, "<meta name=\"author\" content=\"%s\">", a);
        g_free(a);
    }
    GString* desc = g_string_new(kind != NULL ? kind : "");
    if (p != NULL) {
        if (p->pages > 0) g_string_append_printf(desc, " · %d page%s", p->pages, p->pages == 1 ? "" : "s");
        if (p->words > 0) g_string_append_printf(desc, " · %d words", p->words);
        if (p->slides > 0) g_string_append_printf(desc, " · %d slide%s", p->slides, p->slides == 1 ? "" : "s");
        gchar* t = p->modified >= 0 ? format_time(p->modified) : NULL;
        if (t != NULL) {
            t[10] = '\0'; /* the date is enough */
            g_string_append_printf(desc, " · modified %s", t);
        }
        g_free(t);
    }
    if (desc->len > 0) {
        gchar* d = g_markup_escape_text(desc->str, -1);
        g_string_append_printf(head, "<meta name=\"description\" content=\"%s\">", d);
        g_free(d);
    }
    g_string_free(desc, TRUE);
    g_string_append(head, "</head>");
    return g_string_free(head, FALSE);
}
