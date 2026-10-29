/* Spreadsheet reading (see spreadsheet.h). */
#include "spreadsheet.h"

#include <gio/gio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "zip_reader.h"
#include "zip_xml.h"

#define MAX_PART (48u * 1024 * 1024)

static void sheet_free(gpointer data) {
    Sheet* s = data;
    g_free(s->name);
    g_ptr_array_free(s->rows, TRUE);
    g_free(s);
}

static Sheet* sheet_new(const gchar* name) {
    Sheet* s = g_new0(Sheet, 1);
    s->name = g_strdup(name != NULL ? name : "Sheet");
    s->rows = g_ptr_array_new_with_free_func((GDestroyNotify)g_strfreev);
    return s;
}

void workbook_free(Workbook* book) {
    if (book == NULL) {
        return;
    }
    g_ptr_array_free(book->sheets, TRUE);
    g_free(book);
}

gchar* spreadsheet_column_name(guint index) {
    gchar buf[8];
    gint pos = 7;
    buf[pos] = '\0';
    guint n = index + 1;
    while (n > 0 && pos > 0) {
        n--;
        buf[--pos] = (gchar)('A' + n % 26);
        n /= 26;
    }
    return g_strdup(buf + pos);
}

gint spreadsheet_column_index(const gchar* ref) {
    if (ref == NULL || !g_ascii_isalpha(ref[0])) {
        return -1;
    }
    gint col = 0;
    const gchar* p = ref;
    for (; g_ascii_isalpha(*p); p++) {
        col = col * 26 + (g_ascii_toupper(*p) - 'A' + 1);
        if (col > 16384) {
            return -1;
        }
    }
    return col - 1;
}

/* --- Number formats --- */

/* Section of a format code before the first ';', without quoted text,
 * escapes and [colour] / [$-locale] brackets */
