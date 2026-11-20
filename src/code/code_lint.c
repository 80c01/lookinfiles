/* Built-in syntax diagnostics for JSON, TOML, YAML and XML (see code_lint.h) */
#include "code/code_lint.h"

#include "core/file_types.h"
#include "formats/json_view.h"
#include "formats/toml_view.h"
#include "formats/xml_view.h"
#include "formats/yaml_view.h"
#include "preview/preview_doc.h"

#include <string.h>

const gchar* code_lint_source_name(const gchar* path) {
    if (path == NULL || path[0] == '\0') return NULL;
    switch (file_kind_from_name(path)) {
        case FILE_KIND_JSON: return "json";
        case FILE_KIND_TOML: return "toml";
        case FILE_KIND_YAML: return "yaml";
        case FILE_KIND_XML:  return "xml";
        default:             return NULL;
    }
}

/* Parse "line 3, column 5: msg" (or any localized variant with two unsigned
 * numbers before ':') into 0-based line/col and trimmed message text. */
static void parse_error_location(const gchar* raw, gint* out_line, gint* out_col, gchar** out_msg) {
    *out_line = 0;
    *out_col = 0;
    if (raw == NULL) {
        *out_msg = g_strdup("");
        return;
    }
    const gchar* colon = strchr(raw, ':');
    if (colon != NULL) {
        guint nums[2] = {0, 0};
        guint found = 0;
        for (const gchar* p = raw; p < colon && found < 2; ) {
            if (g_ascii_isdigit((guchar)*p)) {
                guint64 v = 0;
                while (p < colon && g_ascii_isdigit((guchar)*p)) {
                    v = v * 10 + (guint64)(*p - '0');
                    p++;
                }
                nums[found++] = (guint)MIN(v, (guint64)G_MAXINT);
            } else {
                p++;
            }
        }
        if (found >= 1) {
            *out_line = nums[0] > 0 ? (gint)(nums[0] - 1) : 0;
            if (found >= 2) *out_col = nums[1] > 0 ? (gint)(nums[1] - 1) : 0;
            const gchar* msg = colon + 1;
            while (*msg == ' ' || *msg == '\t') msg++;
            *out_msg = g_strdup(msg[0] != '\0' ? msg : raw);
            return;
        }
    }
    *out_msg = g_strdup(raw);
}

/* Byte slice [start, end) of 0-based line in text; returns FALSE if out of range */
static gboolean line_bounds(const gchar* text, gsize len, gint target_line,
                            gsize* out_start, gsize* out_end) {
    gsize pos = 0;
    gint line = 0;
    while (line < target_line && pos < len) {
        const gchar* nl = memchr(text + pos, '\n', len - pos);
        if (nl == NULL) return FALSE;
        pos = (gsize)(nl - text) + 1;
        line++;
    }
    if (line != target_line) return FALSE;
    const gchar* nl = memchr(text + pos, '\n', len - pos);
    gsize end = nl != NULL ? (gsize)(nl - text) : len;
    if (end > pos && text[end - 1] == '\r') end--;
    *out_start = pos;
    *out_end = end;
    return TRUE;
}

/* Convert a 0-based UTF-8 character column on [ls, le) to a byte column and a
 * visible end column covering the token at that point */
static void resolve_span(const gchar* text, gsize len, gint line, gint char_col,
                         gint* out_col, gint* out_end_col) {
    gsize ls = 0, le = 0;
    if (!line_bounds(text, len, line, &ls, &le)) {
        *out_col = 0;
        *out_end_col = 0;
        return;
    }
    gsize llen = le - ls;
    const gchar* s = text + ls;
    const gchar* p = s;
    const gchar* end = s + llen;
    for (gint i = 0; i < char_col && p < end; i++) {
        p = g_utf8_next_char(p);
    }
    if (p > end) p = end;
    gint bstart = (gint)(p - s);
    const gchar* q = p;
    while (q < end && !g_ascii_isspace((guchar)*q) && *q != ',' && *q != '}' && *q != ']') {
        q = g_utf8_next_char(q);
    }
    if (q == p && q < end) q = g_utf8_next_char(q);
    if (q > end) q = end;
    *out_col = bstart;
    *out_end_col = (gint)(q - s);
}

static gboolean is_blank(const gchar* text, gsize len) {
    for (gsize i = 0; i < len; i++) {
        if (!g_ascii_isspace((guchar)text[i])) return FALSE;
    }
    return TRUE;
}

GPtrArray* code_lint_check(const gchar* path, const gchar* text, gsize len) {
    GPtrArray* out = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_diagnostic_free);
    const gchar* src = code_lint_source_name(path);
    if (src == NULL || text == NULL || len == 0 || len > CODE_LINT_MAX_BYTES || is_blank(text, len)) {
        return out;
    }

    PreviewDoc* doc = preview_doc_new();
    GError* err = NULL;
    gboolean ok = TRUE;
    switch (file_kind_from_name(path)) {
        case FILE_KIND_JSON: ok = json_view_append(doc, text, len, NULL, &err); break;
        case FILE_KIND_TOML: ok = toml_view_append(doc, text, len, FALSE, NULL, NULL, &err); break;
        case FILE_KIND_YAML: ok = yaml_view_append(doc, text, len, FALSE, NULL, NULL, &err); break;
        case FILE_KIND_XML:  ok = xml_view_append(doc, text, len, FALSE, NULL, NULL, &err); break;
        default: break;
    }
    preview_doc_free(doc);

    if (!ok && err != NULL) {
        gint line = 0, char_col = 0;
        gchar* msg = NULL;
        parse_error_location(err->message, &line, &char_col, &msg);
        gint col = 0, end_col = 0;
        resolve_span(text, len, line, char_col, &col, &end_col);

        LspDiagnostic* d = g_new0(LspDiagnostic, 1);
        d->line = line;
        d->column = col;
        d->end_line = line;
        d->end_column = end_col;
        d->severity = LSP_SEVERITY_ERROR;
        d->message = msg;
        d->source = g_strdup(src);
        g_ptr_array_add(out, d);
    }
    g_clear_error(&err);
    return out;
}
