/* Outline entries and their tree (see code_outline_tree.h) */
#include "code_outline_tree.h"
#include <string.h>

CodeOutlineEntry* code_outline_entry_new(const gchar* name, const gchar* detail, const gchar* container,
                                         CodeSymbolKind kind, gint line, gint column) {
    CodeOutlineEntry* e = g_new0(CodeOutlineEntry, 1);
    e->name = g_strdup(name != NULL ? name : "");
    e->detail = detail != NULL && detail[0] != '\0' ? g_strdup(detail) : NULL;
    e->container = container != NULL && container[0] != '\0' ? g_strdup(container) : NULL;
    e->kind = kind;
    e->line = line;
    e->column = column;
    return e;
}

void code_outline_entry_free(CodeOutlineEntry* entry) {
    if (entry == NULL) return;
    g_free(entry->name);
    g_free(entry->detail);
    g_free(entry->container);
    g_free(entry);
}

GPtrArray* code_outline_entries_new(void) {
    return g_ptr_array_new_with_free_func((GDestroyNotify)code_outline_entry_free);
}

static gint compare_entries(gconstpointer a, gconstpointer b) {
    const CodeOutlineEntry* x = *(const CodeOutlineEntry* const*)a;
    const CodeOutlineEntry* y = *(const CodeOutlineEntry* const*)b;
    return x->line != y->line ? (x->line < y->line ? -1 : 1) : (x->column > y->column) - (x->column < y->column);
}

void code_outline_sort(GPtrArray* entries) {
    g_ptr_array_sort(entries, compare_entries);
}

/* ------------------------------------------------------------------------- */
/* Markdown headings                                                         */
/* ------------------------------------------------------------------------- */

/* Leading spaces of a line (tabs count as 4) */
static gsize indent_of(const gchar* p, const gchar* end) {
    gsize n = 0;
    while (p < end && (*p == ' ' || *p == '\t')) {
        n += *p == '\t' ? 4 : 1;
        p++;
    }
    return n;
}

static const gchar* skip_blanks(const gchar* p, const gchar* end) {
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static gboolean is_blank(const gchar* p, const gchar* end) {
    return skip_blanks(p, end) == end;
}

/* A fence line (``` or ~~~, 3 or more): its character and length */
static gboolean fence_of(const gchar* p, const gchar* end, gchar* ch, gsize* count) {
    if (indent_of(p, end) > 3) return FALSE;
    p = skip_blanks(p, end);
    if (p >= end || (*p != '`' && *p != '~')) return FALSE;
    gsize n = 0;
    while (p + n < end && p[n] == *p) n++;
    if (n < 3) return FALSE;
    *ch = *p;
    *count = n;
    return TRUE;
}

/* "# Title #": level and text range, FALSE when the line is no ATX heading */
static gint atx_heading(const gchar* p, const gchar* end, const gchar** start, const gchar** stop) {
    if (indent_of(p, end) > 3) return 0;
    p = skip_blanks(p, end);
    gint level = 0;
    while (p < end && *p == '#' && level < 7) {
        level++;
        p++;
    }
    if (level == 0 || level > 6 || (p < end && *p != ' ' && *p != '\t')) return 0;
    p = skip_blanks(p, end);
    const gchar* e = end;
    while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
    /* Closing sequence: #s after a space (or the whole text) */
    const gchar* h = e;
    while (h > p && h[-1] == '#') h--;
    if (h < e && (h == p || h[-1] == ' ' || h[-1] == '\t')) {
        e = h;
        while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
    }
    *start = p;
    *stop = e;
    return level;
}

/* "====" (1) or "----" (2) alone on a line: Setext underline level, else 0 */
static gint setext_level(const gchar* p, const gchar* end) {
    if (indent_of(p, end) > 3) return 0;
    p = skip_blanks(p, end);
    if (p >= end || (*p != '=' && *p != '-')) return 0;
    gchar c = *p;
    while (p < end && *p == c) p++;
    if (!is_blank(p, end)) return 0;
    return c == '=' ? 1 : 2;
}

/* A line that cannot be the text of a Setext heading (list item, quote,
 * table, code) */
static gboolean is_paragraph_text(const gchar* p, const gchar* end) {
    if (is_blank(p, end) || indent_of(p, end) > 3) return FALSE;
    p = skip_blanks(p, end);
    if (*p == '>' || *p == '|' || *p == '<') return FALSE;
    if ((*p == '-' || *p == '*' || *p == '+') && (p + 1 == end || p[1] == ' ' || p[1] == '\t')) return FALSE;
    const gchar* d = p;
    while (d < end && g_ascii_isdigit(*d)) d++;
    if (d > p && d < end && (*d == '.' || *d == ')')) return FALSE;
    return TRUE;
}

gint code_outline_heading_tag_level(const gchar* tag_name) {
    static const gchar* const prefixes[] = {"md_h", "html_h", "org_h", "pd_h"};
    if (tag_name == NULL) return 0;
    for (gsize i = 0; i < G_N_ELEMENTS(prefixes); i++) {
        if (!g_str_has_prefix(tag_name, prefixes[i])) continue;
        const gchar* d = tag_name + strlen(prefixes[i]);
        if (d[0] >= '1' && d[0] <= '6' && d[1] == '\0') return d[0] - '0';
    }
    return 0;
}

CodeOutlineEntry* code_outline_heading_new(const gchar* text, gint level, gint line) {
    if (text == NULL) return NULL;
    gchar* valid = g_utf8_make_valid(text, -1);
    GString* name = g_string_new(NULL);
    gboolean space = FALSE;
    for (const gchar* p = valid; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isspace(c)) {
            space = name->len > 0;
            continue;
        }
        if (space) g_string_append_c(name, ' ');
        space = FALSE;
        g_string_append_unichar(name, c);
    }
    g_free(valid);
    CodeOutlineEntry* e = NULL;
    if (name->len > 0) {
        e = code_outline_entry_new(name->str, NULL, NULL, CODE_SYM_MODULE, line, 0);
        e->level = level;
    }
    g_string_free(name, TRUE);
    return e;
}