static gchar* clean_code(const gchar* code) {
    GString* out = g_string_new(NULL);
    for (const gchar* p = code; *p != '\0' && *p != ';'; p++) {
        if (*p == '"') {
            const gchar* q = strchr(p + 1, '"');
            if (q == NULL) {
                break;
            }
            p = q;
        } else if (*p == '\\' && p[1] != '\0') {
            p++;
        } else if (*p == '[') {
            const gchar* q = strchr(p, ']');
            if (q == NULL) {
                break;
            }
            /* [h], [mm] elapsed time keep their letters */
            if (q - p > 1 && strchr("hHmMsS", p[1]) != NULL) {
                g_string_append_len(out, p + 1, q - p - 1);
            }
            p = q;
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

static gboolean is_date_builtin(guint id) {
    return (id >= 14 && id <= 22) || (id >= 45 && id <= 47) || (id >= 27 && id <= 36) ||
           (id >= 50 && id <= 58);
}

static gboolean code_is_date(const gchar* clean) {
    for (const gchar* p = clean; *p != '\0'; p++) {
        if (strchr("dDyYhHsS", *p) != NULL) {
            return TRUE;
        }
        if ((*p == 'm' || *p == 'M') && strchr(clean, '0') == NULL && strchr(clean, '#') == NULL) {
            return TRUE;
        }
    }
    return FALSE;
}

/* "2026-10-29", "2026-10-29 10:30", "10:30:00" from a serial date */
static gchar* format_date(gdouble value, gboolean has_date, gboolean has_time, gboolean seconds) {
    gdouble days = floor(value);
    gint64 secs = (gint64)llround((value - days) * 86400.0);
    if (secs >= 86400) {
        days += 1;
        secs -= 86400;
    }
    GDateTime* epoch = g_date_time_new_utc(1899, 12, 30, 0, 0, 0);
    GDateTime* dt = g_date_time_add_days(epoch, (gint)days);
    g_date_time_unref(epoch);
    if (dt == NULL) {
        return g_strdup_printf("%g", value);
    }
    GDateTime* full = g_date_time_add_seconds(dt, (gdouble)secs);
    g_date_time_unref(dt);
    const gchar* fmt = has_date && has_time ? (seconds ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%d %H:%M")
                       : has_time ? (seconds ? "%H:%M:%S" : "%H:%M") : "%Y-%m-%d";
    gchar* out = g_date_time_format(full, fmt);
    g_date_time_unref(full);
    return out;
}

/* Number with a fixed number of decimals and optional thousands separators */
static gchar* format_fixed(gdouble value, gint decimals, gboolean thousands) {
    gchar* digits = g_strdup_printf("%.*f", decimals, fabs(value));
    gchar* dot = strchr(digits, '.');
    gsize int_len = dot != NULL ? (gsize)(dot - digits) : strlen(digits);
    GString* out = g_string_new(value < 0 && strspn(digits, "0.") != strlen(digits) ? "-" : "");
    for (gsize i = 0; i < int_len; i++) {
        if (thousands && i > 0 && (int_len - i) % 3 == 0) {
            g_string_append_c(out, ',');
        }
        g_string_append_c(out, digits[i]);
    }
    if (dot != NULL) {
        g_string_append(out, dot);
    }
    g_free(digits);
    return g_string_free(out, FALSE);
}

/* General format: up to 10 significant digits, no trailing zeros */
static gchar* format_general(gdouble value) {
    if (value == floor(value) && fabs(value) < 1e15) {
        return g_strdup_printf("%.0f", value);
    }
    gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(buf, sizeof(buf), "%.10g", value);
    return g_strdup(buf);
}

gchar* spreadsheet_format_number(gdouble value, guint builtin_id, const gchar* format_code) {
    static const gchar* const BUILTIN[] = {
        [1] = "0", [2] = "0.00", [3] = "#,##0", [4] = "#,##0.00", [9] = "0%", [10] = "0.00%",
        [11] = "0.00E+00", [37] = "#,##0 ;(#,##0)", [38] = "#,##0 ;(#,##0)", [39] = "#,##0.00;(#,##0.00)",
        [40] = "#,##0.00;(#,##0.00)", [48] = "##0.0E+0",
    };
    if (format_code == NULL && builtin_id < G_N_ELEMENTS(BUILTIN)) {
        format_code = BUILTIN[builtin_id];
    }
    if (is_date_builtin(builtin_id) && format_code == NULL) {
        gboolean time = (builtin_id >= 18 && builtin_id <= 21) || (builtin_id >= 45 && builtin_id <= 47);
        gboolean date = !(builtin_id >= 18 && builtin_id <= 21) && !(builtin_id >= 45 && builtin_id <= 47);
        return format_date(value, date || builtin_id == 22, time || builtin_id == 22,
                           builtin_id == 19 || builtin_id == 21 || builtin_id == 46 || builtin_id == 47);
    }
    if (format_code == NULL || g_ascii_strcasecmp(format_code, "General") == 0 || format_code[0] == '@') {
        return format_general(value);
    }
    gchar* code = clean_code(format_code);
    gchar* out;
    if (code_is_date(code)) {
        gboolean date = strpbrk(code, "dDyY") != NULL || (strpbrk(code, "mM") != NULL && strpbrk(code, "hH") == NULL);
        gboolean time = strpbrk(code, "hHsS") != NULL;
        out = format_date(value, date, time, strpbrk(code, "sS") != NULL);
    } else if (strpbrk(code, "Ee") != NULL && strchr(code, '0') != NULL) {
        out = g_strdup_printf("%.2E", value);
    } else {
        gboolean percent = strchr(code, '%') != NULL;
        gboolean thousands = strstr(code, "#,") != NULL || strstr(code, "0,0") != NULL;
        const gchar* dot = strchr(code, '.');
        gint decimals = 0;
        if (dot != NULL) {
            for (const gchar* p = dot + 1; *p == '0' || *p == '#'; p++) {
                decimals++;
            }
        }
        if (strchr(code, '0') == NULL && strchr(code, '#') == NULL) {
            out = format_general(value);
        } else {
            gchar* num = format_fixed(percent ? value * 100 : value, decimals, thousands);
            out = percent ? g_strconcat(num, "%", NULL) : g_strdup(num);
            g_free(num);
        }
        /* Currency symbols written in the code ("$#,##0.00", "#,##0 €") */
        const gchar* currency = strstr(format_code, "$") != NULL ? "$"
                                : strstr(format_code, "€") != NULL ? "€"
                                : strstr(format_code, "£") != NULL ? "£" : NULL;
        if (currency != NULL) {
            const gchar* digit = strpbrk(format_code, "0#");
            gboolean before = digit != NULL && strstr(format_code, currency) < digit;
            gchar* with = before ? g_strconcat(currency, out, NULL) : g_strconcat(out, " ", currency, NULL);
            g_free(out);
            out = with;
        }
    }
    g_free(code);
    return out;
}

/* --- Rows --- */

typedef struct {
    Sheet* sheet;
    guint max_rows;
    GPtrArray* row;        /* gchar* cells of the current row */
    gint last_filled_row;  /* index in sheet->rows of the last row with content */
} RowBuilder;

static void row_set(RowBuilder* b, guint col, gchar* text) {
    if (col >= SPREADSHEET_MAX_COLS) {
        g_free(text);
        return;
    }
    while (b->row->len <= col) {
        g_ptr_array_add(b->row, g_strdup(""));
    }
    g_free(g_ptr_array_index(b->row, col));
    g_ptr_array_index(b->row, col) = text;
}

/* End the current row; FALSE once max_rows rows are stored */
static gboolean row_end(RowBuilder* b, guint repeat) {
    gboolean filled = FALSE;
    guint last = 0;
    for (guint c = 0; c < b->row->len; c++) {
        if (((gchar*)g_ptr_array_index(b->row, c))[0] != '\0') {
            filled = TRUE;
            last = c + 1;
        }
    }
    if (!filled && repeat > 100) {
        repeat = 1; /* long runs of empty rows (the end of an .ods sheet) */
    }
    for (guint r = 0; r < MAX(repeat, 1u); r++) {
        if (b->sheet->rows->len >= b->max_rows) {
            b->sheet->truncated = TRUE;
            g_ptr_array_set_size(b->row, 0);
            return FALSE;
        }
        gchar** cells = g_new0(gchar*, b->row->len + 1);
        for (guint c = 0; c < b->row->len; c++) {
            cells[c] = g_strdup(g_ptr_array_index(b->row, c));
        }
        g_ptr_array_add(b->sheet->rows, cells);
        if (filled) {
            b->last_filled_row = (gint)b->sheet->rows->len - 1;
            b->sheet->n_cols = MAX(b->sheet->n_cols, last);
        }
    }
    g_ptr_array_set_size(b->row, 0);
    return TRUE;
}

/* Drop trailing empty rows, pad rows to n_cols */
static void finish_sheet(RowBuilder* b) {
    Sheet* s = b->sheet;
    g_ptr_array_set_size(s->rows, (guint)(b->last_filled_row + 1));
    for (guint r = 0; r < s->rows->len; r++) {
        gchar** cells = g_ptr_array_index(s->rows, r);
        guint n = g_strv_length(cells);
        if (n != s->n_cols) {
            gchar** fixed = g_new0(gchar*, s->n_cols + 1);
            for (guint c = 0; c < s->n_cols; c++) {
                fixed[c] = g_strdup(c < n ? cells[c] : "");
            }
            g_ptr_array_index(s->rows, r) = fixed;
            g_strfreev(cells);
        }
    }
    s->loaded = TRUE;
}

/* --- XLSX --- */

typedef struct {
    GPtrArray* strings;    /* shared strings */
    GString* text;
    gboolean in_si;
    gboolean in_t;
    gboolean in_rph;       /* phonetic runs are not shown */
} SharedStrings;

static void ss_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                     const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)names; (void)values; (void)error;
    SharedStrings* s = data;
    const gchar* n = zip_xml_local(name);
    if (strcmp(n, "si") == 0) {
        s->in_si = TRUE;
        g_string_truncate(s->text, 0);
    } else if (strcmp(n, "t") == 0) {
        s->in_t = TRUE;
    } else if (strcmp(n, "rPh") == 0) {
        s->in_rph = TRUE;
    }
}

static void ss_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx; (void)error;
    SharedStrings* s = data;
    const gchar* n = zip_xml_local(name);
    if (strcmp(n, "si") == 0) {
        g_ptr_array_add(s->strings, g_strdup(s->text->str));
        s->in_si = FALSE;
    } else if (strcmp(n, "t") == 0) {
        s->in_t = FALSE;
    } else if (strcmp(n, "rPh") == 0) {
        s->in_rph = FALSE;
    }
}

static void ss_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx; (void)error;
    SharedStrings* s = data;
    if (s->in_si && s->in_t && !s->in_rph) {
        g_string_append_len(s->text, text, (gssize)len);
    }
}

