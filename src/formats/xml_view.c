/* XML pretty-printing for the preview (see xml_view.h). */
#include "formats/xml_view.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

typedef enum { X_ELEMENT, X_TEXT, X_COMMENT, X_CDATA, X_PI, X_DOCTYPE } XmlType;

typedef struct XmlNode XmlNode;
struct XmlNode {
    XmlType type;
    const gchar* text;    /* element: its start tag, '<' to '>'; text: trimmed; others: the markup */
    gsize len;
    const gchar* name;    /* element name as written (prefix included) */
    gsize name_len;
    GPtrArray* children;  /* elements: child nodes */
    gboolean empty_tag;   /* written <name/> */
    gboolean cut;         /* element that the end of a cut text left open */
};

#define MAX_DEPTH 256
#define INLINE_WIDTH 100  /* widest element kept on one line with its text */

typedef struct {
    const gchar* text;
    gsize len;
    gsize pos;
    GError** error;
    gboolean cut;         /* the text stops before the end of the file */
    gboolean at_cut;      /* parsing reached the end of the cut text */
    guint n_elements;
} Parser;

static void node_free(gpointer data) {
    XmlNode* node = data;
    if (node->children != NULL) {
        g_ptr_array_free(node->children, TRUE);
    }
    g_free(node);
}

