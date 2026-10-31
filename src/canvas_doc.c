#include "canvas_doc.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_TEXT_WIDTH 300

/* Style names of the "s" attribute of <span>, in CanvasStyle bit order */
static const gchar* const STYLE_NAMES[] = {"b", "i", "u", "s", "code", "h"};

/* ------------------------------------------------------------------------- */
/* Blocks                                                                    */
/* ------------------------------------------------------------------------- */

static CanvasBlock* block_new(CanvasBlockKind kind, gint x, gint y, gint w, gint h) {
    CanvasBlock* b = g_new0(CanvasBlock, 1);
    b->kind = kind;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    if (kind == CANVAS_BLOCK_TEXT) {
        b->text = g_strdup("");
        b->runs = g_array_new(FALSE, FALSE, sizeof(CanvasRun));
    }
    return b;
}

static void block_free(gpointer data) {
    CanvasBlock* b = data;
    if (b == NULL) return;
    g_free(b->text);
    if (b->runs != NULL) g_array_unref(b->runs);
    g_free(b->src);
    g_free(b);
}

CanvasDoc* canvas_doc_new(void) {
    CanvasDoc* doc = g_new0(CanvasDoc, 1);
    doc->blocks = g_ptr_array_new_with_free_func(block_free);
    return doc;
}

void canvas_doc_free(CanvasDoc* doc) {
    if (doc == NULL) return;
    g_ptr_array_unref(doc->blocks);
    g_free(doc);
}

CanvasBlock* canvas_doc_add_text(CanvasDoc* doc, gint x, gint y, gint w, const gchar* text) {
    CanvasBlock* b = block_new(CANVAS_BLOCK_TEXT, x, y, w > 0 ? w : DEFAULT_TEXT_WIDTH, 0);
    if (text != NULL) {
        g_free(b->text);
        b->text = g_utf8_make_valid(text, -1);
    }
    g_ptr_array_add(doc->blocks, b);
    return b;
}

CanvasBlock* canvas_doc_add_image(CanvasDoc* doc, gint x, gint y, gint w, gint h, const gchar* src) {
    CanvasBlock* b = block_new(CANVAS_BLOCK_IMAGE, x, y, w, h);
    b->src = g_strdup(src != NULL ? src : "");
    g_ptr_array_add(doc->blocks, b);
    return b;
}

gboolean canvas_doc_remove(CanvasDoc* doc, CanvasBlock* block) {
    return g_ptr_array_remove(doc->blocks, block);
}

void canvas_doc_raise(CanvasDoc* doc, CanvasBlock* block) {
    guint i;
    if (!g_ptr_array_find(doc->blocks, block, &i)) return;
    g_ptr_array_steal_index(doc->blocks, i);
    g_ptr_array_add(doc->blocks, block);
}

void canvas_block_add_run(CanvasBlock* block, gint start, gint end, guint styles) {
    if (block == NULL || block->kind != CANVAS_BLOCK_TEXT) return;
    gint len = (gint)g_utf8_strlen(block->text, -1);
    start = CLAMP(start, 0, len);
    end = CLAMP(end, 0, len);
    if (start >= end || styles == 0) return;

    /* Styles of each character, then runs of equal styles again */
    guint* flags = g_new0(guint, len);
    for (guint i = 0; i < block->runs->len; i++) {
        CanvasRun* r = &g_array_index(block->runs, CanvasRun, i);
        for (gint c = MAX(r->start, 0); c < MIN(r->end, len); c++) flags[c] |= r->styles;
    }
    for (gint c = start; c < end; c++) flags[c] |= styles;

    g_array_set_size(block->runs, 0);
    gint c = 0;
    while (c < len) {
        gint s = c;
        while (c < len && flags[c] == flags[s]) c++;
        if (flags[s] != 0) {
            CanvasRun r = {s, c, flags[s]};
            g_array_append_val(block->runs, r);
        }
    }
    g_free(flags);
}

/* ------------------------------------------------------------------------- */
/* page.xml                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    CanvasDoc* doc;
    gboolean in_canvas;
    CanvasBlock* text_block; /* <text> being read */
    GString* text;
    gint n_chars;
    GArray* spans; /* open <span>s: CanvasRun with end unused */
} ParseState;

static const gchar* attr(const gchar** names, const gchar** values, const gchar* name) {
    for (gint i = 0; names[i] != NULL; i++) {
        if (strcmp(names[i], name) == 0) return values[i];
    }
    return NULL;
}

static gint attr_int(const gchar** names, const gchar** values, const gchar* name) {
    const gchar* v = attr(names, values, name);
    return (v != NULL) ? (gint)g_ascii_strtoll(v, NULL, 10) : 0;
}