typedef struct {
    GArray* xf_formats;    /* guint numFmtId per cell style index */
    GHashTable* codes;     /* numFmtId -> format code */
    gboolean in_cell_xfs;
} Styles;

static void styles_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                         const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    Styles* st = data;
    const gchar* n = zip_xml_local(name);
    if (strcmp(n, "numFmt") == 0) {
        const gchar* id = zip_xml_attr(names, values, "numFmtId");
        const gchar* code = zip_xml_attr(names, values, "formatCode");
        if (id != NULL && code != NULL) {
            g_hash_table_insert(st->codes, GUINT_TO_POINTER((guint)atoi(id)), g_strdup(code));
        }
    } else if (strcmp(n, "cellXfs") == 0) {
        st->in_cell_xfs = TRUE;
    } else if (strcmp(n, "xf") == 0 && st->in_cell_xfs) {
        const gchar* id = zip_xml_attr(names, values, "numFmtId");
        guint v = id != NULL ? (guint)atoi(id) : 0;
        g_array_append_val(st->xf_formats, v);
    }
}

static void styles_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx; (void)error;
    Styles* st = data;
    if (strcmp(zip_xml_local(name), "cellXfs") == 0) {
        st->in_cell_xfs = FALSE;
    }
}

typedef struct {
    RowBuilder b;
    GPtrArray* strings;
    Styles* styles;
    gint col;              /* column of the current cell */
    gchar* type;           /* t attribute */
    gint style;            /* s attribute */
    GString* value;        /* <v> or inline <t> text */
    gboolean in_value;
    gint next_col;
} SheetParse;