static void add_heading(GPtrArray* out, const gchar* text, const gchar* start, const gchar* stop, gint level,
                        gint line) {
    if (stop <= start) return;
    gchar* name = g_strndup(start, (gsize)(stop - start));
    if (!g_utf8_validate(name, -1, NULL)) {
        gchar* valid = g_utf8_make_valid(name, -1);
        g_free(name);
        name = valid;
    }
    const gchar* line_start = start;
    while (line_start > text && line_start[-1] != '\n') line_start--;
    CodeOutlineEntry* e = code_outline_entry_new(name, NULL, NULL, CODE_SYM_MODULE, line, (gint)(start - line_start));
    e->level = level;
    g_ptr_array_add(out, e);
    g_free(name);
}

GPtrArray* code_outline_markdown(const gchar* text, gsize len) {
    GPtrArray* out = code_outline_entries_new();
    if (text == NULL) return out;
    const gchar* end = text + len;
    const gchar* p = text;
    gint line = 0;
    gchar fence = 0;
    gsize fence_len = 0;
    gboolean front_matter = FALSE;
    /* The previous line, when it may be a Setext heading's text */
    const gchar* prev = NULL;
    const gchar* prev_end = NULL;
    while (p < end) {
        const gchar* nl = memchr(p, '\n', (gsize)(end - p));
        const gchar* le = nl != NULL ? nl : end;
        if (le > p && le[-1] == '\r') le--;
        gchar ch;
        gsize count;
        gboolean candidate = FALSE;
        if (line == 0 && le - p == 3 && strncmp(p, "---", 3) == 0) {
            front_matter = TRUE;
        } else if (front_matter) {
            if ((le - p == 3 && (strncmp(p, "---", 3) == 0 || strncmp(p, "...", 3) == 0))) front_matter = FALSE;
        } else if (fence != 0) {
            if (fence_of(p, le, &ch, &count) && ch == fence && count >= fence_len &&
                is_blank(skip_blanks(p, le) + count, le)) {
                fence = 0;
            }
        } else if (fence_of(p, le, &ch, &count)) {
            fence = ch;
            fence_len = count;
        } else {
            const gchar *start, *stop;
            gint level = atx_heading(p, le, &start, &stop);
            gint under = prev != NULL ? setext_level(p, le) : 0;
            if (level > 0) {
                add_heading(out, text, start, stop, level, line);
            } else if (under > 0) {
                const gchar* s = skip_blanks(prev, prev_end);
                const gchar* e = prev_end;
                while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
                add_heading(out, text, s, e, under, line - 1);
            } else {
                candidate = is_paragraph_text(p, le);
            }
        }
        prev = candidate ? p : NULL;
        prev_end = le;
        line++;
        p = nl != NULL ? nl + 1 : end;
    }
    return out;
}

/* ------------------------------------------------------------------------- */
/* Tree                                                                      */
/* ------------------------------------------------------------------------- */

