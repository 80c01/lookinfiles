/* A text shown as written with folds and paths (see text_outline.h). */
#include "formats/text_outline.h"
#include "core/i18n.h"

#include <string.h>

typedef struct {
    gsize start;
    gsize end;
    PreviewStyle style;
} Token;

struct TextOutline {
    const gchar* text;
    gsize len;
    GArray* line_starts;   /* gsize per line */
    GArray* tokens;        /* Token, by offset */
    GArray* headings;      /* Token: style = PREVIEW_STYLE_OUTLINE1..6 */
    GArray* blocks;        /* TextOutlineBlock */
    GPtrArray* line_paths; /* gchar* per line, or NULL */
};

TextOutline* text_outline_new(const gchar* text, gsize len) {
    TextOutline* o = g_new0(TextOutline, 1);
    o->text = text;
    o->len = len;
    o->line_starts = g_array_new(FALSE, FALSE, sizeof(gsize));
    o->tokens = g_array_new(FALSE, FALSE, sizeof(Token));
    o->headings = g_array_new(FALSE, FALSE, sizeof(Token));
    o->blocks = g_array_new(FALSE, FALSE, sizeof(TextOutlineBlock));
    o->line_paths = g_ptr_array_new_with_free_func(g_free);
    for (gsize i = 0; i <= len; i++) {
        if (i == 0 || text[i - 1] == '\n') {
            if (i == len && i > 0) {
                break; /* no line after the final break */
            }
            g_array_append_val(o->line_starts, i);
            g_ptr_array_add(o->line_paths, NULL);
        }
    }
    return o;
}

void text_outline_free(TextOutline* o) {
    if (o == NULL) {
        return;
    }
    for (guint i = 0; i < o->blocks->len; i++) {
        g_free(g_array_index(o->blocks, TextOutlineBlock, i).id);
    }
    g_array_free(o->blocks, TRUE);
    g_array_free(o->tokens, TRUE);
    g_array_free(o->headings, TRUE);
    g_array_free(o->line_starts, TRUE);
    g_ptr_array_unref(o->line_paths);
    g_free(o);
}

guint text_outline_n_lines(const TextOutline* o) {
    return o->line_starts->len;
}