static gchar* xlsx_cell_text(SheetParse* p) {
    const gchar* v = p->value->str;
    const gchar* t = p->type;
    if (g_strcmp0(t, "s") == 0) {
        guint i = (guint)atoi(v);
        return g_strdup(i < p->strings->len ? (const gchar*)g_ptr_array_index(p->strings, i) : "");
    }
    if (g_strcmp0(t, "b") == 0) {
        return g_strdup(strcmp(v, "1") == 0 ? "TRUE" : "FALSE");
    }
    if (g_strcmp0(t, "str") == 0 || g_strcmp0(t, "inlineStr") == 0 || g_strcmp0(t, "e") == 0 || v[0] == '\0') {
        return g_strdup(v);
    }
    gchar* end = NULL;
    gdouble num = g_ascii_strtod(v, &end);
    if (end == v) {
        return g_strdup(v);
    }
    guint fmt_id = 0;
    if (p->style >= 0 && (guint)p->style < p->styles->xf_formats->len) {
        fmt_id = g_array_index(p->styles->xf_formats, guint, p->style);
    }
    const gchar* code = g_hash_table_lookup(p->styles->codes, GUINT_TO_POINTER(fmt_id));
    return spreadsheet_format_number(num, fmt_id, code);
}

static void sheet_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                        const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    SheetParse* p = data;
    const gchar* n = zip_xml_local(name);
    if (strcmp(n, "row") == 0) {
        /* Rows skipped in the file are empty rows */
        const gchar* r = zip_xml_attr(names, values, "r");
        guint index = r != NULL ? (guint)atoi(r) : p->b.sheet->rows->len + 1;
        while (index > 1 && p->b.sheet->rows->len < index - 1 && p->b.sheet->rows->len < p->b.max_rows) {
            row_end(&p->b, 1);
        }
        p->next_col = 0;
    } else if (strcmp(n, "c") == 0) {
        const gchar* r = zip_xml_attr(names, values, "r");
        p->col = r != NULL ? spreadsheet_column_index(r) : p->next_col;
        g_free(p->type);
        p->type = g_strdup(zip_xml_attr(names, values, "t"));
        const gchar* s = zip_xml_attr(names, values, "s");
        p->style = s != NULL ? atoi(s) : -1;
        g_string_truncate(p->value, 0);
    } else if (strcmp(n, "v") == 0 || strcmp(n, "t") == 0) {
        p->in_value = TRUE;
    }
}

static void sheet_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    SheetParse* p = data;
    const gchar* n = zip_xml_local(name);
    if (strcmp(n, "v") == 0 || strcmp(n, "t") == 0) {
        p->in_value = FALSE;
    } else if (strcmp(n, "c") == 0) {
        if (p->col >= 0) {
            row_set(&p->b, (guint)p->col, xlsx_cell_text(p));
            p->next_col = p->col + 1;
        }
    } else if (strcmp(n, "row") == 0) {
        if (!row_end(&p->b, 1)) {
            zip_xml_stop(error);
        }
    }
}

static void sheet_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx; (void)error;
    SheetParse* p = data;
    if (p->in_value) {
        g_string_append_len(p->value, text, (gssize)len);
    }
}

typedef struct {
    GPtrArray* names;
    GPtrArray* rids;
} WorkbookParse;