static CodeOutlineNode* node_new(gint entry, CodeSymbolKind group) {
    CodeOutlineNode* n = g_new0(CodeOutlineNode, 1);
    n->entry = entry;
    n->group = group;
    n->children = g_ptr_array_new_with_free_func((GDestroyNotify)code_outline_node_free);
    return n;
}

void code_outline_node_free(CodeOutlineNode* node) {
    if (node == NULL) return;
    g_ptr_array_unref(node->children);
    g_free(node);
}

/* Order of the groups */
static const CodeSymbolKind GROUP_ORDER[] = {
    CODE_SYM_MODULE, CODE_SYM_CLASS, CODE_SYM_TYPE,     CODE_SYM_FUNCTION, CODE_SYM_METHOD,
    CODE_SYM_MACRO,  CODE_SYM_CONSTANT, CODE_SYM_VARIABLE, CODE_SYM_FIELD,  CODE_SYM_TARGET,
};

const gchar* code_outline_group_label(CodeSymbolKind kind) {
    switch (kind) {
    case CODE_SYM_FUNCTION: return "Functions";
    case CODE_SYM_METHOD: return "Methods";
    case CODE_SYM_MACRO: return "Macros";
    case CODE_SYM_TYPE: return "Types";
    case CODE_SYM_CLASS: return "Classes";
    case CODE_SYM_VARIABLE: return "Variables";
    case CODE_SYM_CONSTANT: return "Constants";
    case CODE_SYM_FIELD: return "Fields";
    case CODE_SYM_MODULE: return "Modules";
    case CODE_SYM_TARGET: return "Targets";
    }
    return "Symbols";
}

/* Index of the parent of entry i, or -1 */
static gint parent_of(const GPtrArray* entries, guint i) {
    const CodeOutlineEntry* e = g_ptr_array_index(entries, i);
    for (gint j = (gint)i - 1; j >= 0; j--) {
        const CodeOutlineEntry* p = g_ptr_array_index(entries, j);
        if (e->level > 0) {
            if (p->level > 0 && p->level < e->level) return j;
        } else if (e->container != NULL && p->level == 0 && strcmp(p->name, e->container) == 0) {
            return j;
        }
    }
    return -1;
}

GPtrArray* code_outline_tree(const GPtrArray* entries, CodeOutlineSort sort) {
    GPtrArray* roots = g_ptr_array_new_with_free_func((GDestroyNotify)code_outline_node_free);
    guint n = entries != NULL ? entries->len : 0;
    if (n == 0) return roots;
    /* Nodes owned by their parent (or by top until grouped) */
    CodeOutlineNode** nodes = g_new0(CodeOutlineNode*, n);
    GPtrArray* top = g_ptr_array_new();
    for (guint i = 0; i < n; i++) {
        nodes[i] = node_new((gint)i, 0);
        gint parent = parent_of(entries, i);
        if (parent >= 0) {
            g_ptr_array_add(nodes[parent]->children, nodes[i]);
        } else {
            g_ptr_array_add(top, nodes[i]);
        }
    }
    if (sort == CODE_OUTLINE_BY_LINE) {
        for (guint i = 0; i < top->len; i++) g_ptr_array_add(roots, g_ptr_array_index(top, i));
    } else {
        /* Headings first, as they are (a Markdown file has no symbols) */
        for (guint i = 0; i < top->len; i++) {
            CodeOutlineNode* node = g_ptr_array_index(top, i);
            const CodeOutlineEntry* e = g_ptr_array_index(entries, node->entry);
            if (e->level > 0) g_ptr_array_add(roots, node);
        }
        for (gsize k = 0; k < G_N_ELEMENTS(GROUP_ORDER); k++) {
            CodeOutlineNode* group = NULL;
            for (guint i = 0; i < top->len; i++) {
                CodeOutlineNode* node = g_ptr_array_index(top, i);
                const CodeOutlineEntry* e = g_ptr_array_index(entries, node->entry);
                if (e->level > 0 || e->kind != GROUP_ORDER[k]) continue;
                if (group == NULL) {
                    group = node_new(-1, GROUP_ORDER[k]);
                    g_ptr_array_add(roots, group);
                }
                g_ptr_array_add(group->children, node);
            }
        }
    }
    g_ptr_array_unref(top);
    g_free(nodes);
    return roots;
}

gint code_outline_entry_at_line(const GPtrArray* entries, gint line) {
    gint best = -1;
    for (guint i = 0; entries != NULL && i < entries->len; i++) {
        const CodeOutlineEntry* e = g_ptr_array_index(entries, i);
        if (e->line > line) break;
        best = (gint)i;
    }
    return best;
}