static XmlNode* node_new(XmlType type, const gchar* text, gsize len) {
    XmlNode* node = g_new0(XmlNode, 1);
    node->type = type;
    node->text = text;
    node->len = len;
    return node;
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

/* At the end of a cut text: what is open stops here, without an error */
static gboolean at_cut(Parser* p) {
    if (p->cut && p->pos >= p->len) {
        p->at_cut = TRUE;
    }
    return p->at_cut;
}

static gboolean is_space(gchar c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static gboolean is_name_start(gchar c) {
    return g_ascii_isalpha(c) || c == '_' || c == ':' || (guchar)c >= 0x80;
}

static gboolean is_name_char(gchar c) {
    return is_name_start(c) || g_ascii_isdigit(c) || c == '-' || c == '.';
}

static gboolean starts_with(const Parser* p, const gchar* prefix) {
    gsize n = strlen(prefix);
    return p->pos + n <= p->len && memcmp(p->text + p->pos, prefix, n) == 0;
}

/* TRUE if blanks were skipped */
static gboolean skip_spaces(Parser* p) {
    gsize start = p->pos;
    while (p->pos < p->len && is_space(p->text[p->pos])) {
        p->pos++;
    }
    return p->pos > start;
}

static gsize scan_name(Parser* p) {
    gsize start = p->pos;
    if (p->pos < p->len && is_name_start(p->text[p->pos])) {
        while (p->pos < p->len && is_name_char(p->text[p->pos])) {
            p->pos++;
        }
    }
    return p->pos - start;
}

/* Move past the next end ("-->"); FALSE at the end of the text */
static gboolean skip_past(Parser* p, const gchar* end) {
    const gchar* at = g_strstr_len(p->text + p->pos, (gssize)(p->len - p->pos), end);
    if (at == NULL) {
        p->pos = p->len;
        return FALSE;
    }
    p->pos = (gsize)(at - p->text) + strlen(end);
    return TRUE;
}

/* <!DOCTYPE ...> and other declarations, with an internal subset [...] */
static gboolean skip_declaration(Parser* p) {
    gboolean subset = FALSE;
    gchar quote = 0;
    for (p->pos += 2; p->pos < p->len; p->pos++) {
        gchar c = p->text[p->pos];
        if (quote != 0) {
            quote = c == quote ? 0 : quote;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '[') {
            subset = TRUE;
        } else if (c == ']') {
            subset = FALSE;
        } else if (c == '>' && !subset) {
            p->pos++;
            return TRUE;
        }
    }
    return FALSE;
}

/* A start tag at '<': NULL on an error or at the cut */
static XmlNode* parse_start_tag(Parser* p) {
    gsize start = p->pos;
    p->pos++;
    gsize name_start = p->pos;
    gsize name_len = scan_name(p);
    if (name_len == 0) {
        if (!at_cut(p)) {
            fail(p, _("expected a tag name"));
        }
        return NULL;
    }
    gboolean empty_tag = FALSE;
    for (;;) {
        gboolean spaced = skip_spaces(p);
        if (at_cut(p)) {
            return NULL;
        }
        if (p->pos >= p->len) {
            fail(p, _("missing >"));
            return NULL;
        }
        gchar c = p->text[p->pos];
        if (c == '>') {
            p->pos++;
            break;
        }
        if (c == '/') {
            p->pos++;
            if (at_cut(p)) {
                return NULL;
            }
            if (p->pos < p->len && p->text[p->pos] == '>') {
                p->pos++;
                empty_tag = TRUE;
                break;
            }
            fail(p, _("expected />"));
            return NULL;
        }
        if (!spaced || scan_name(p) == 0) {
            fail(p, _("expected an attribute name"));
            return NULL;
        }
        skip_spaces(p);
        if (at_cut(p)) {
            return NULL;
        }
        if (p->pos >= p->len || p->text[p->pos] != '=') {
            fail(p, _("expected ="));
            return NULL;
        }
        p->pos++;
        skip_spaces(p);
        if (at_cut(p)) {
            return NULL;
        }
        gchar quote = p->pos < p->len ? p->text[p->pos] : 0;
        if (quote != '"' && quote != '\'') {
            fail(p, _("expected a quoted value"));
            return NULL;
        }
        const gchar* close = memchr(p->text + p->pos + 1, quote, p->len - p->pos - 1);
        if (close == NULL) {
            p->pos = p->len;
            if (!at_cut(p)) {
                fail(p, _("unterminated value"));
            }
            return NULL;
        }
        p->pos = (gsize)(close - p->text) + 1;
    }
    XmlNode* node = node_new(X_ELEMENT, p->text + start, p->pos - start);
    node->name = p->text + name_start;
    node->name_len = name_len;
    node->children = g_ptr_array_new_with_free_func(node_free);
    node->empty_tag = empty_tag;
    p->n_elements++;
    return node;
}

/* Markup that ends with end ("-->", "]]>", "?>") */
static XmlNode* parse_section(Parser* p, XmlType type, const gchar* end, const gchar* unterminated) {
    gsize start = p->pos;
    if (!skip_past(p, end)) {
        if (!at_cut(p)) {
            p->pos = start;
            fail(p, unterminated);
        }
        return NULL;
    }
    return node_new(type, p->text + start, p->pos - start);
}

static XmlNode* top_of(GPtrArray* open) {
    return open->len > 0 ? g_ptr_array_index(open, open->len - 1) : NULL;
}

/* An end tag at "</": pops the element it closes */
static gboolean parse_end_tag(Parser* p, GPtrArray* open) {
    gsize start = p->pos;
    p->pos += 2;
    gsize name_start = p->pos;
    gsize name_len = scan_name(p);
    skip_spaces(p);
    if (at_cut(p)) {
        return FALSE;
    }
    if (name_len == 0 || p->pos >= p->len || p->text[p->pos] != '>') {
        return fail(p, _("expected >"));
    }
    p->pos++;
    XmlNode* current = top_of(open);
    if (current == NULL || current->name_len != name_len ||
        memcmp(current->name, p->text + name_start, name_len) != 0) {
        p->pos = start;
        g_autofree gchar* name = g_strndup(current != NULL ? current->name : p->text + name_start,
                                           current != NULL ? current->name_len : name_len);
        g_autofree gchar* what = current != NULL ? g_strdup_printf(_("expected </%s>"), name)
                                                 : g_strdup_printf(_("unexpected </%s>"), name);
        return fail(p, what);
    }
    g_ptr_array_remove_index(open, open->len - 1);
    return TRUE;
}

/* The nodes of the document into top; FALSE with the error set */
static gboolean parse_document(Parser* p, GPtrArray* top) {
    GPtrArray* open = g_ptr_array_new(); /* elements not closed yet, innermost last */
    gboolean ok = TRUE;
    if (p->len >= 3 && memcmp(p->text, "\xEF\xBB\xBF", 3) == 0) {
        p->pos = 3;
    }
    while (ok && p->pos < p->len) {
        XmlNode* parent = top_of(open);
        GPtrArray* into = parent != NULL ? parent->children : top;
        XmlNode* node = NULL;
        if (p->text[p->pos] != '<') {
            /* Text up to the next tag, trimmed; blanks between tags are dropped */
            const gchar* lt = memchr(p->text + p->pos, '<', p->len - p->pos);
            gsize end = lt != NULL ? (gsize)(lt - p->text) : p->len;
            gsize start = p->pos;
            while (start < end && is_space(p->text[start])) start++;
            gsize stop = end;
            while (stop > start && is_space(p->text[stop - 1])) stop--;
            p->pos = end;
            if (stop > start && parent == NULL) {
                p->pos = start;
                ok = fail(p, _("text outside the root element"));
            } else if (stop > start) {
                node = node_new(X_TEXT, p->text + start, stop - start);
            }
        } else if (starts_with(p, "<!--")) {
            node = parse_section(p, X_COMMENT, "-->", _("unterminated comment"));
            ok = node != NULL || p->at_cut;
        } else if (starts_with(p, "<![CDATA[")) {
            node = parse_section(p, X_CDATA, "]]>", _("unterminated CDATA section"));
            ok = node != NULL || p->at_cut;
        } else if (starts_with(p, "<?")) {
            node = parse_section(p, X_PI, "?>", _("unterminated processing instruction"));
            ok = node != NULL || p->at_cut;
        } else if (starts_with(p, "<!")) {
            gsize start = p->pos;
            if (skip_declaration(p)) {
                node = node_new(X_DOCTYPE, p->text + start, p->pos - start);
            } else if (!at_cut(p)) {
                p->pos = start;
                ok = fail(p, _("missing >"));
            }
        } else if (starts_with(p, "</")) {
            ok = parse_end_tag(p, open) || p->at_cut;
        } else {
            node = parse_start_tag(p);
            ok = node != NULL || p->at_cut;
            if (node != NULL && !node->empty_tag) {
                if (open->len >= MAX_DEPTH) {
                    ok = fail(p, _("nested too deeply"));
                } else {
                    g_ptr_array_add(open, node);
                }
            }
        }
        if (node != NULL) {
            if (node->type == X_CDATA && parent == NULL) {
                p->pos = (gsize)(node->text - p->text);
                ok = fail(p, _("text outside the root element"));
                node_free(node); /* a section: never open */
            } else {
                g_ptr_array_add(into, node);
            }
        }
        if (p->at_cut) {
            break;
        }
    }
    if (ok && open->len > 0) {
        if (p->cut) {
            p->at_cut = TRUE;
            for (guint i = 0; i < open->len; i++) {
                ((XmlNode*)g_ptr_array_index(open, i))->cut = TRUE;
            }
        } else {
            XmlNode* current = top_of(open);
            g_autofree gchar* name = g_strndup(current->name, current->name_len);
            g_autofree gchar* what = g_strdup_printf(_("missing </%s>"), name);
            ok = fail(p, what);
        }
    }
    if (ok && p->n_elements == 0) {
        p->at_cut = FALSE;
        ok = fail(p, _("no XML element"));
    }
    g_ptr_array_free(open, TRUE);
    return ok;
}

/* --- Printing --- */

typedef struct {
    PreviewDoc* doc;
    guint lines;
    gboolean stopped;
    const PreviewFold* fold;
    gint level;          /* fold level in effect (PREVIEW_FOLD_AUTO resolved) */
    GString* path;       /* path of the element being printed ("/feed/entry[2]") */
    gint line_start;     /* offset of the start of the current line */
} Printer;

static void newline(Printer* pr, guint indent) {
    pr->lines++;
    if (pr->lines >= XML_VIEW_MAX_LINES) {
        pr->stopped = TRUE;
    }
    preview_doc_append_len(pr->doc, "\n", 1);
    pr->line_start = preview_doc_offset(pr->doc);
    for (guint i = 0; i < indent; i++) {
        preview_doc_append_len(pr->doc, "  ", 2);
    }
}

/* Text on the current line: line breaks and tabs become spaces */
static void append_flat(Printer* pr, const gchar* text, gsize len, PreviewStyle style) {
    gint start = preview_doc_offset(pr->doc);
    GString* flat = g_string_new_len(text, (gssize)len);
    for (gsize i = 0; i < flat->len; i++) {
        if (flat->str[i] == '\n' || flat->str[i] == '\r' || flat->str[i] == '\t') {
            flat->str[i] = ' ';
        }
    }
    preview_doc_append_len(pr->doc, flat->str, (gssize)flat->len);
    preview_doc_add_span(pr->doc, start, preview_doc_offset(pr->doc), style);
    g_string_free(flat, TRUE);
}

/* <name attr="value"...> or <name/>, coloured; outline: name and attributes
 * go to the tagbar */
static void print_start_tag(Printer* pr, const XmlNode* node, gboolean outline) {
    const gchar* s = node->text;
    gsize len = node->len;
    preview_doc_append_len(pr->doc, "<", 1);
    gint outline_start = preview_doc_offset(pr->doc);
    append_flat(pr, node->name, node->name_len, PREVIEW_STYLE_KEYWORD);
    gsize i = 1 + node->name_len;
    for (;;) {
        while (i < len && is_space(s[i])) i++;
        if (i >= len || s[i] == '/' || s[i] == '>') {
            break;
        }
        gsize name = i;
        while (i < len && is_name_char(s[i])) i++;
        preview_doc_append_len(pr->doc, " ", 1);
        append_flat(pr, s + name, i - name, PREVIEW_STYLE_KEY);
        while (i < len && s[i] != '"' && s[i] != '\'') i++; /* blanks and = */
        gsize value = i;
        const gchar* close = i < len ? memchr(s + i + 1, s[i], len - i - 1) : NULL;
        i = close != NULL ? (gsize)(close - s) + 1 : len;
        preview_doc_append_len(pr->doc, "=", 1);
        append_flat(pr, s + value, i - value, PREVIEW_STYLE_STRING);
    }
    if (outline) {
        preview_doc_add_span(pr->doc, outline_start, preview_doc_offset(pr->doc), PREVIEW_STYLE_OUTLINE1);
    }
    preview_doc_append_len(pr->doc, node->empty_tag ? "/>" : ">", -1);
}

static void print_end_tag(Printer* pr, const XmlNode* node) {
    if (node->cut) {
        preview_doc_append(pr->doc, PREVIEW_STYLE_COMMENT, "…"); /* the rest was not read */
        return;
    }
    preview_doc_append_len(pr->doc, "</", 2);
    append_flat(pr, node->name, node->name_len, PREVIEW_STYLE_KEYWORD);
    preview_doc_append_len(pr->doc, ">", 1);
}

/* Lines of a text, comment or section: raw keeps their blanks (CDATA),
 * else each line is trimmed and re-indented and blank ones are dropped */
static guint count_lines(const XmlNode* node) {
    gboolean raw = node->type == X_CDATA;
    guint n = 0;
    const gchar* s = node->text;
    const gchar* end = s + node->len;
    while (s < end) {
        const gchar* nl = memchr(s, '\n', (gsize)(end - s));
        const gchar* stop = nl != NULL ? nl : end;
        gboolean blank = TRUE;
        for (const gchar* c = s; c < stop && blank; c++) blank = is_space(*c);
        n += (raw || !blank) ? 1 : 0;
        s = nl != NULL ? nl + 1 : end;
    }
    return MAX(n, 1);
}

static void print_lines(Printer* pr, const XmlNode* node, guint indent) {
    PreviewStyle style = node->type == X_TEXT ? PREVIEW_STYLE_PLAIN
                         : node->type == X_CDATA ? PREVIEW_STYLE_STRING
                         : node->type == X_COMMENT ? PREVIEW_STYLE_COMMENT
                         : PREVIEW_STYLE_DIM;
    gboolean raw = node->type == X_CDATA;
    gboolean first = TRUE;
    const gchar* s = node->text;
    const gchar* end = s + node->len;
    while (s < end && !pr->stopped) {
        const gchar* nl = memchr(s, '\n', (gsize)(end - s));
        const gchar* stop = nl != NULL ? nl : end;
        const gchar* a = s;
        const gchar* b = stop;
        if (!raw) {
            while (a < b && is_space(*a)) a++;
            while (b > a && is_space(b[-1])) b--;
        } else if (b > a && b[-1] == '\r') {
            b--;
        }
        if (raw || b > a) {
            if (!first) {
                newline(pr, raw ? 0 : indent);
            }
            if (pr->path->len > 0) {
                preview_doc_add_path(pr->doc, pr->line_start, pr->path->str);
            }
            gint start = preview_doc_offset(pr->doc);
            preview_doc_append_len(pr->doc, a, (gssize)(b - a));
            preview_doc_add_span(pr->doc, start, preview_doc_offset(pr->doc), style);
            first = FALSE;
        }
        s = nl != NULL ? nl + 1 : end;
    }
}

/* An element holding only a short line of text stays on one line */
static gboolean fits_inline(const XmlNode* node) {
    if (node->children->len != 1 || node->cut) {
        return FALSE;
    }
    const XmlNode* child = g_ptr_array_index(node->children, 0);
    return (child->type == X_TEXT || child->type == X_CDATA) &&
           memchr(child->text, '\n', child->len) == NULL &&
           node->len + child->len + node->name_len + 3 <= INLINE_WIDTH;
}

/* An element written on several lines (the ones that fold) */
static gboolean is_block(const XmlNode* node) {
    return node->type == X_ELEMENT && !node->empty_tag && node->children->len > 0 && !fits_inline(node);
}

static guint count_elements(const XmlNode* node) {
    guint n = 0;
    for (guint i = 0; i < node->children->len; i++) {
        n += ((const XmlNode*)g_ptr_array_index(node->children, i))->type == X_ELEMENT;
    }
    return n;
}

static void print_folded(Printer* pr, const XmlNode* node, gboolean outline) {
    print_start_tag(pr, node, outline);
    preview_doc_append(pr->doc, PREVIEW_STYLE_COMMENT, "…");
    print_end_tag(pr, node);
    guint n = count_elements(node);
    if (n > 0) {
        gchar* count = g_strdup_printf(ngettext("  %u element", "  %u elements", n), n);
        preview_doc_append(pr->doc, PREVIEW_STYLE_COMMENT, count);
        g_free(count);
    }
}

/* The path steps of the elements in nodes: "/name", "/name[2]" when
 * several siblings share the name (free with g_ptr_array_unref()) */
static GPtrArray* path_steps(GPtrArray* nodes) {
    GHashTable* totals = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < nodes->len; i++) {
        const XmlNode* node = g_ptr_array_index(nodes, i);
        gchar* name = node->type == X_ELEMENT ? g_strndup(node->name, node->name_len) : NULL;
        g_ptr_array_add(names, name);
        if (name != NULL) {
            guint n = GPOINTER_TO_UINT(g_hash_table_lookup(totals, name));
            g_hash_table_insert(totals, g_strdup(name), GUINT_TO_POINTER(n + 1));
        }
    }
    GHashTable* seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GPtrArray* steps = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < nodes->len; i++) {
        const gchar* name = g_ptr_array_index(names, i);
        if (name == NULL) {
            g_ptr_array_add(steps, NULL);
            continue;
        }
        guint index = GPOINTER_TO_UINT(g_hash_table_lookup(seen, name)) + 1;
        g_hash_table_insert(seen, g_strdup(name), GUINT_TO_POINTER(index));
        g_ptr_array_add(steps, GPOINTER_TO_UINT(g_hash_table_lookup(totals, name)) > 1
                                   ? g_strdup_printf("/%s[%u]", name, index)
                                   : g_strdup_printf("/%s", name));
    }
    g_hash_table_destroy(seen);
    g_hash_table_destroy(totals);
    g_ptr_array_unref(names);
    return steps;
}