static guint parse_styles(const gchar* s) {
    guint styles = 0;
    if (s == NULL) return 0;
    gchar** words = g_strsplit_set(s, " ,", -1);
    for (gint i = 0; words[i] != NULL; i++) {
        for (guint k = 0; k < G_N_ELEMENTS(STYLE_NAMES); k++) {
            if (strcmp(words[i], STYLE_NAMES[k]) == 0) styles |= 1u << k;
        }
    }
    g_strfreev(words);
    return styles;
}

static void on_start(GMarkupParseContext* ctx, const gchar* element, const gchar** names,
                     const gchar** values, gpointer data, GError** error) {
    (void)ctx;
    ParseState* st = data;
    if (!st->in_canvas) {
        if (strcmp(element, "canvas") != 0) {
            g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "not a canvas page");
            return;
        }
        st->in_canvas = TRUE;
        return;
    }
    if (st->text_block != NULL) {
        if (strcmp(element, "span") == 0) {
            CanvasRun r = {st->n_chars, 0, parse_styles(attr(names, values, "s"))};
            g_array_append_val(st->spans, r);
        }
        return;
    }
    gint x = attr_int(names, values, "x");
    gint y = attr_int(names, values, "y");
    gint w = attr_int(names, values, "w");
    gint h = attr_int(names, values, "h");
    if (strcmp(element, "text") == 0) {
        st->text_block = canvas_doc_add_text(st->doc, x, y, w, NULL);
        st->text_block->h = MAX(h, 0);
        g_string_truncate(st->text, 0);
        st->n_chars = 0;
    } else if (strcmp(element, "image") == 0) {
        const gchar* src = attr(names, values, "src");
        if (src != NULL && src[0] != '\0') {
            canvas_doc_add_image(st->doc, x, y, w, h, src);
        }
    }
}

static void on_end(GMarkupParseContext* ctx, const gchar* element, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    ParseState* st = data;
    if (st->text_block == NULL) return;
    if (strcmp(element, "span") == 0 && st->spans->len > 0) {
        CanvasRun r = g_array_index(st->spans, CanvasRun, st->spans->len - 1);
        g_array_set_size(st->spans, st->spans->len - 1);
        /* The runs refer to the text read so far */
        g_free(st->text_block->text);
        st->text_block->text = g_strdup(st->text->str);
        canvas_block_add_run(st->text_block, r.start, st->n_chars, r.styles);
    } else if (strcmp(element, "text") == 0) {
        g_free(st->text_block->text);
        st->text_block->text = g_strdup(st->text->str);
        st->text_block = NULL;
        g_array_set_size(st->spans, 0);
    }
}

static void on_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    ParseState* st = data;
    if (st->text_block == NULL) return;
    gchar* valid = g_utf8_make_valid(text, (gssize)len);
    g_string_append(st->text, valid);
    st->n_chars += (gint)g_utf8_strlen(valid, -1);
    g_free(valid);
}

CanvasDoc* canvas_doc_parse(const gchar* xml, gssize length, GError** error) {
    if (xml == NULL) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY, "empty canvas page");
        return NULL;
    }
    ParseState st = {0};
    st.doc = canvas_doc_new();
    st.text = g_string_new(NULL);
    st.spans = g_array_new(FALSE, FALSE, sizeof(CanvasRun));
    GMarkupParser parser = {on_start, on_end, on_text, NULL, NULL};
    GMarkupParseContext* ctx = g_markup_parse_context_new(&parser, 0, &st, NULL);
    gboolean ok = g_markup_parse_context_parse(ctx, xml, length, error) &&
                  g_markup_parse_context_end_parse(ctx, error);
    g_markup_parse_context_free(ctx);
    g_string_free(st.text, TRUE);
    g_array_unref(st.spans);
    if (ok && !st.in_canvas) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY, "empty canvas page");
        ok = FALSE;
    }
    if (!ok) {
        canvas_doc_free(st.doc);
        return NULL;
    }
    return st.doc;
}

static void append_escaped(GString* out, const gchar* start, const gchar* end) {
    gchar* esc = g_markup_escape_text(start, end - start);
    g_string_append(out, esc);
    g_free(esc);
}

static void append_styles(GString* out, guint styles) {
    gboolean first = TRUE;
    for (guint k = 0; k < G_N_ELEMENTS(STYLE_NAMES); k++) {
        if ((styles & (1u << k)) == 0) continue;
        if (!first) g_string_append_c(out, ' ');
        g_string_append(out, STYLE_NAMES[k]);
        first = FALSE;
    }
}

