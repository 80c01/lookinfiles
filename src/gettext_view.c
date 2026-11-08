/* gettext catalogs for the preview (see gettext_view.h). */
#include "gettext_view.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

static void message_clear(gpointer data) {
    GettextMessage* m = data;
    g_free(m->context);
    g_free(m->id);
    g_free(m->plural);
    g_free(m->translation);
}

static GettextCatalog* catalog_new(void) {
    GettextCatalog* c = g_new0(GettextCatalog, 1);
    c->messages = g_array_new(FALSE, TRUE, sizeof(GettextMessage));
    g_array_set_clear_func(c->messages, message_clear);
    return c;
}

void gettext_catalog_free(GettextCatalog* catalog) {
    if (catalog == NULL) {
        return;
    }
    g_array_free(catalog->messages, TRUE);
    g_free(catalog->header);
    g_free(catalog);
}

/* Add a message; the entry with an empty msgid is the header */
static void add_message(GettextCatalog* c, gchar* context, gchar* id, gchar* plural, gchar* translation,
                        gboolean fuzzy) {
    if (context == NULL && (id == NULL || id[0] == '\0')) {
        g_free(c->header);
        c->header = translation;
        g_free(id);
        g_free(plural);
        return;
    }
    GettextMessage m = { context, id, plural, translation, fuzzy };
    g_array_append_val(c->messages, m);
}

/* --- .mo --- */

GettextCatalog* gettext_parse_mo(const guchar* data, gsize len, GError** error) {
    gboolean le;
    if (len >= 28 && memcmp(data, "\xDE\x12\x04\x95", 4) == 0) {
        le = TRUE;
    } else if (len >= 28 && memcmp(data, "\x95\x04\x12\xDE", 4) == 0) {
        le = FALSE;
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not a gettext catalog"));
        return NULL;
    }
#define U32(off) (le ? ((guint32)data[(off)] | ((guint32)data[(off) + 1] << 8) | ((guint32)data[(off) + 2] << 16) | ((guint32)data[(off) + 3] << 24)) \
                     : (((guint32)data[(off)] << 24) | ((guint32)data[(off) + 1] << 16) | ((guint32)data[(off) + 2] << 8) | (guint32)data[(off) + 3]))
    guint32 n = U32(8);
    guint32 orig = U32(12);
    guint32 trans = U32(16);
    GettextCatalog* c = catalog_new();
    for (guint32 i = 0; i < n; i++) {
        guint64 oe = (guint64)orig + (guint64)i * 8;
        guint64 te = (guint64)trans + (guint64)i * 8;
        if (oe + 8 > len || te + 8 > len) {
            break;
        }
        guint32 olen = U32(oe), ooff = U32(oe + 4);
        guint32 tlen = U32(te), toff = U32(te + 4);
        if ((guint64)ooff + olen > len || (guint64)toff + tlen > len) {
            break;
        }
        /* "context\x04id\0plural" and "translation\0plural translation" */
        const gchar* o = (const gchar*)data + ooff;
        const gchar* eot = memchr(o, '\x04', olen);
        gchar* context = eot != NULL ? g_utf8_make_valid(o, eot - o) : NULL;
        const gchar* id_start = eot != NULL ? eot + 1 : o;
        gsize id_room = olen - (gsize)(id_start - o);
        gsize id_len = strnlen(id_start, id_room);
        gchar* id = g_utf8_make_valid(id_start, (gssize)id_len);
        gchar* plural = id_len + 1 < id_room ? g_utf8_make_valid(id_start + id_len + 1, -1) : NULL;
        const gchar* t = (const gchar*)data + toff;
        gchar* translation = g_utf8_make_valid(t, (gssize)strnlen(t, tlen));
        add_message(c, context, id, plural, translation, FALSE);
    }
#undef U32
    return c;
}

/* --- .po --- */

/* Concatenation of the "..." strings of a .po value, unescaped */
static void append_po_string(GString* out, const gchar* p) {
    const gchar* q = strchr(p, '"');
    if (q == NULL) {
        return;
    }
    for (q++; *q != '\0' && *q != '"'; q++) {
        if (*q == '\\' && q[1] != '\0') {
            q++;
            g_string_append_c(out, *q == 'n' ? '\n' : *q == 't' ? '\t' : *q);
        } else {
            g_string_append_c(out, *q);
        }
    }
}

