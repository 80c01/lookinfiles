/* TOML preview (see toml_view.h). */
#include "formats/toml_view.h"
#include "formats/text_outline.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

/* A table header still open: the tables after it may be under it */
typedef struct {
    GPtrArray* parts;     /* the key parts of its header, as written */
    gchar* path;
    guint block;
} Section;

typedef struct {
    const gchar* text;
    gsize len;
    gsize pos;
    GError** error;
    gboolean at_cut;      /* the end of the cut text was reached inside a value */
    gboolean cut;
    TextOutline* outline; /* lines, colours, blocks and paths found */
    gboolean any_token;   /* something was coloured (comments only: still TOML) */
    GPtrArray* open;      /* Section*, innermost last */
    GHashTable* array_counts; /* path of an array of tables -> entries so far */
    guint n_tables;
    guint n_keys;
} Parser;

#define MAX_DEPTH 128

static void section_free(gpointer data) {
    Section* s = data;
    g_ptr_array_unref(s->parts);
    g_free(s->path);
    g_free(s);
}

static gboolean fail(Parser* p, const gchar* what) {
    if (p->error != NULL && *p->error == NULL) {
        guint line = 1;
        guint col = 1;
        for (gsize i = 0; i < p->pos && i < p->len; i++) {
            if (p->text[i] == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
        }
        g_set_error(p->error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("line %1$u, column %2$u: %3$s"),
                    line, col, what);
    }
    return FALSE;
}

static guint line_at(const Parser* p, gsize pos) {
    return text_outline_line_at(p->outline, pos);
}

static void add_token(Parser* p, gsize start, gsize end, PreviewStyle style) {
    text_outline_add_token(p->outline, start, end, style);
    p->any_token |= end > start;
}

/* The path of the line holding pos, unless it has one already */
static void note_path(Parser* p, gsize pos, const gchar* path) {
    text_outline_set_path(p->outline, pos, path);
}

static gboolean at_end(const Parser* p) {
    return p->pos >= p->len;
}

static gboolean starts_with(const Parser* p, const gchar* prefix) {
    gsize n = strlen(prefix);
    return p->pos + n <= p->len && memcmp(p->text + p->pos, prefix, n) == 0;
}

static void skip_blanks(Parser* p) {
    while (!at_end(p) && (p->text[p->pos] == ' ' || p->text[p->pos] == '\t')) {
        p->pos++;
    }
}

static void skip_comment(Parser* p) {
    gsize start = p->pos;
    const gchar* nl = memchr(p->text + p->pos, '\n', p->len - p->pos);
    p->pos = nl != NULL ? (gsize)(nl - p->text) : p->len;
    gsize end = p->pos;
    if (end > start && p->text[end - 1] == '\r') end--;
    add_token(p, start, end, PREVIEW_STYLE_COMMENT);
}

/* Blanks, line breaks and comments (inside arrays) */
static void skip_space(Parser* p) {
    for (;;) {
        skip_blanks(p);
        if (at_end(p)) {
            return;
        }
        gchar c = p->text[p->pos];
        if (c == '\n' || c == '\r') {
            p->pos++;
        } else if (c == '#') {
            skip_comment(p);
        } else {
            return;
        }
    }
}

/* A string of any of the four kinds */
static gboolean parse_string(Parser* p) {
    gsize start = p->pos;
    gchar quote = p->text[p->pos];
    gboolean multi = (quote == '"' && starts_with(p, "\"\"\"")) || (quote == '\'' && starts_with(p, "'''"));
    p->pos += multi ? 3 : 1;
    for (;;) {
        if (at_end(p)) {
            add_token(p, start, p->len, PREVIEW_STYLE_STRING);
            if (multi && p->cut) {
                p->at_cut = TRUE;
                return TRUE;
            }
            p->pos = start;
            return fail(p, _("unterminated string"));
        }
        gchar c = p->text[p->pos];
        if (c == '\\' && quote == '"') {
            p->pos += 2;
            continue;
        }
        if (!multi && (c == '\n' || c == '\r')) {
            p->pos = start;
            return fail(p, _("unterminated string"));
        }
        if (c == quote && (!multi || (p->pos + 2 < p->len && p->text[p->pos + 1] == quote &&
                                      p->text[p->pos + 2] == quote))) {
            p->pos += multi ? 3 : 1;
            /* """a "quoted" word"""": up to two quotes before the closing ones */
            for (gint extra = 0; multi && extra < 2 && !at_end(p) && p->text[p->pos] == quote; extra++) {
                p->pos++;
            }
            break;
        }
        p->pos++;
    }
    add_token(p, start, p->pos, PREVIEW_STYLE_STRING);
    return TRUE;
}

static gboolean is_bare_key_char(gchar c) {
    return g_ascii_isalnum(c) || c == '_' || c == '-';
}

/* A dotted key into parts ("a", "\"b.c\"") */
static gboolean parse_key(Parser* p, GPtrArray* parts) {
    for (;;) {
        skip_blanks(p);
        gsize start = p->pos;
        if (!at_end(p) && (p->text[p->pos] == '"' || p->text[p->pos] == '\'')) {
            gchar quote = p->text[p->pos];
            const gchar* close = memchr(p->text + p->pos + 1, quote, p->len - p->pos - 1);
            const gchar* nl = memchr(p->text + p->pos + 1, '\n', p->len - p->pos - 1);
            while (close != NULL && quote == '"' && close[-1] == '\\') {
                close = memchr(close + 1, quote, (gsize)(p->text + p->len - close - 1));
            }
            if (close == NULL || (nl != NULL && nl < close)) {
                return fail(p, _("unterminated string"));
            }
            p->pos = (gsize)(close - p->text) + 1;
        } else {
            while (!at_end(p) && is_bare_key_char(p->text[p->pos])) {
                p->pos++;
            }
            if (p->pos == start) {
                return fail(p, _("expected a key"));
            }
        }
        g_ptr_array_add(parts, g_strndup(p->text + start, p->pos - start));
        skip_blanks(p);
        if (at_end(p) || p->text[p->pos] != '.') {
            return TRUE;
        }
        p->pos++;
    }
}

static gchar* join_path(const gchar* prefix, GPtrArray* parts, guint from) {
    GString* path = g_string_new(prefix);
    for (guint i = from; i < parts->len; i++) {
        if (path->len > 0) {
            g_string_append_c(path, '.');
        }
        g_string_append(path, g_ptr_array_index(parts, i));
    }
    return g_string_free(path, FALSE);
}

static gboolean parse_value(Parser* p, const gchar* path, gint depth);

/* Numbers, dates and times, booleans, inf and nan */
static gboolean parse_scalar(Parser* p) {
    gsize start = p->pos;
    while (!at_end(p) && strchr(",]}# \t\r\n", p->text[p->pos]) == NULL) {
        p->pos++;
    }
    /* A date and a time separated by a space: 1979-05-27 07:32:00 */
    if (p->pos - start == 10 && p->text[start + 4] == '-' && p->pos + 1 < p->len && p->text[p->pos] == ' ' &&
        g_ascii_isdigit(p->text[p->pos + 1])) {
        p->pos++;
        while (!at_end(p) && strchr(",]}# \t\r\n", p->text[p->pos]) == NULL) {
            p->pos++;
        }
    }
    gsize n = p->pos - start;
    const gchar* word = p->text + start;
    if ((n == 4 && memcmp(word, "true", 4) == 0) || (n == 5 && memcmp(word, "false", 5) == 0)) {
        add_token(p, start, p->pos, PREVIEW_STYLE_KEYWORD);
        return TRUE;
    }
    gboolean number = n > 0 && (g_ascii_isdigit(word[0]) || word[0] == '+' || word[0] == '-' ||
                                (n == 3 && (memcmp(word, "inf", 3) == 0 || memcmp(word, "nan", 3) == 0)));
    for (gsize i = 0; number && i < n; i++) {
        number = g_ascii_isalnum(word[i]) || strchr("_+-.: ", word[i]) != NULL;
    }
    if (!number) {
        p->pos = start;
        return fail(p, _("expected a value"));
    }
    add_token(p, start, p->pos, PREVIEW_STYLE_NUMBER);
    return TRUE;
}

/* An array or inline table: items until close, a block when on several lines */
static gboolean parse_container(Parser* p, const gchar* path, gint depth) {
    gboolean table = p->text[p->pos] == '{';
    gchar close = table ? '}' : ']';
    guint first = line_at(p, p->pos);
    guint count = 0;
    if (depth > MAX_DEPTH) {
        return fail(p, _("nested too deeply"));
    }
    p->pos++;
    for (;;) {
        skip_space(p);
        if (at_end(p)) {
            if (!p->cut) {
                return fail(p, table ? _("missing }") : _("missing ]"));
            }
            p->at_cut = TRUE;
            break;
        }
        if (p->text[p->pos] == close) {
            note_path(p, p->pos, path);
            p->pos++;
            break;
        }
        gboolean ok;
        if (table) {
            GPtrArray* parts = g_ptr_array_new_with_free_func(g_free);
            gsize key_start = p->pos;
            ok = parse_key(p, parts);
            gchar* key_path = ok ? join_path(path, parts, 0) : NULL;
            g_ptr_array_unref(parts);
            if (ok) {
                gsize key_end = p->pos;
                while (key_end > key_start && (p->text[key_end - 1] == ' ' || p->text[key_end - 1] == '\t')) key_end--;
                add_token(p, key_start, key_end, PREVIEW_STYLE_KEY);
                note_path(p, key_start, key_path);
                skip_blanks(p);
                ok = !at_end(p) && p->text[p->pos] == '=' ? TRUE : fail(p, _("expected ="));
            }
            if (ok) {
                p->pos++;
                skip_blanks(p);
                ok = parse_value(p, key_path, depth + 1);
            }
            g_free(key_path);
        } else {
            gchar* item_path = g_strdup_printf("%s[%u]", path, count);
            note_path(p, p->pos, item_path);
            ok = parse_value(p, item_path, depth + 1);
            g_free(item_path);
        }
        if (!ok) {
            return FALSE;
        }
        count++;
        if (p->at_cut) {
            break;
        }
        skip_space(p);
        if (!at_end(p) && p->text[p->pos] == ',') {
            p->pos++;
        } else if (!at_end(p) && p->text[p->pos] != close) {
            return fail(p, table ? _("expected , or }") : _("expected , or ]"));
        }
    }
    guint last = line_at(p, p->pos > 0 ? p->pos - 1 : 0);
    if (last > first) {
        text_outline_add_block(p->outline, first, last, path, depth, table ? TEXT_OUTLINE_KEYS : TEXT_OUTLINE_ITEMS,
                               count, TRUE);
    }
    return TRUE;
}

static gboolean parse_value(Parser* p, const gchar* path, gint depth) {
    if (at_end(p) || p->text[p->pos] == '\n' || p->text[p->pos] == '\r' || p->text[p->pos] == '#') {
        return fail(p, _("expected a value"));
    }
    gchar c = p->text[p->pos];
    if (c == '"' || c == '\'') {
        return parse_string(p);
    }
    if (c == '[' || c == '{') {
        return parse_container(p, path, depth);
    }
    return parse_scalar(p);
}

static Section* current_section(Parser* p) {
    return p->open->len > 0 ? g_ptr_array_index(p->open, p->open->len - 1) : NULL;
}

/* TRUE if prefix is a proper prefix of parts */
static gboolean is_under(GPtrArray* prefix, GPtrArray* parts) {
    if (prefix->len >= parts->len) {
        return FALSE;
    }
    for (guint i = 0; i < prefix->len; i++) {
        if (strcmp(g_ptr_array_index(prefix, i), g_ptr_array_index(parts, i)) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

/* [table] or [[array of tables]] */
static gboolean parse_header(Parser* p) {
    gboolean array = starts_with(p, "[[");
    gsize start = p->pos;
    p->pos += array ? 2 : 1;
    skip_blanks(p);
    gsize name_start = p->pos;
    GPtrArray* parts = g_ptr_array_new_with_free_func(g_free);
    if (!parse_key(p, parts)) {
        g_ptr_array_unref(parts);
        return FALSE;
    }
    gsize name_end = p->pos;
    while (name_end > name_start && (p->text[name_end - 1] == ' ' || p->text[name_end - 1] == '\t')) {
        name_end--;
    }
    if (!starts_with(p, array ? "]]" : "]")) {
        g_ptr_array_unref(parts);
        return fail(p, array ? _("expected ]]") : _("expected ]"));
    }
    add_token(p, start, name_start, PREVIEW_STYLE_KEYWORD);
    add_token(p, name_start, name_end, PREVIEW_STYLE_KEY);
    add_token(p, p->pos, p->pos + (array ? 2 : 1), PREVIEW_STYLE_KEYWORD);
    p->pos += array ? 2 : 1;

    /* The tables it is not under are closed */
    while (current_section(p) != NULL && !is_under(current_section(p)->parts, parts)) {
        g_ptr_array_remove_index(p->open, p->open->len - 1);
    }
    Section* parent = current_section(p);
    gchar* path = join_path(parent != NULL ? parent->path : "", parts, parent != NULL ? parent->parts->len : 0);
    if (array) {
        guint index = GPOINTER_TO_UINT(g_hash_table_lookup(p->array_counts, path));
        g_hash_table_insert(p->array_counts, g_strdup(path), GUINT_TO_POINTER(index + 1));
        gchar* entry = g_strdup_printf("%s[%u]", path, index);
        g_free(path);
        path = entry;
    }
    if (parent != NULL) {
        text_outline_block(p->outline, parent->block)->count++;
    }
    guint line = line_at(p, start);
    gint depth = (gint)p->open->len + 1;
    note_path(p, start, path);
    text_outline_add_heading(p->outline, name_start, name_end, MIN(depth, 6));
    Section* section = g_new0(Section, 1);
    section->parts = parts;
    section->path = path;
    section->block = text_outline_add_block(p->outline, line, line, path, depth, TEXT_OUTLINE_KEYS, 0, FALSE);
    g_ptr_array_add(p->open, section);
    p->n_tables++;
    return TRUE;
}

/* key = value in the current table */
static gboolean parse_pair(Parser* p) {
    Section* section = current_section(p);
    GPtrArray* parts = g_ptr_array_new_with_free_func(g_free);
    gsize start = p->pos;
    if (!parse_key(p, parts)) {
        g_ptr_array_unref(parts);
        return FALSE;
    }
    gsize end = p->pos;
    while (end > start && (p->text[end - 1] == ' ' || p->text[end - 1] == '\t')) end--;
    add_token(p, start, end, PREVIEW_STYLE_KEY);
    gchar* path = join_path(section != NULL ? section->path : "", parts, 0);
    g_ptr_array_unref(parts);
    note_path(p, start, path);
    gboolean ok = !at_end(p) && p->text[p->pos] == '=' ? TRUE : fail(p, _("expected ="));
    if (ok) {
        p->pos++;
        skip_blanks(p);
        ok = parse_value(p, path, section != NULL ? (gint)p->open->len + 1 : 1);
    }
    g_free(path);
    if (ok && section != NULL) {
        text_outline_block(p->outline, section->block)->count++;
    }
    p->n_keys += ok;
    return ok;
}

/* After a statement: blanks, a comment, the end of the line */
static gboolean end_statement(Parser* p) {
    skip_blanks(p);
    if (!at_end(p) && p->text[p->pos] == '#') {
        skip_comment(p);
    }
    if (!at_end(p) && p->text[p->pos] == '\r') {
        p->pos++;
    }
    if (!at_end(p) && p->text[p->pos] != '\n') {
        return fail(p, _("expected the end of the line"));
    }
    if (!at_end(p)) {
        p->pos++;
    }
    return TRUE;
}

static gboolean parse_document(Parser* p) {
    if (p->len >= 3 && memcmp(p->text, "\xEF\xBB\xBF", 3) == 0) {
        p->pos = 3;
    }
    while (!at_end(p)) {
        skip_blanks(p);
        if (at_end(p)) {
            break;
        }
        gchar c = p->text[p->pos];
        if (c == '#' || c == '\r' || c == '\n') {
            if (!end_statement(p)) { /* a comment or a blank line */
                return FALSE;
            }
            continue;
        }
        if (!(c == '[' ? parse_header(p) : parse_pair(p))) {
            return FALSE;
        }
        /* The open tables reach this statement */
        guint last = line_at(p, p->pos > 0 ? p->pos - 1 : 0);
        for (guint i = 0; i < p->open->len; i++) {
            const Section* section = g_ptr_array_index(p->open, i);
            TextOutlineBlock* block = text_outline_block(p->outline, section->block);
            block->last = MAX(block->last, last);
        }
        if (p->at_cut) {
            break;
        }
        if (!end_statement(p)) {
            return FALSE;
        }
    }
    if (p->n_tables == 0 && p->n_keys == 0 && !p->any_token) {
        return fail(p, _("empty document"));
    }
    return TRUE;
}

gboolean toml_view_append(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                          const PreviewFold* fold, TomlViewInfo* info, GError** error) {
    if (cut) {
        /* The last line is incomplete */
        while (len > 0 && text[len - 1] != '\n') len--;
    }
    Parser p = { 0 };
    p.text = text;
    p.len = len;
    p.error = error;
    p.cut = cut;
    p.outline = text_outline_new(text, len);
    p.open = g_ptr_array_new_with_free_func(section_free);
    p.array_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gboolean ok = parse_document(&p);
    if (ok) {
        TomlViewInfo result = { p.n_tables, p.n_keys, text_outline_level(p.outline, fold), p.at_cut };
        if (info != NULL) {
            *info = result;
        }
        if ((fold == NULL || fold->level == PREVIEW_FOLD_AUTO) && result.level != PREVIEW_FOLD_OPEN) {
            preview_doc_note(doc, _("Long document: the deeper levels are folded (Tab folds or opens a "
                                    "block, Shift+Tab changes the level)"));
        }
        text_outline_print(p.outline, doc, fold, result.level);
        if (cut) {
            preview_doc_append(doc, PREVIEW_STYLE_COMMENT, "…\n"); /* the rest was not read */
        }
    }
    text_outline_free(p.outline);
    g_ptr_array_unref(p.open);
    g_hash_table_destroy(p.array_counts);
    return ok;
}

gboolean toml_view_build(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                         const PreviewFold* fold, const gchar* note, GError** error) {
    g_autoptr(PreviewDoc) body = preview_doc_new();
    TomlViewInfo info;
    if (!toml_view_append(body, text, len, cut, fold, &info, error)) {
        return FALSE;
    }
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    if (info.n_tables > 0) {
        gchar* count = preview_doc_count(info.n_tables);
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM,
                            ngettext("TOML · %s table\n", "TOML · %s tables\n", info.n_tables), count);
        g_free(count);
    } else {
        gchar* count = preview_doc_count(info.n_keys);
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, ngettext("TOML · %s key\n", "TOML · %s keys\n", info.n_keys),
                            count);
        g_free(count);
    }
    if (note != NULL) {
        preview_doc_note(doc, note);
    }
    preview_doc_append_len(doc, "\n", 1);
    preview_doc_append_doc(doc, body); /* with its fold points and paths */
    return TRUE;
}
