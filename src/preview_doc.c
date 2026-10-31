/* Styled preview documents built in worker threads (see preview_doc.h). */
#include "preview_doc.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

PreviewDoc* preview_doc_new(void) {
    PreviewDoc* doc = g_new0(PreviewDoc, 1);
    doc->format = PREVIEW_DOC_TEXT;
    doc->text = g_string_new(NULL);
    doc->spans = g_array_new(FALSE, FALSE, sizeof(PreviewSpan));
    doc->images = g_array_new(FALSE, FALSE, sizeof(PreviewImage));
    doc->wrap = TRUE;
    doc->label_width = PREVIEW_DOC_LABEL_WIDTH;
    return doc;
}

PreviewDoc* preview_doc_new_source(PreviewDocFormat format, gchar* source) {
    PreviewDoc* doc = preview_doc_new();
    doc->format = format;
    g_string_assign(doc->text, source != NULL ? source : "");
    doc->n_chars = (gint)g_utf8_strlen(doc->text->str, (gssize)doc->text->len);
    g_free(source);
    return doc;
}

void preview_doc_free(PreviewDoc* doc) {
    if (doc == NULL) {
        return;
    }
    for (guint i = 0; i < doc->spans->len; i++) {
        g_free(g_array_index(doc->spans, PreviewSpan, i).arg);
    }
    for (guint i = 0; i < doc->images->len; i++) {
        g_bytes_unref(g_array_index(doc->images, PreviewImage, i).data);
    }
    g_array_free(doc->spans, TRUE);
    g_array_free(doc->images, TRUE);
    if (doc->font_files != NULL) {
        g_ptr_array_free(doc->font_files, TRUE);
    }
    g_string_free(doc->text, TRUE);
    g_free(doc);
}

gint preview_doc_offset(const PreviewDoc* doc) {
    return doc->n_chars;
}

void preview_doc_append_len(PreviewDoc* doc, const gchar* text, gssize len) {
    if (text == NULL) {
        return;
    }
    gsize n = len < 0 ? strlen(text) : (gsize)len;
    if (n == 0) {
        return;
    }
    if (g_utf8_validate_len(text, n, NULL)) {
        g_string_append_len(doc->text, text, (gssize)n);
        doc->n_chars += (gint)g_utf8_strlen(text, (gssize)n);
    } else {
        gchar* valid = g_utf8_make_valid(text, (gssize)n);
        g_string_append(doc->text, valid);
        doc->n_chars += (gint)g_utf8_strlen(valid, -1);
        g_free(valid);
    }
}

void preview_doc_append(PreviewDoc* doc, PreviewStyle style, const gchar* text) {
    gint start = doc->n_chars;
    preview_doc_append_len(doc, text, -1);
    preview_doc_add_span(doc, start, doc->n_chars, style);
}

void preview_doc_appendf(PreviewDoc* doc, PreviewStyle style, const gchar* format, ...) {
    va_list args;
    va_start(args, format);
    gchar* text = g_strdup_vprintf(format, args);
    va_end(args);
    preview_doc_append(doc, style, text);
    g_free(text);
}

void preview_doc_add_span(PreviewDoc* doc, gint start, gint end, PreviewStyle style) {
    if (style == PREVIEW_STYLE_PLAIN || style >= PREVIEW_STYLE_COUNT || end <= start) {
        return;
    }
    PreviewSpan span = { start, end, style, NULL };
    g_array_append_val(doc->spans, span);
}

void preview_doc_add_arg_span(PreviewDoc* doc, gint start, gint end, PreviewStyle style, const gchar* arg) {
    if (end <= start || arg == NULL || style == PREVIEW_STYLE_PLAIN || style >= PREVIEW_STYLE_COUNT) {
        return;
    }
    PreviewSpan span = { start, end, style, g_strdup(arg) };
    g_array_append_val(doc->spans, span);
}

void preview_doc_add_font_span(PreviewDoc* doc, gint start, gint end, const gchar* description) {
    preview_doc_add_arg_span(doc, start, end, PREVIEW_STYLE_FONT, description);
}

void preview_doc_add_paragraph_span(PreviewDoc* doc, gint start, gint end, const PreviewParagraph* para) {
    gchar* arg = g_strdup_printf("%d %d %d %d %d %d", (gint)para->align, para->left, para->right, para->indent,
                                 para->above, para->below);
    preview_doc_add_arg_span(doc, start, end, PREVIEW_STYLE_PARAGRAPH, arg);
    g_free(arg);
}