static void wb_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                     const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    WorkbookParse* w = data;
    if (strcmp(zip_xml_local(name), "sheet") == 0) {
        g_ptr_array_add(w->names, g_strdup(zip_xml_attr(names, values, "name")));
        /* r:id (the relationship namespace prefix varies) */
        const gchar* rid = NULL;
        for (gint i = 0; names[i] != NULL; i++) {
            if (g_str_has_suffix(names[i], ":id")) {
                rid = values[i];
            }
        }
        g_ptr_array_add(w->rids, g_strdup(rid));
    }
}

static Workbook* read_xlsx(ZipReader* zip, guint max_sheets, guint max_rows, GError** error) {
    WorkbookParse w = { g_ptr_array_new_with_free_func(g_free), g_ptr_array_new_with_free_func(g_free) };
    static const GMarkupParser wb_parser = { wb_start, NULL, NULL, NULL, NULL };
    if (!zip_xml_parse(zip, "xl/workbook.xml", MAX_PART, FALSE, &wb_parser, &w, error)) {
        g_ptr_array_free(w.names, TRUE);
        g_ptr_array_free(w.rids, TRUE);
        return NULL;
    }
    GHashTable* rels = zip_xml_relationships(zip, "xl/workbook.xml");

    SharedStrings ss = { g_ptr_array_new_with_free_func(g_free), g_string_new(NULL), FALSE, FALSE, FALSE };
    static const GMarkupParser ss_parser = { ss_start, ss_end, ss_text, NULL, NULL };
    zip_xml_parse(zip, "xl/sharedStrings.xml", MAX_PART, TRUE, &ss_parser, &ss, NULL);
    Styles styles = { g_array_new(FALSE, FALSE, sizeof(guint)),
                      g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free), FALSE };
    static const GMarkupParser styles_parser = { styles_start, styles_end, NULL, NULL, NULL };
    zip_xml_parse(zip, "xl/styles.xml", MAX_PART, TRUE, &styles_parser, &styles, NULL);

    Workbook* book = g_new0(Workbook, 1);
    book->format = "Excel workbook";
    book->sheets = g_ptr_array_new_with_free_func(sheet_free);
    for (guint i = 0; i < w.names->len; i++) {
        Sheet* sheet = sheet_new(g_ptr_array_index(w.names, i));
        g_ptr_array_add(book->sheets, sheet);
        if (i >= max_sheets) {
            continue;
        }
        const gchar* rid = g_ptr_array_index(w.rids, i);
        const gchar* part = rid != NULL ? g_hash_table_lookup(rels, rid) : NULL;
        gchar* fallback = g_strdup_printf("xl/worksheets/sheet%u.xml", i + 1);
        SheetParse p = { { sheet, max_rows, g_ptr_array_new_with_free_func(g_free), -1 },
                         ss.strings, &styles, 0, NULL, -1, g_string_new(NULL), FALSE, 0 };
        static const GMarkupParser sheet_parser = { sheet_start, sheet_end, sheet_text, NULL, NULL };
        if (zip_xml_parse(zip, part != NULL ? part : fallback, MAX_PART, TRUE, &sheet_parser, &p, NULL)) {
            finish_sheet(&p.b);
        }
        g_free(fallback);
        g_free(p.type);
        g_string_free(p.value, TRUE);
        g_ptr_array_free(p.b.row, TRUE);
    }
    g_hash_table_destroy(rels);
    g_ptr_array_free(ss.strings, TRUE);
    g_string_free(ss.text, TRUE);
    g_array_free(styles.xf_formats, TRUE);
    g_hash_table_destroy(styles.codes);
    g_ptr_array_free(w.names, TRUE);
    g_ptr_array_free(w.rids, TRUE);
    return book;
}

/* --- ODS --- */

typedef struct {
    Workbook* book;
    guint max_sheets;
    RowBuilder b;
    gboolean in_table;     /* a sheet whose cells are read */
    guint table_depth;     /* nested tables (subtables) are skipped */
    gboolean in_cell;
    gint cell_repeat;
    guint col;
    GString* text;
    gint paragraphs;
    gint row_repeat;
} OdsParse;