static void append_text_block(GString* out, const CanvasBlock* b) {
    g_string_append_printf(out, "  <text x=\"%d\" y=\"%d\" w=\"%d\"", b->x, b->y, b->w);
    if (b->h > 0) g_string_append_printf(out, " h=\"%d\"", b->h);
    g_string_append_c(out, '>');
    const gchar* text = b->text;
    gint len = (gint)g_utf8_strlen(text, -1);
    gint pos = 0;
    for (guint i = 0; i < b->runs->len; i++) {
        const CanvasRun* r = &g_array_index(b->runs, CanvasRun, i);
        gint start = CLAMP(r->start, pos, len);
        gint end = CLAMP(r->end, start, len);
        if (start == end) continue;
        append_escaped(out, g_utf8_offset_to_pointer(text, pos), g_utf8_offset_to_pointer(text, start));
        g_string_append(out, "<span s=\"");
        append_styles(out, r->styles);
        g_string_append(out, "\">");
        append_escaped(out, g_utf8_offset_to_pointer(text, start), g_utf8_offset_to_pointer(text, end));
        g_string_append(out, "</span>");
        pos = end;
    }
    append_escaped(out, g_utf8_offset_to_pointer(text, pos), text + strlen(text));
    g_string_append(out, "</text>\n");
}

gchar* canvas_doc_to_xml(const CanvasDoc* doc) {
    GString* out = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<canvas version=\"1\">\n");
    for (guint i = 0; doc != NULL && i < doc->blocks->len; i++) {
        const CanvasBlock* b = g_ptr_array_index(doc->blocks, i);
        if (b->kind == CANVAS_BLOCK_TEXT) {
            append_text_block(out, b);
        } else {
            gchar* line = g_markup_printf_escaped("  <image x=\"%d\" y=\"%d\" w=\"%d\" h=\"%d\" src=\"%s\"/>\n",
                                                  b->x, b->y, b->w, b->h, b->src);
            g_string_append(out, line);
            g_free(line);
        }
    }
    g_string_append(out, "</canvas>\n");
    return g_string_free(out, FALSE);
}

CanvasDoc* canvas_doc_load(const gchar* dir, GError** error) {
    if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "no canvas note at %s", dir);
        return NULL;
    }
    gchar* path = g_build_filename(dir, CANVAS_PAGE_FILE, NULL);
    gchar* xml = NULL;
    gsize len = 0;
    CanvasDoc* doc = NULL;
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        doc = canvas_doc_new();
    } else if (g_file_get_contents(path, &xml, &len, error)) {
        doc = canvas_doc_parse(xml, (gssize)len, error);
    }
    g_free(xml);
    g_free(path);
    return doc;
}

gboolean canvas_doc_save(const CanvasDoc* doc, const gchar* dir, GError** error) {
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "cannot create %s", dir);
        return FALSE;
    }
    gchar* path = g_build_filename(dir, CANVAS_PAGE_FILE, NULL);
    gchar* xml = canvas_doc_to_xml(doc);
    gboolean ok = g_file_set_contents(path, xml, -1, error);
    g_free(xml);
    g_free(path);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Media                                                                     */
/* ------------------------------------------------------------------------- */

guint canvas_doc_prune_media(const CanvasDoc* doc, const gchar* dir) {
    gchar* media = g_build_filename(dir, CANVAS_MEDIA_DIR, NULL);
    GDir* d = g_dir_open(media, 0, NULL);
    guint removed = 0;
    if (d != NULL) {
        GHashTable* used = g_hash_table_new(g_str_hash, g_str_equal);
        for (guint i = 0; i < doc->blocks->len; i++) {
            const CanvasBlock* b = g_ptr_array_index(doc->blocks, i);
            if (b->kind == CANVAS_BLOCK_IMAGE) g_hash_table_add(used, b->src);
        }
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* rel = g_strconcat(CANVAS_MEDIA_DIR "/", name, NULL);
            if (!g_hash_table_contains(used, rel)) {
                gchar* full = g_build_filename(media, name, NULL);
                if (g_file_test(full, G_FILE_TEST_IS_REGULAR) && g_remove(full) == 0) removed++;
                g_free(full);
            }
            g_free(rel);
        }
        g_hash_table_unref(used);
        g_dir_close(d);
    }
    g_free(media);
    return removed;
}