gboolean preview_paragraph_parse(const gchar* arg, PreviewParagraph* para) {
    gint align = 0;
    memset(para, 0, sizeof *para);
    if (arg == NULL || sscanf(arg, "%d %d %d %d %d %d", &align, &para->left, &para->right, &para->indent,
                              &para->above, &para->below) != 6 ||
        align < PREVIEW_ALIGN_LEFT || align > PREVIEW_ALIGN_JUSTIFY) {
        return FALSE;
    }
    para->align = (PreviewAlign)align;
    return TRUE;
}

void preview_doc_append_doc(PreviewDoc* doc, const PreviewDoc* other) {
    gint base = doc->n_chars;
    g_string_append_len(doc->text, other->text->str, (gssize)other->text->len);
    doc->n_chars += other->n_chars;
    for (guint i = 0; i < other->spans->len; i++) {
        PreviewSpan span = g_array_index(other->spans, PreviewSpan, i);
        span.start += base;
        span.end += base;
        span.arg = g_strdup(span.arg);
        g_array_append_val(doc->spans, span);
    }
    for (guint i = 0; i < other->images->len; i++) {
        PreviewImage image = g_array_index(other->images, PreviewImage, i);
        image.offset += base;
        g_bytes_ref(image.data);
        g_array_append_val(doc->images, image);
    }
    for (guint i = 0; other->font_files != NULL && i < other->font_files->len; i++) {
        preview_doc_add_font_file(doc, g_ptr_array_index(other->font_files, i));
    }
}

gchar* preview_symbol_text(const gchar* text, const gchar* font) {
    if (text == NULL || text[0] == '\0') {
        return g_strdup("•");
    }
    gunichar c = g_utf8_get_char_validated(text, -1);
    if (c == (gunichar)-1 || c == (gunichar)-2) {
        return g_strdup("•");
    }
    gboolean symbol = font != NULL && (g_ascii_strncasecmp(font, "Symbol", 6) == 0 ||
                                       g_ascii_strncasecmp(font, "Wingdings", 9) == 0);
    if (c >= 0xF000 && c <= 0xF0FF) {
        c -= 0xF000;
        symbol = TRUE;
    }
    if (symbol) {
        static const struct { gunichar code; const gchar* text; } SYMBOLS[] = {
            { 0xB7, "•" }, { 0xA7, "▪" }, { 0x6E, "■" }, { 0x71, "❑" }, { 0x76, "❖" },
            { 0xD8, "➢" }, { 0xFC, "✓" }, { 0xA8, "◆" }, { 0x6C, "●" }, { 0x77, "◆" },
        };
        for (gsize i = 0; i < G_N_ELEMENTS(SYMBOLS); i++) {
            if (c == SYMBOLS[i].code) {
                return g_strdup(SYMBOLS[i].text);
            }
        }
        return g_strdup("•");
    }
    if (c == 'o' && font != NULL && g_ascii_strncasecmp(font, "Courier", 7) == 0) {
        return g_strdup("◦");
    }
    return g_strdup(text);
}

/* --- Document colours --- */

