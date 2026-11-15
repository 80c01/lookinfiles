/* TOML preview (see toml_view.h). */
#include "formats/toml_view.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

/* A coloured range of the text, in bytes */
typedef struct {
    gsize start;
    gsize end;
    PreviewStyle style;
} Token;

/* A foldable block, in lines */
typedef struct {
    guint first;
    guint last;
    gchar* id;
    gint depth;           /* nesting: tables at the top are at depth 1 */
    gboolean table;       /* a table section, else an array or inline table */
    guint count;          /* tables: keys and subtables; arrays: items */
    gsize name_start;     /* tables: the name in the header, for the tagbar */
    gsize name_end;
} Block;

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
    GArray* line_starts;  /* gsize: offset of each line */
    GArray* tokens;       /* Token, by offset */
    GArray* blocks;       /* Block */
    GPtrArray* line_paths;/* gchar*: path of each line, or NULL */
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

/* Line of a byte offset */
static guint line_at(const Parser* p, gsize pos) {
    guint lo = 0;
    guint hi = p->line_starts->len;
    while (lo + 1 < hi) {
        guint mid = lo + (hi - lo) / 2;
        if (g_array_index(p->line_starts, gsize, mid) <= pos) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static void add_token(Parser* p, gsize start, gsize end, PreviewStyle style) {
    if (end > start) {
        Token token = { start, end, style };
        g_array_append_val(p->tokens, token);
    }
}

/* The path of the line holding pos, unless it has one already */
static void note_path(Parser* p, gsize pos, const gchar* path) {
    guint line = line_at(p, pos);
    if (g_ptr_array_index(p->line_paths, line) == NULL) {
        g_ptr_array_index(p->line_paths, line) = g_strdup(path);
    }
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

static void add_block(Parser* p, Block block) {
    g_array_append_val(p->blocks, block);
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
        add_block(p, (Block){ first, last, g_strdup(path), depth, FALSE, count, 0, 0 });
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
        g_array_index(p->blocks, Block, parent->block).count++;
    }
    guint line = line_at(p, start);
    note_path(p, start, path);
    add_block(p, (Block){ line, line, g_strdup(path), (gint)p->open->len + 1, TRUE, 0, name_start, name_end });
    Section* section = g_new0(Section, 1);
    section->parts = parts;
    section->path = path;
    section->block = p->blocks->len - 1;
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
        g_array_index(p->blocks, Block, section->block).count++;
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
            Block* block = &g_array_index(p->blocks, Block, ((Section*)g_ptr_array_index(p->open, i))->block);
            block->last = MAX(block->last, last);
        }
        if (p->at_cut) {
            break;
        }
        if (!end_statement(p)) {
            return FALSE;
        }
    }
    if (p->n_tables == 0 && p->n_keys == 0 && p->tokens->len == 0) {
        return fail(p, _("empty document"));
    }
    return TRUE;
}

/* --- Printing --- */

static gint compare_blocks(gconstpointer a, gconstpointer b) {
    const Block* x = a;
    const Block* y = b;
    if (x->first != y->first) {
        return x->first < y->first ? -1 : 1;
    }
    return x->depth - y->depth;
}

/* The level in effect: PREVIEW_FOLD_AUTO shows everything when it fits in
 * TOML_VIEW_MAX_LINES, else folds below the deepest level that fits */
static gint resolve_level(const Parser* p, guint n_lines, const PreviewFold* fold) {
    if (fold != NULL && fold->level != PREVIEW_FOLD_AUTO) {
        return fold->level;
    }
    if (n_lines < TOML_VIEW_MAX_LINES) {
        return PREVIEW_FOLD_OPEN;
    }
    gint max_depth = 0;
    for (guint i = 0; i < p->blocks->len; i++) {
        max_depth = MAX(max_depth, g_array_index(p->blocks, Block, i).depth);
    }
    for (gint level = max_depth; level > 1; level--) {
        guint64 hidden = 0; /* the blocks at this depth fold, and those inside them */
        for (guint i = 0; i < p->blocks->len; i++) {
            const Block* block = &g_array_index(p->blocks, Block, i);
            hidden += block->depth == level ? block->last - block->first : 0;
        }
        if (n_lines - hidden < TOML_VIEW_MAX_LINES) {
            return level;
        }
    }
    return 1;
}

typedef struct {
    PreviewDoc* doc;
    const Parser* p;
    guint token;          /* first token that may reach the next line */
} Printer;

/* Line i as written, coloured; the header name of an outline block tagged */
static void print_line(Printer* pr, guint i, const Block* header) {
    const Parser* p = pr->p;
    gsize start = g_array_index(p->line_starts, gsize, i);
    gsize end = i + 1 < p->line_starts->len ? g_array_index(p->line_starts, gsize, i + 1) : p->len;
    while (end > start && (p->text[end - 1] == '\n' || p->text[end - 1] == '\r')) end--;
    gint base = preview_doc_offset(pr->doc);
    preview_doc_append_len(pr->doc, p->text + start, (gssize)(end - start));
    while (pr->token < p->tokens->len && g_array_index(p->tokens, Token, pr->token).end <= start) {
        pr->token++;
    }
    for (guint t = pr->token; t < p->tokens->len; t++) {
        const Token* token = &g_array_index(p->tokens, Token, t);
        if (token->start >= end) {
            break;
        }
        gsize a = MAX(token->start, start);
        gsize b = MIN(token->end, end);
        if (b > a) {
            gint from = base + (gint)g_utf8_strlen(p->text + start, (gssize)(a - start));
            gint to = from + (gint)g_utf8_strlen(p->text + a, (gssize)(b - a));
            preview_doc_add_span(pr->doc, from, to, token->style);
        }
    }
    if (header != NULL && header->name_end > header->name_start) {
        gint from = base + (gint)g_utf8_strlen(p->text + start, (gssize)(header->name_start - start));
        gint to = from + (gint)g_utf8_strlen(p->text + header->name_start,
                                             (gssize)(header->name_end - header->name_start));
        preview_doc_add_span(pr->doc, from, to, PREVIEW_STYLE_OUTLINE1 + MIN(header->depth, 6) - 1);
    }
}