guint text_outline_line_at(const TextOutline* o, gsize pos) {
    guint lo = 0;
    guint hi = o->line_starts->len;
    while (lo + 1 < hi) {
        guint mid = lo + (hi - lo) / 2;
        if (g_array_index(o->line_starts, gsize, mid) <= pos) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

gsize text_outline_line_start(const TextOutline* o, guint line) {
    return line < o->line_starts->len ? g_array_index(o->line_starts, gsize, line) : o->len;
}

void text_outline_add_token(TextOutline* o, gsize start, gsize end, PreviewStyle style) {
    if (end > start) {
        Token token = { start, end, style };
        g_array_append_val(o->tokens, token);
    }
}

void text_outline_set_path(TextOutline* o, gsize pos, const gchar* path) {
    guint line = text_outline_line_at(o, pos);
    if (line < o->line_paths->len && g_ptr_array_index(o->line_paths, line) == NULL) {
        g_ptr_array_index(o->line_paths, line) = g_strdup(path);
    }
}

void text_outline_add_heading(TextOutline* o, gsize start, gsize end, gint level) {
    if (end > start) {
        Token heading = { start, end, PREVIEW_STYLE_OUTLINE1 + CLAMP(level, 1, 6) - 1 };
        g_array_append_val(o->headings, heading);
    }
}

guint text_outline_add_block(TextOutline* o, guint first, guint last, const gchar* id, gint depth,
                             TextOutlineCount kind, guint count, gboolean ellipsis) {
    TextOutlineBlock block = { first, last, g_strdup(id), depth, kind, count, ellipsis };
    g_array_append_val(o->blocks, block);
    return o->blocks->len - 1;
}

TextOutlineBlock* text_outline_block(TextOutline* o, guint index) {
    return &g_array_index(o->blocks, TextOutlineBlock, index);
}

gint text_outline_level(TextOutline* o, const PreviewFold* fold) {
    if (fold != NULL && fold->level != PREVIEW_FOLD_AUTO) {
        return fold->level;
    }
    guint n_lines = o->line_starts->len;
    if (n_lines < TEXT_OUTLINE_MAX_LINES) {
        return PREVIEW_FOLD_OPEN;
    }
    gint max_depth = 0;
    for (guint i = 0; i < o->blocks->len; i++) {
        max_depth = MAX(max_depth, g_array_index(o->blocks, TextOutlineBlock, i).depth);
    }
    for (gint level = max_depth; level > 1; level--) {
        guint64 hidden = 0; /* the blocks at this depth fold, and those inside them */
        for (guint i = 0; i < o->blocks->len; i++) {
            const TextOutlineBlock* block = &g_array_index(o->blocks, TextOutlineBlock, i);
            hidden += block->depth == level ? block->last - block->first : 0;
        }
        if (n_lines - hidden < TEXT_OUTLINE_MAX_LINES) {
            return level;
        }
    }
    return 1;
}

static gint compare_blocks(gconstpointer a, gconstpointer b) {
    const TextOutlineBlock* x = a;
    const TextOutlineBlock* y = b;
    if (x->first != y->first) {
        return x->first < y->first ? -1 : 1;
    }
    return x->depth - y->depth;
}

static gint compare_tokens(gconstpointer a, gconstpointer b) {
    const Token* x = a;
    const Token* y = b;
    return x->start < y->start ? -1 : x->start > y->start;
}

/* Spans of the ranges of tokens (from *next on) that reach [start, end) */
static void apply_ranges(PreviewDoc* doc, const TextOutline* o, GArray* ranges, guint* next, gsize start,
                         gsize end, gint base) {
    while (*next < ranges->len && g_array_index(ranges, Token, *next).end <= start) {
        (*next)++;
    }
    for (guint t = *next; t < ranges->len; t++) {
        const Token* token = &g_array_index(ranges, Token, t);
        if (token->start >= end) {
            break;
        }
        gsize a = MAX(token->start, start);
        gsize b = MIN(token->end, end);
        if (b > a) {
            gint from = base + (gint)g_utf8_strlen(o->text + start, (gssize)(a - start));
            gint to = from + (gint)g_utf8_strlen(o->text + a, (gssize)(b - a));
            preview_doc_add_span(doc, from, to, token->style);
        }
    }
}

static void print_folded(PreviewDoc* doc, const TextOutlineBlock* block) {
    gchar* count = block->kind == TEXT_OUTLINE_KEYS
                       ? g_strdup_printf(ngettext("  %u key", "  %u keys", block->count), block->count)
                       : g_strdup_printf(ngettext("  %u item", "  %u items", block->count), block->count);
    gint start = preview_doc_offset(doc);
    preview_doc_append_len(doc, block->ellipsis ? "…" : "", -1);
    preview_doc_append_len(doc, count, -1);
    preview_doc_add_span(doc, start, preview_doc_offset(doc), PREVIEW_STYLE_COMMENT);
    g_free(count);
}

typedef struct {
    const TextOutlineBlock* block;
    gint start;
} Pending;

gboolean text_outline_print(TextOutline* o, PreviewDoc* doc, const PreviewFold* fold, gint level) {
    g_array_sort(o->blocks, compare_blocks);
    g_array_sort(o->headings, compare_tokens);
    GArray* pending = g_array_new(FALSE, FALSE, sizeof(Pending)); /* open blocks shown */
    guint next_token = 0;
    guint next_heading = 0;
    guint b = 0;
    guint shown = 0;
    gboolean stopped = FALSE;
    guint n_lines = o->line_starts->len;
    for (guint i = 0; i < n_lines;) {
        if (shown >= TEXT_OUTLINE_MAX_LINES) {
            stopped = TRUE;
            break;
        }
        shown++;
        gint line_start = preview_doc_offset(doc);
        const gchar* path = g_ptr_array_index(o->line_paths, i);
        if (path != NULL) {
            preview_doc_add_path(doc, line_start, path);
        }
        const TextOutlineBlock* folded = NULL;
        for (; b < o->blocks->len && g_array_index(o->blocks, TextOutlineBlock, b).first == i; b++) {
            const TextOutlineBlock* block = &g_array_index(o->blocks, TextOutlineBlock, b);
            if (folded != NULL) {
                continue; /* inside the folded block */
            }
            if (preview_fold_is_folded(fold, block->id, block->depth, level)) {
                folded = block;
            } else {
                Pending open = { block, line_start };
                g_array_append_val(pending, open);
            }
        }
        gsize start = g_array_index(o->line_starts, gsize, i);
        gsize end = i + 1 < n_lines ? g_array_index(o->line_starts, gsize, i + 1) : o->len;
        while (end > start && (o->text[end - 1] == '\n' || o->text[end - 1] == '\r')) end--;
        preview_doc_append_len(doc, o->text + start, (gssize)(end - start));
        apply_ranges(doc, o, o->tokens, &next_token, start, end, line_start);
        apply_ranges(doc, o, o->headings, &next_heading, start, end, line_start);
        if (folded != NULL) {
            print_folded(doc, folded);
            preview_doc_add_fold(doc, line_start, preview_doc_offset(doc), folded->id, TRUE);
            i = folded->last + 1;
            while (b < o->blocks->len && g_array_index(o->blocks, TextOutlineBlock, b).first < i) b++;
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
        preview_doc_append_len(doc, "\n", 1);
    }
    g_array_free(pending, TRUE);
    if (stopped) {
        gchar* note = g_strdup_printf(_("… (stopped after %u lines)"), TEXT_OUTLINE_MAX_LINES);
        preview_doc_note(doc, note);
        g_free(note);
    }
    return stopped;
}