GettextCatalog* gettext_parse_po(const gchar* text, gsize len, GError** error) {
    GettextCatalog* c = catalog_new();
    gchar* copy = g_strndup(text, len);
    gchar** lines = g_strsplit(copy, "\n", -1);
    g_free(copy);
    GString* ctx = NULL;
    GString* id = NULL;
    GString* plural = NULL;
    GString* str = NULL;
    GString* current = NULL;
    gboolean fuzzy = FALSE;
    gboolean next_fuzzy = FALSE;
    gboolean any = FALSE;
#define FLUSH() do { \
        if (id != NULL) { \
            add_message(c, ctx != NULL ? g_string_free(ctx, FALSE) : NULL, g_string_free(id, FALSE), \
                        plural != NULL ? g_string_free(plural, FALSE) : NULL, \
                        str != NULL ? g_string_free(str, FALSE) : g_strdup(""), fuzzy); \
        } else { \
            if (ctx != NULL) g_string_free(ctx, TRUE); \
            if (plural != NULL) g_string_free(plural, TRUE); \
            if (str != NULL) g_string_free(str, TRUE); \
        } \
        ctx = id = plural = str = current = NULL; \
    } while (0)
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = g_strstrip(lines[i]);
        if (g_str_has_prefix(line, "#,")) {
            next_fuzzy = strstr(line, "fuzzy") != NULL;
            continue;
        }
        if (line[0] == '#' || line[0] == '\0') {
            continue; /* comments, obsolete entries (#~) */
        }
        if (g_str_has_prefix(line, "msgctxt")) {
            FLUSH();
            fuzzy = next_fuzzy;
            next_fuzzy = FALSE;
            ctx = g_string_new(NULL);
            current = ctx;
            append_po_string(current, line + 7);
            any = TRUE;
        } else if (g_str_has_prefix(line, "msgid_plural")) {
            plural = g_string_new(NULL);
            current = plural;
            append_po_string(current, line + 12);
        } else if (g_str_has_prefix(line, "msgid")) {
            /* A new entry, unless its msgctxt came first */
            if (id != NULL || ctx == NULL) {
                FLUSH();
                fuzzy = next_fuzzy;
                next_fuzzy = FALSE;
            }
            id = g_string_new(NULL);
            current = id;
            append_po_string(current, line + 5);
            any = TRUE;
        } else if (g_str_has_prefix(line, "msgstr")) {
            /* msgstr or msgstr[0]: the first translation is kept */
            if (str == NULL) {
                str = g_string_new(NULL);
                current = str;
                append_po_string(current, line + 6);
            } else {
                current = NULL;
            }
        } else if (line[0] == '"' && current != NULL) {
            append_po_string(current, line);
        }
    }
    FLUSH();
#undef FLUSH
    g_strfreev(lines);
    if (!any) {
        gettext_catalog_free(c);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("no messages"));
        return NULL;
    }
    return c;
}

gchar* gettext_header_field(const GettextCatalog* catalog, const gchar* field) {
    if (catalog->header == NULL) {
        return NULL;
    }
    gchar** lines = g_strsplit(catalog->header, "\n", -1);
    gchar* value = NULL;
    gsize flen = strlen(field);
    for (gint i = 0; lines[i] != NULL && value == NULL; i++) {
        if (g_ascii_strncasecmp(lines[i], field, flen) == 0 && lines[i][flen] == ':') {
            value = g_strstrip(g_strdup(lines[i] + flen + 1));
            if (value[0] == '\0') {
                g_clear_pointer(&value, g_free);
            }
        }
    }
    g_strfreev(lines);
    return value;
}

void gettext_view_append(PreviewDoc* doc, const GettextCatalog* catalog) {
    static const struct {
        const gchar* field;
        const gchar* label;
    } FIELDS[] = {
        { "Project-Id-Version", N_("Project") }, { "Language", N_("Language") },
        { "Last-Translator", N_("Translator") }, { "Language-Team", N_("Team") },
        { "PO-Revision-Date", N_("Revised") }, { "Plural-Forms", N_("Plural forms") },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(FIELDS); i++) {
        gchar* v = gettext_header_field(catalog, FIELDS[i].field);
        preview_doc_field(doc, _(FIELDS[i].label), v);
        g_free(v);
    }
    guint translated = 0, fuzzy = 0;
    for (guint i = 0; i < catalog->messages->len; i++) {
        const GettextMessage* m = &g_array_index(catalog->messages, GettextMessage, i);
        if (m->fuzzy) {
            fuzzy++;
        } else if (m->translation != NULL && m->translation[0] != '\0') {
            translated++;
        }
    }
    guint total = catalog->messages->len;
    gchar* counts = g_strdup_printf(_("%1$u translated, %2$u fuzzy, %3$u untranslated"), translated, fuzzy,
                                    total - translated - fuzzy);
    gchar* label = g_strdup_printf("%u", total);
    preview_doc_field(doc, _("Messages"), label);
    preview_doc_field(doc, "", counts);
    g_free(label);
    g_free(counts);
    preview_doc_append_len(doc, "\n", 1);
    if (total == 0) {
        return;
    }
    g_autoptr(PreviewTable) t = preview_table_new(2);
    const gchar* header[] = { _("Original"), _("Translation") };
    preview_table_set_header(t, header);
    preview_table_set_max_cell_width(t, 60);
    for (guint i = 0; i < total && i < GETTEXT_VIEW_MAX_ROWS; i++) {
        const GettextMessage* m = &g_array_index(catalog->messages, GettextMessage, i);
        gchar* original = m->context != NULL ? g_strdup_printf("%s [%s]", m->id, m->context) : g_strdup(m->id);
        gchar* translation = m->fuzzy ? g_strdup_printf(_("%s (fuzzy)"), m->translation) : g_strdup(m->translation);
        const gchar* row[] = { original, translation };
        preview_table_add_row(t, row, 2);
        g_free(translation);
        g_free(original);
    }
    preview_doc_add_table(doc, t);
    if (total > GETTEXT_VIEW_MAX_ROWS) {
        gchar* note = g_strdup_printf(_("Showing the first %u messages."), GETTEXT_VIEW_MAX_ROWS);
        preview_doc_note(doc, note);
        g_free(note);
    }
}