static void print_element(Printer* pr, const XmlNode* node, guint depth);

/* The nodes of an element (or of the document), one per line */
static void print_children(Printer* pr, GPtrArray* nodes, guint depth, gboolean first_on_line) {
    GPtrArray* steps = path_steps(nodes);
    gsize path_len = pr->path->len;
    for (guint i = 0; i < nodes->len && !pr->stopped; i++) {
        const XmlNode* child = g_ptr_array_index(nodes, i);
        if (i > 0 || !first_on_line) {
            newline(pr, depth);
        }
        if (child->type == X_ELEMENT) {
            g_string_append(pr->path, g_ptr_array_index(steps, i));
            print_element(pr, child, depth);
            g_string_truncate(pr->path, path_len);
        } else {
            print_lines(pr, child, depth);
        }
    }
    g_ptr_array_unref(steps);
}

static void print_element(Printer* pr, const XmlNode* node, guint depth) {
    if (pr->stopped) {
        return;
    }
    gint line_start = pr->line_start;
    gboolean outline = depth == 1;
    preview_doc_add_path(pr->doc, line_start, pr->path->str);
    if (node->empty_tag) {
        print_start_tag(pr, node, outline);
        return;
    }
    if (node->children->len == 0 || fits_inline(node)) {
        print_start_tag(pr, node, outline);
        if (node->children->len > 0) {
            const XmlNode* text = g_ptr_array_index(node->children, 0);
            append_flat(pr, text->text, text->len,
                        text->type == X_CDATA ? PREVIEW_STYLE_STRING : PREVIEW_STYLE_PLAIN);
        }
        print_end_tag(pr, node);
        return;
    }
    if (preview_fold_is_folded(pr->fold, pr->path->str, (gint)depth, pr->level)) {
        print_folded(pr, node, outline);
        preview_doc_add_fold(pr->doc, line_start, preview_doc_offset(pr->doc), pr->path->str, TRUE);
        return;
    }
    print_start_tag(pr, node, outline);
    print_children(pr, node->children, depth + 1, FALSE);
    if (!pr->stopped) {
        newline(pr, depth);
        preview_doc_add_path(pr->doc, pr->line_start, pr->path->str);
        print_end_tag(pr, node);
        preview_doc_add_fold(pr->doc, line_start, preview_doc_offset(pr->doc), pr->path->str, FALSE);
    }
}