static void ods_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names,
                      const gchar** values, gpointer data, GError** error) {
    (void)ctx; (void)error;
    OdsParse* p = data;
    if (strcmp(name, "table:table") == 0) {
        p->table_depth++;
        if (p->table_depth > 1) {
            return;
        }
        Sheet* sheet = sheet_new(zip_xml_attr(names, values, "name"));
        g_ptr_array_add(p->book->sheets, sheet);
        p->in_table = p->book->sheets->len <= p->max_sheets;
        p->b.sheet = sheet;
        p->b.last_filled_row = -1;
        g_ptr_array_set_size(p->b.row, 0);
        return;
    }
    if (!p->in_table || p->table_depth != 1) {
        return;
    }
    if (strcmp(name, "table:table-row") == 0) {
        const gchar* rep = zip_xml_attr(names, values, "number-rows-repeated");
        p->row_repeat = rep != NULL ? atoi(rep) : 1;
        p->col = 0;
    } else if (strcmp(name, "table:table-cell") == 0 || strcmp(name, "table:covered-table-cell") == 0) {
        const gchar* rep = zip_xml_attr(names, values, "number-columns-repeated");
        p->cell_repeat = rep != NULL ? atoi(rep) : 1;
        p->in_cell = TRUE;
        p->paragraphs = 0;
        g_string_truncate(p->text, 0);
    } else if (p->in_cell && strcmp(name, "text:p") == 0) {
        if (p->paragraphs++ > 0) {
            g_string_append_c(p->text, ' ');
        }
    } else if (p->in_cell && strcmp(name, "text:s") == 0) {
        const gchar* c = zip_xml_attr(names, values, "c");
        for (gint i = 0; i < (c != NULL ? atoi(c) : 1); i++) {
            g_string_append_c(p->text, ' ');
        }
    } else if (p->in_cell && (strcmp(name, "text:tab") == 0 || strcmp(name, "text:line-break") == 0)) {
        g_string_append_c(p->text, ' ');
    }
}

static void ods_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    OdsParse* p = data;
    if (strcmp(name, "table:table") == 0) {
        if (p->table_depth == 1 && p->in_table) {
            finish_sheet(&p->b);
            p->in_table = FALSE;
        }
        p->table_depth--;
        return;
    }
    if (!p->in_table || p->table_depth != 1) {
        return;
    }
    if (strcmp(name, "table:table-cell") == 0 || strcmp(name, "table:covered-table-cell") == 0) {
        gboolean empty = p->text->len == 0;
        gint repeat = empty ? MIN(p->cell_repeat, (gint)SPREADSHEET_MAX_COLS) : MIN(p->cell_repeat, 1000);
        for (gint i = 0; i < repeat; i++) {
            if (!empty) {
                row_set(&p->b, p->col, g_strdup(p->text->str));
            }
            p->col++;
        }
        p->in_cell = FALSE;
    } else if (strcmp(name, "table:table-row") == 0) {
        if (!row_end(&p->b, (guint)MAX(p->row_repeat, 1))) {
            finish_sheet(&p->b);
            p->in_table = FALSE;
            if (p->book->sheets->len >= p->max_sheets) {
                zip_xml_stop(error);
            }
        }
    }
}

static void ods_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx; (void)error;
    OdsParse* p = data;
    if (p->in_table && p->in_cell) {
        g_string_append_len(p->text, text, (gssize)len);
    }
}

static Workbook* read_ods(ZipReader* zip, guint max_sheets, guint max_rows, GError** error) {
    Workbook* book = g_new0(Workbook, 1);
    book->format = "OpenDocument spreadsheet";
    book->sheets = g_ptr_array_new_with_free_func(sheet_free);
    OdsParse p = { book, max_sheets, { NULL, max_rows, g_ptr_array_new_with_free_func(g_free), -1 },
                   FALSE, 0, FALSE, 1, 0, g_string_new(NULL), 0, 1 };
    static const GMarkupParser parser = { ods_start, ods_end, ods_text, NULL, NULL };
    gboolean ok = zip_xml_parse(zip, "content.xml", MAX_PART * 4, FALSE, &parser, &p, error);
    g_string_free(p.text, TRUE);
    g_ptr_array_free(p.b.row, TRUE);
    if (!ok) {
        workbook_free(book);
        return NULL;
    }
    return book;
}

Workbook* spreadsheet_read(const gchar* path, guint max_sheets, guint max_rows, GError** error) {
    ZipReader* zip = zip_reader_open(path, error);
    if (zip == NULL) {
        return NULL;
    }
    Workbook* book = NULL;
    if (zip_reader_has(zip, "xl/workbook.xml")) {
        book = read_xlsx(zip, max_sheets, max_rows, error);
    } else if (zip_reader_has(zip, "content.xml")) {
        book = read_ods(zip, max_sheets, max_rows, error);
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not a spreadsheet (no workbook)");
    }
    zip_reader_free(zip);
    return book;
}