static void print_folded(PreviewDoc* doc, const Block* block) {
    gchar* count = block->table ? g_strdup_printf(ngettext("  %u key", "  %u keys", block->count), block->count)
                                : g_strdup_printf(ngettext("  %u item", "  %u items", block->count), block->count);
    gint start = preview_doc_offset(doc);
    preview_doc_append_len(doc, block->table ? "" : "…", -1);
    preview_doc_append_len(doc, count, -1);
    preview_doc_add_span(doc, start, preview_doc_offset(doc), PREVIEW_STYLE_COMMENT);
    g_free(count);
}

typedef struct {
    const Block* block;
    gint start;
} Pending;

static gboolean print_document(PreviewDoc* doc, const Parser* p, guint n_lines, const PreviewFold* fold,
                               gint level) {
    Printer pr = { doc, p, 0 };
    GArray* pending = g_array_new(FALSE, FALSE, sizeof(Pending)); /* open blocks shown */
    guint b = 0;
    guint shown = 0;
    gboolean stopped = FALSE;
    for (guint i = 0; i < n_lines;) {
        if (shown >= TOML_VIEW_MAX_LINES) {
            stopped = TRUE;
            break;
        }
        if (shown > 0) {
            preview_doc_append_len(doc, "\n", 1);
        }
        shown++;
        gint line_start = preview_doc_offset(doc);
        const gchar* path = g_ptr_array_index(p->line_paths, i);
        if (path != NULL) {
            preview_doc_add_path(doc, line_start, path);
        }
        const Block* folded = NULL;
        const Block* header = NULL;
        for (; b < p->blocks->len && g_array_index(p->blocks, Block, b).first == i; b++) {
            const Block* block = &g_array_index(p->blocks, Block, b);
            if (folded != NULL) {
                continue; /* inside the folded block */
            }
            header = block->table ? block : header;
            if (preview_fold_is_folded(fold, block->id, block->depth, level)) {
                folded = block;
            } else {
                Pending open = { block, line_start };
                g_array_append_val(pending, open);
            }
        }
        print_line(&pr, i, header);
        if (folded != NULL) {
            print_folded(doc, folded);
            preview_doc_add_fold(doc, line_start, preview_doc_offset(doc), folded->id, TRUE);
            i = folded->last + 1;
            while (b < p->blocks->len && g_array_index(p->blocks, Block, b).first < i) b++;
        } else {
            i++;
        }
        for (guint k = pending->len; k-- > 0;) {
            const Pending* open = &g_array_index(pending, Pending, k);
            if (open->block->last < i) {
                preview_doc_add_fold(doc, open->start, preview_doc_offset(doc), open->block->id, FALSE);
                g_array_remove_index(pending, k);
            }
        }
    }
    g_array_free(pending, TRUE);
    return stopped;
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
    p.line_starts = g_array_new(FALSE, FALSE, sizeof(gsize));
    p.tokens = g_array_new(FALSE, FALSE, sizeof(Token));
    p.blocks = g_array_new(FALSE, FALSE, sizeof(Block));
    p.line_paths = g_ptr_array_new_with_free_func(g_free);
    p.open = g_ptr_array_new_with_free_func(section_free);
    p.array_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (gsize i = 0; i <= len; i++) {
        if (i == 0 || text[i - 1] == '\n') {
            if (i == len && i > 0) break; /* no line after the final break */
            g_array_append_val(p.line_starts, i);
            g_ptr_array_add(p.line_paths, NULL);
        }
    }
    guint n_lines = p.line_starts->len;
    gboolean ok = parse_document(&p);
    if (ok) {
        g_array_sort(p.blocks, compare_blocks);
        TomlViewInfo result = { p.n_tables, p.n_keys, resolve_level(&p, n_lines, fold), p.at_cut };
        if (info != NULL) {
            *info = result;
        }
        if ((fold == NULL || fold->level == PREVIEW_FOLD_AUTO) && result.level != PREVIEW_FOLD_OPEN) {
            preview_doc_note(doc, _("Long document: the deeper levels are folded (Tab folds or opens a "
                                    "block, Shift+Tab changes the level)"));
        }
        gboolean stopped = print_document(doc, &p, n_lines, fold, result.level);
        preview_doc_append_len(doc, "\n", 1);
        if (cut) {
            preview_doc_append(doc, PREVIEW_STYLE_COMMENT, "…\n"); /* the rest was not read */
        }
        if (stopped) {
            gchar* note = g_strdup_printf(_("… (stopped after %u lines)"), TOML_VIEW_MAX_LINES);
            preview_doc_note(doc, note);
            g_free(note);
        }
    }
    for (guint i = 0; i < p.blocks->len; i++) {
        g_free(g_array_index(p.blocks, Block, i).id);
    }
    g_array_free(p.blocks, TRUE);
    g_array_free(p.tokens, TRUE);
    g_array_free(p.line_starts, TRUE);
    g_ptr_array_unref(p.line_paths);
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
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, ngettext("TOML · %s table\n", "TOML · %s tables\n", info.n_tables),
                            count);
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