/* Lines added by the blocks at each depth when they are open */
static void count_block_lines(const XmlNode* node, guint depth, GArray* per_depth) {
    if (!is_block(node)) {
        return;
    }
    if (per_depth->len <= depth) {
        g_array_set_size(per_depth, depth + 1);
    }
    guint64 lines = 1; /* the end tag */
    for (guint i = 0; i < node->children->len; i++) {
        const XmlNode* child = g_ptr_array_index(node->children, i);
        lines += child->type == X_ELEMENT ? 1 : count_lines(child);
        count_block_lines(child, depth + 1, per_depth);
    }
    g_array_index(per_depth, guint64, depth) += lines;
}

/* The level in effect: PREVIEW_FOLD_AUTO opens everything that fits in
 * XML_VIEW_MAX_LINES, so long documents open folded below some depth */
static gint resolve_level(GPtrArray* top, const PreviewFold* fold) {
    if (fold != NULL && fold->level != PREVIEW_FOLD_AUTO) {
        return fold->level;
    }
    GArray* per_depth = g_array_new(FALSE, TRUE, sizeof(guint64));
    guint64 base = 0;
    for (guint i = 0; i < top->len; i++) {
        const XmlNode* node = g_ptr_array_index(top, i);
        base += node->type == X_ELEMENT ? 1 : count_lines(node);
        count_block_lines(node, 0, per_depth);
    }
    guint64 total = base;
    for (guint d = 0; d < per_depth->len; d++) {
        total += g_array_index(per_depth, guint64, d);
    }
    gint level = PREVIEW_FOLD_OPEN;
    if (total >= XML_VIEW_MAX_LINES) {
        level = 1;
        guint64 lines = base;
        for (guint d = 0; d + 1 < per_depth->len; d++) {
            lines += g_array_index(per_depth, guint64, d);
            if (lines >= XML_VIEW_MAX_LINES) {
                break;
            }
            level = (gint)d + 1;
        }
    }
    g_array_free(per_depth, TRUE);
    return level;
}