gchar* canvas_doc_new_media_path(const gchar* dir, const gchar* ext) {
    gchar* media = g_build_filename(dir, CANVAS_MEDIA_DIR, NULL);
    GDir* d = g_dir_open(media, 0, NULL);
    guint64 max = 0;
    if (d != NULL) {
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* end = NULL;
            guint64 n = g_ascii_strtoull(name, &end, 10);
            if (end != name && n > max) max = n;
        }
        g_dir_close(d);
    }
    g_free(media);
    const gchar* e = (ext != NULL && ext[0] != '\0') ? ext : "png";
    if (e[0] == '.') e++;
    return g_strdup_printf(CANVAS_MEDIA_DIR "/%04" G_GUINT64_FORMAT ".%s", max + 1, e);
}

/* ------------------------------------------------------------------------- */
/* Markdown export                                                           */
/* ------------------------------------------------------------------------- */

static gint compare_position(gconstpointer a, gconstpointer b) {
    const CanvasBlock* x = *(const CanvasBlock* const*)a;
    const CanvasBlock* y = *(const CanvasBlock* const*)b;
    if (x->y != y->y) return (x->y < y->y) ? -1 : 1;
    if (x->x != y->x) return (x->x < y->x) ? -1 : 1;
    return 0;
}

/* One line of a styled segment, wrapped in the Markdown markers of its styles */
static void append_md_line(GString* out, const gchar* start, const gchar* end, guint styles, gboolean line_start) {
    if (start == end) return;
    if ((styles & CANVAS_STYLE_HEADING) && line_start) g_string_append(out, "## ");
    const gchar* open = (styles & CANVAS_STYLE_CODE) ? "`" : "";
    GString* marks = g_string_new(NULL);
    if (!(styles & CANVAS_STYLE_CODE)) {
        if (styles & CANVAS_STYLE_BOLD) g_string_append(marks, "**");
        if (styles & CANVAS_STYLE_ITALIC) g_string_append(marks, "*");
        if (styles & CANVAS_STYLE_STRIKE) g_string_append(marks, "~~");
    }
    g_string_append(out, open);
    g_string_append(out, marks->str);
    g_string_append_len(out, start, end - start);
    g_string_append(out, g_strreverse(marks->str));
    g_string_append(out, open);
    g_string_free(marks, TRUE);
}

static void append_md_segment(GString* out, const gchar* start, const gchar* end, guint styles) {
    const gchar* p = start;
    while (p < end) {
        const gchar* nl = memchr(p, '\n', end - p);
        const gchar* line_end = (nl != NULL) ? nl : end;
        gboolean line_start = (out->len == 0 || out->str[out->len - 1] == '\n');
        append_md_line(out, p, line_end, styles, line_start);
        if (nl == NULL) break;
        g_string_append_c(out, '\n');
        p = nl + 1;
    }
}

static void append_md_text(GString* out, const CanvasBlock* b) {
    const gchar* text = b->text;
    gint len = (gint)g_utf8_strlen(text, -1);
    gint pos = 0;
    for (guint i = 0; i <= b->runs->len; i++) {
        const CanvasRun* r = (i < b->runs->len) ? &g_array_index(b->runs, CanvasRun, i) : NULL;
        gint start = (r != NULL) ? CLAMP(r->start, pos, len) : len;
        gint end = (r != NULL) ? CLAMP(r->end, start, len) : len;
        append_md_segment(out, g_utf8_offset_to_pointer(text, pos), g_utf8_offset_to_pointer(text, start), 0);
        if (r != NULL) {
            append_md_segment(out, g_utf8_offset_to_pointer(text, start), g_utf8_offset_to_pointer(text, end),
                              r->styles);
        }
        pos = end;
    }
}

gchar* canvas_doc_to_markdown(const CanvasDoc* doc, const gchar* media_prefix) {
    GPtrArray* sorted = g_ptr_array_new();
    for (guint i = 0; i < doc->blocks->len; i++) g_ptr_array_add(sorted, g_ptr_array_index(doc->blocks, i));
    g_ptr_array_sort(sorted, compare_position);

    GString* out = g_string_new(NULL);
    for (guint i = 0; i < sorted->len; i++) {
        const CanvasBlock* b = g_ptr_array_index(sorted, i);
        if (b->kind == CANVAS_BLOCK_TEXT && b->text[0] == '\0') continue;
        if (out->len > 0) g_string_append(out, "\n\n");
        if (b->kind == CANVAS_BLOCK_TEXT) {
            append_md_text(out, b);
        } else {
            gchar* link = g_strconcat(media_prefix != NULL ? media_prefix : "", b->src, NULL);
            /* A link with spaces goes between angle brackets */
            g_string_append_printf(out, strchr(link, ' ') != NULL ? "![](<%s>)" : "![](%s)", link);
            g_free(link);
        }
    }
    if (out->len > 0) g_string_append_c(out, '\n');
    g_ptr_array_unref(sorted);
    return g_string_free(out, FALSE);
}