gchar* preview_color_format(guint32 rgb) {
    return g_strdup_printf("#%02x%02x%02x", (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

gboolean preview_color_parse(const gchar* text, guint32* rgb) {
    if (text == NULL) {
        return FALSE;
    }
    if (text[0] == '#') {
        text++;
    }
    guint32 v = 0;
    for (gint i = 0; i < 6; i++) {
        gint d = g_ascii_xdigit_value(text[i]);
        if (d < 0) {
            return FALSE;
        }
        v = (v << 4) | (guint32)d;
    }
    if (text[6] != '\0') {
        return FALSE;
    }
    *rgb = v;
    return TRUE;
}

static gdouble channel_linear(guint32 c) {
    gdouble v = (gdouble)(c & 0xFF) / 255.0;
    return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

static gdouble luminance(guint32 rgb) {
    return 0.2126 * channel_linear(rgb >> 16) + 0.7152 * channel_linear(rgb >> 8) + 0.0722 * channel_linear(rgb);
}

gdouble preview_color_contrast(guint32 a, guint32 b) {
    gdouble la = luminance(a), lb = luminance(b);
    return (MAX(la, lb) + 0.05) / (MIN(la, lb) + 0.05);
}

/* Mix of a and b: t = 0 gives a, t = 1 gives b */
static guint32 mix(guint32 a, guint32 b, gdouble t) {
    guint32 out = 0;
    for (gint shift = 16; shift >= 0; shift -= 8) {
        gdouble ca = (gdouble)((a >> shift) & 0xFF), cb = (gdouble)((b >> shift) & 0xFF);
        guint32 c = (guint32)CLAMP(ca + (cb - ca) * t + 0.5, 0.0, 255.0);
        out |= c << shift;
    }
    return out;
}

#define READABLE_CONTRAST 4.5

guint32 preview_color_readable(guint32 color, guint32 background, guint32 text) {
    if (preview_color_contrast(color, background) >= READABLE_CONTRAST) {
        return color;
    }
    guint32 r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    guint32 hi = MAX(r, MAX(g, b)), lo = MIN(r, MIN(g, b));
    if (hi - lo < 40) {
        return text; /* black on a dark theme, white on a light one */
    }
    /* Towards white on a dark background, towards black on a light one */
    guint32 target = luminance(background) < 0.4 ? 0xFFFFFFu : 0x000000u;
    for (gint step = 1; step <= 10; step++) {
        guint32 c = mix(color, target, step / 10.0);
        if (preview_color_contrast(c, background) >= READABLE_CONTRAST) {
            return c;
        }
    }
    return text;
}

void preview_doc_add_font_file(PreviewDoc* doc, const gchar* path) {
    if (doc->font_files == NULL) {
        doc->font_files = g_ptr_array_new_with_free_func(g_free);
    }
    g_ptr_array_add(doc->font_files, g_strdup(path));
}

void preview_doc_add_image(PreviewDoc* doc, GBytes* data, gint max_width) {
    if (data == NULL) {
        return;
    }
    PreviewImage image = { doc->n_chars, g_bytes_ref(data), max_width };
    g_array_append_val(doc->images, image);
    preview_doc_append_len(doc, PREVIEW_DOC_IMAGE_CHAR, -1);
}

/* --- Summary card --- */

static gboolean ends_with_newline(const PreviewDoc* doc) {
    return doc->text->len == 0 || doc->text->str[doc->text->len - 1] == '\n';
}

void preview_doc_title(PreviewDoc* doc, const gchar* title, const gchar* subtitle) {
    if (!ends_with_newline(doc)) {
        preview_doc_append_len(doc, "\n", 1);
    }
    preview_doc_append(doc, PREVIEW_STYLE_TITLE, title != NULL ? title : "");
    preview_doc_append_len(doc, "\n", 1);
    if (subtitle != NULL && subtitle[0] != '\0') {
        preview_doc_append(doc, PREVIEW_STYLE_DIM, subtitle);
        preview_doc_append_len(doc, "\n", 1);
    }
    preview_doc_append_len(doc, "\n", 1);
}

void preview_doc_section(PreviewDoc* doc, const gchar* title) {
    if (!ends_with_newline(doc)) {
        preview_doc_append_len(doc, "\n", 1);
    }
    gsize len = doc->text->len;
    if (len > 0 && !(len >= 2 && doc->text->str[len - 2] == '\n')) {
        preview_doc_append_len(doc, "\n", 1);
    }
    preview_doc_append(doc, PREVIEW_STYLE_SECTION, title);
    preview_doc_append_len(doc, "\n", 1);
}

void preview_doc_field_styled(PreviewDoc* doc, const gchar* label, const gchar* value,
                             PreviewStyle value_style) {
    if (value == NULL || value[0] == '\0') {
        return;
    }
    GString* padded = g_string_new(label);
    guint width = preview_text_width(label);
    for (guint i = width; i < (guint)doc->label_width; i++) {
        g_string_append_c(padded, ' ');
    }
    g_string_append_c(padded, ' ');
    preview_doc_append(doc, PREVIEW_STYLE_DIM, padded->str);
    g_string_free(padded, TRUE);
    /* Continuation lines of a multi-line value line up with the first one */
    gchar** lines = g_strsplit(value, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        if (i > 0) {
            preview_doc_append_len(doc, "\n", 1);
            for (gint j = 0; j <= doc->label_width; j++) {
                preview_doc_append_len(doc, " ", 1);
            }
        }
        preview_doc_append(doc, value_style, lines[i]);
    }
    g_strfreev(lines);
    preview_doc_append_len(doc, "\n", 1);
}

void preview_doc_field(PreviewDoc* doc, const gchar* label, const gchar* value) {
    preview_doc_field_styled(doc, label, value, PREVIEW_STYLE_PLAIN);
}

void preview_doc_item(PreviewDoc* doc, const gchar* text) {
    preview_doc_append_len(doc, "  ", 2);
    preview_doc_append_len(doc, text, -1);
    preview_doc_append_len(doc, "\n", 1);
}

void preview_doc_note(PreviewDoc* doc, const gchar* text) {
    if (!ends_with_newline(doc)) {
        preview_doc_append_len(doc, "\n", 1);
    }
    preview_doc_append(doc, PREVIEW_STYLE_DIM, text);
    preview_doc_append_len(doc, "\n", 1);
}

/* --- Tables --- */

struct PreviewTable {
    guint n_cols;
    gchar** header;       /* n_cols cells, or NULL */
    GPtrArray* rows;      /* gchar** of n_cols cells */
    gboolean row_numbers;
    guint max_cell;
    gboolean* right;      /* n_cols: right-aligned columns */
};

PreviewTable* preview_table_new(guint n_cols) {
    PreviewTable* t = g_new0(PreviewTable, 1);
    t->n_cols = MAX(n_cols, 1);
    t->rows = g_ptr_array_new_with_free_func((GDestroyNotify)g_strfreev);
    t->max_cell = 40;
    t->right = g_new0(gboolean, t->n_cols);
    return t;
}

void preview_table_free(PreviewTable* table) {
    if (table == NULL) {
        return;
    }
    g_strfreev(table->header);
    g_ptr_array_free(table->rows, TRUE);
    g_free(table->right);
    g_free(table);
}

guint preview_table_get_n_cols(const PreviewTable* table) {
    return table->n_cols;
}

guint preview_table_get_n_rows(const PreviewTable* table) {
    return table->rows->len;
}

/* Copy of a cell on one line: newlines and tabs become spaces, valid UTF-8 */
static gchar* clean_cell(const gchar* cell) {
    if (cell == NULL) {
        return g_strdup("");
    }
    gchar* valid = g_utf8_make_valid(cell, -1);
    for (gchar* p = valid; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            *p = ' ';
        }
    }
    return valid;
}

static gchar** copy_cells(const PreviewTable* t, const gchar* const* cells, guint n_cells) {
    gchar** row = g_new0(gchar*, t->n_cols + 1);
    for (guint c = 0; c < t->n_cols; c++) {
        row[c] = clean_cell(c < n_cells ? cells[c] : NULL);
    }
    return row;
}

void preview_table_set_header(PreviewTable* table, const gchar* const* cells) {
    g_strfreev(table->header);
    table->header = copy_cells(table, cells, table->n_cols);
}

void preview_table_add_row(PreviewTable* table, const gchar* const* cells, guint n_cells) {
    g_ptr_array_add(table->rows, copy_cells(table, cells, n_cells));
}

void preview_table_set_row_numbers(PreviewTable* table, gboolean row_numbers) {
    table->row_numbers = row_numbers;
}

void preview_table_set_right_aligned(PreviewTable* table, guint col) {
    if (col < table->n_cols) {
        table->right[col] = TRUE;
    }
}

void preview_table_set_max_cell_width(PreviewTable* table, guint width) {
    table->max_cell = MAX(width, 3);
}

guint preview_text_width(const gchar* text) {
    guint width = 0;
    for (const gchar* p = text; p != NULL && *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_iszerowidth(c)) {
            continue;
        }
        width += g_unichar_iswide(c) ? 2 : 1;
    }
    return width;
}

gboolean preview_cell_is_number(const gchar* cell) {
    const gchar* p = cell;
    while (*p == ' ') {
        p++;
    }
    if (*p == '-' || *p == '+') {
        p++;
    }
    /* Currency prefix */
    if (g_str_has_prefix(p, "$") || g_str_has_prefix(p, "€") || g_str_has_prefix(p, "£")) {
        p += (*p == '$') ? 1 : strlen("€");
    }
    gboolean digits = FALSE;
    while (g_ascii_isdigit(*p) || *p == ',' || *p == '_' || *p == ' ') {
        digits |= g_ascii_isdigit(*p);
        p++;
    }
    if (*p == '.') {
        p++;
        while (g_ascii_isdigit(*p)) {
            digits = TRUE;
            p++;
        }
    }
    if (!digits) {
        return FALSE;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '-' || *p == '+') {
            p++;
        }
        if (!g_ascii_isdigit(*p)) {
            return FALSE;
        }
        while (g_ascii_isdigit(*p)) {
            p++;
        }
    }
    if (*p == '%') {
        p++;
    }
    while (*p == ' ') {
        p++;
    }
    return *p == '\0';
}

/* Cell cut to max display columns, with "…" when cut */
static gchar* fit_cell(const gchar* cell, guint max) {
    if (preview_text_width(cell) <= max) {
        return g_strdup(cell);
    }
    GString* out = g_string_new(NULL);
    guint width = 0;
    for (const gchar* p = cell; *p != '\0'; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        guint w = g_unichar_iszerowidth(c) ? 0 : (g_unichar_iswide(c) ? 2 : 1);
        if (width + w > max - 1) {
            break;
        }
        g_string_append_unichar(out, c);
        width += w;
    }
    g_string_append(out, "…");
    return g_string_free(out, FALSE);
}

static void append_spaces(PreviewDoc* doc, guint n) {
    static const gchar SPACES[] = "                                ";
    while (n > 0) {
        guint k = MIN(n, (guint)sizeof(SPACES) - 1);
        preview_doc_append_len(doc, SPACES, k);
        n -= k;
    }
}

/* One table line: cells padded to their column width, joined by " │ " */
static void append_table_line(PreviewDoc* doc, const PreviewTable* t, gchar** cells,
                              const gchar* number, guint number_width, const guint* widths,
                              const gboolean* numeric, gboolean header) {
    if (t->row_numbers) {
        guint w = preview_text_width(number);
        append_spaces(doc, number_width - w);
        preview_doc_append(doc, PREVIEW_STYLE_DIM, number);
        preview_doc_append(doc, PREVIEW_STYLE_GRID, " │ ");
    }
    for (guint c = 0; c < t->n_cols; c++) {
        gchar* text = fit_cell(cells[c], t->max_cell);
        guint pad = widths[c] - preview_text_width(text);
        gboolean number = !header && numeric[c] && preview_cell_is_number(cells[c]);
        gboolean right = (number || t->right[c]) && !header;
        if (right) {
            append_spaces(doc, pad);
        }
        PreviewStyle style = header ? PREVIEW_STYLE_HEADER
                             : (number ? PREVIEW_STYLE_NUMBER : PREVIEW_STYLE_PLAIN);
        preview_doc_append(doc, style, text);
        if (!right && c + 1 < t->n_cols) {
            append_spaces(doc, pad);
        }
        if (c + 1 < t->n_cols) {
            preview_doc_append(doc, PREVIEW_STYLE_GRID, " │ ");
        }
        g_free(text);
    }
    preview_doc_append_len(doc, "\n", 1);
}

void preview_doc_add_table(PreviewDoc* doc, const PreviewTable* t) {
    doc->monospace = TRUE;
    doc->wrap = FALSE;

    guint* widths = g_new0(guint, t->n_cols);
    gboolean* numeric = g_new0(gboolean, t->n_cols);
    for (guint c = 0; c < t->n_cols; c++) {
        guint filled = 0;
        guint numbers = 0;
        if (t->header != NULL) {
            widths[c] = MIN(preview_text_width(t->header[c]), t->max_cell);
        }
        /* Numeric column: every cell is a number, except maybe a title in
         * the first row (spreadsheets without a header row) */
        for (guint r = 0; r < t->rows->len; r++) {
            const gchar* cell = ((gchar**)g_ptr_array_index(t->rows, r))[c];
            widths[c] = MAX(widths[c], MIN(preview_text_width(cell), t->max_cell));
            if (cell[0] != '\0' && (r > 0 || preview_cell_is_number(cell))) {
                filled++;
                numbers += preview_cell_is_number(cell);
            }
        }
        numeric[c] = (filled > 0 && numbers == filled);
    }
    gchar* last = g_strdup_printf("%u", t->rows->len);
    guint number_width = (guint)strlen(last);
    g_free(last);

    if (t->header != NULL) {
        append_table_line(doc, t, t->header, "", number_width, widths, numeric, TRUE);
        GString* rule = g_string_new(NULL);
        if (t->row_numbers) {
            for (guint i = 0; i < number_width + 1; i++) {
                g_string_append(rule, "─");
            }
            g_string_append(rule, "┼─");
        }
        for (guint c = 0; c < t->n_cols; c++) {
            for (guint i = 0; i < widths[c]; i++) {
                g_string_append(rule, "─");
            }
            if (c + 1 < t->n_cols) {
                g_string_append(rule, "─┼─");
            }
        }
        g_string_append_c(rule, '\n');
        preview_doc_append(doc, PREVIEW_STYLE_GRID, rule->str);
        g_string_free(rule, TRUE);
    }
    for (guint r = 0; r < t->rows->len; r++) {
        gint start = preview_doc_offset(doc);
        gchar number[16];
        g_snprintf(number, sizeof(number), "%u", r + 1);
        append_table_line(doc, t, g_ptr_array_index(t->rows, r), number, number_width, widths,
                          numeric, FALSE);
        if (r % 2 == 1) {
            preview_doc_add_span(doc, start, preview_doc_offset(doc), PREVIEW_STYLE_ROW_ALT);
        }
    }
    g_free(widths);
    g_free(numeric);
}