gboolean xml_view_append(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                         const PreviewFold* fold, XmlViewInfo* info, GError** error) {
    Parser p = { text, len, 0, error, cut, FALSE, 0 };
    GPtrArray* top = g_ptr_array_new_with_free_func(node_free);
    if (!parse_document(&p, top)) {
        g_ptr_array_free(top, TRUE);
        return FALSE;
    }

    XmlViewInfo result = { 0 };
    result.n_elements = p.n_elements;
    for (guint i = 0; i < top->len; i++) {
        const XmlNode* node = g_ptr_array_index(top, i);
        if (node->type == X_ELEMENT) {
            g_strlcpy(result.root, node->name, MIN(node->name_len + 1, sizeof result.root));
            break;
        }
    }
    result.level = resolve_level(top, fold);
    result.cut = p.at_cut;
    if (info != NULL) {
        *info = result;
    }

    if ((fold == NULL || fold->level == PREVIEW_FOLD_AUTO) && result.level != PREVIEW_FOLD_OPEN) {
        preview_doc_note(doc, _("Long document: the deeper levels are folded (Tab folds or opens a "
                                "block, Shift+Tab changes the level)"));
    }
    Printer pr = { doc, 0, FALSE, fold, result.level, g_string_new(NULL), preview_doc_offset(doc) };
    print_children(&pr, top, 0, TRUE);
    preview_doc_append_len(doc, "\n", 1);
    if (pr.stopped) {
        gchar* note = g_strdup_printf(_("… (stopped after %u lines)"), XML_VIEW_MAX_LINES);
        preview_doc_note(doc, note);
        g_free(note);
    }
    g_string_free(pr.path, TRUE);
    g_ptr_array_free(top, TRUE);
    return TRUE;
}

gboolean xml_view_build(PreviewDoc* doc, const gchar* text, gsize len, gboolean cut,
                        const PreviewFold* fold, const gchar* note, GError** error) {
    g_autoptr(PreviewDoc) body = preview_doc_new();
    XmlViewInfo info;
    if (!xml_view_append(body, text, len, cut, fold, &info, error)) {
        return FALSE;
    }
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* count = preview_doc_count(info.n_elements);
    /* TRANSLATORS: %1$s is the name of the outer element, %2$s the number of elements */
    preview_doc_appendf(doc, PREVIEW_STYLE_DIM,
                        ngettext("XML · <%1$s> · %2$s element\n", "XML · <%1$s> · %2$s elements\n",
                                 info.n_elements),
                        info.root, count);
    g_free(count);
    if (note != NULL) {
        preview_doc_note(doc, note);
    }
    preview_doc_append_len(doc, "\n", 1);
    preview_doc_append_doc(doc, body); /* with its fold points and paths */
    return TRUE;
}
