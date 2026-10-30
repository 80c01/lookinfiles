/* Excel 97-2003 and 5.0 / 95 workbooks (see xls_reader.h) */
#include "xls_reader.h"
#include <gio/gio.h>
#include <math.h>
#include <string.h>

#define BOOK_MAX_STREAM (64 * 1024 * 1024)
#define MAX_STRINGS 2000000

#define REC_FORMULA 0x0006
#define REC_EOF 0x000A
#define REC_DATEMODE 0x0022
#define REC_FILEPASS 0x002F
#define REC_CONTINUE 0x003C
#define REC_CODEPAGE 0x0042
#define REC_BOUNDSHEET 0x0085
#define REC_MULRK 0x00BD
#define REC_RSTRING 0x00D6
#define REC_XF 0x00E0
#define REC_SST 0x00FC
#define REC_LABELSST 0x00FD
#define REC_NUMBER 0x0203
#define REC_LABEL 0x0204
#define REC_BOOLERR 0x0205
#define REC_STRING 0x0207
#define REC_RK 0x027E
#define REC_FORMAT 0x041E
#define REC_FORMAT_OLD 0x001E
#define REC_BOF 0x0809

static guint16 u16(const guint8* p) {
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 u32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static gdouble f64(const guint8* p) {
    guint64 bits = (guint64)u32(p) | ((guint64)u32(p + 4) << 32);
    gdouble v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

typedef struct {
    const guint8* data;
    gsize len;
    gboolean biff8;
    gint codepage;
    gboolean date1904;
    GPtrArray* strings;     /* SST */
    GHashTable* formats;    /* ifmt -> format code */
    GArray* xf_formats;     /* guint16 ifmt of each XF */
} Book;

/* One record: type, payload, payload length; FALSE at the end or when damaged */
static gboolean next_record(const Book* b, gsize* pos, guint16* type, const guint8** data, guint16* len) {
    if (*pos + 4 > b->len) return FALSE;
    *type = u16(b->data + *pos);
    *len = u16(b->data + *pos + 2);
    if (*pos + 4 + *len > b->len) return FALSE;
    *data = b->data + *pos + 4;
    *pos += 4 + (gsize)*len;
    return TRUE;
}

/* 8-bit text in the workbook's code page */
static gchar* decode_8bit(const Book* b, const guint8* s, gsize n) {
    gchar* out = NULL;
    if (b->codepage == 65001) {
        out = g_strndup((const gchar*)s, n);
    } else {
        gchar* name = b->codepage == 1252 || b->codepage <= 0 || b->codepage == 1200 ? g_strdup("WINDOWS-1252")
                      : b->codepage == 10000 || b->codepage == 32768 ? g_strdup("MACINTOSH")
                                                                     : g_strdup_printf("CP%d", b->codepage);
        out = g_convert((const gchar*)s, (gssize)n, "UTF-8", name, NULL, NULL, NULL);
        if (out == NULL) out = g_convert((const gchar*)s, (gssize)n, "UTF-8", "WINDOWS-1252", NULL, NULL, NULL);
        g_free(name);
    }
    if (out == NULL) out = g_strndup((const gchar*)s, n);
    if (!g_utf8_validate(out, -1, NULL)) {
        gchar* v = g_utf8_make_valid(out, -1);
        g_free(out);
        out = v;
    }
    return out;
}

/* Characters of a BIFF8 string: 8-bit (Latin-1 "compressed") or UTF-16 */
static void append_chars(GString* out, const guint8* p, gsize count, gboolean high) {
    for (gsize i = 0; i < count; i++) {
        gunichar c = high ? u16(p + 2 * i) : p[i];
        if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
        if (c == 0) continue;
        g_string_append_unichar(out, c);
    }
}

/* A string at p within [p, end): BIFF8 XLUnicodeString (cch_bytes 2) or
 * ShortXLUnicodeString (1), BIFF5 counted 8-bit string. NULL when damaged. */
static gchar* read_string(const Book* b, const guint8* p, const guint8* end, gint cch_bytes) {
    if (p + cch_bytes > end) return NULL;
    gsize cch = cch_bytes == 2 ? u16(p) : p[0];
    p += cch_bytes;
    if (!b->biff8) {
        if (p + cch > end) cch = (gsize)(end - p);
        return decode_8bit(b, p, cch);
    }
    if (p + 1 > end) return NULL;
    guint8 flags = *p++;
    gboolean high = (flags & 0x01) != 0;
    if (flags & 0x08) p += 2; /* rich text run count */
    if (flags & 0x04) p += 4; /* extended data size */
    gsize need = cch * (high ? 2 : 1);
    if (p > end || need > (gsize)(end - p)) cch = p > end ? 0 : (gsize)(end - p) / (high ? 2 : 1);
    GString* out = g_string_new(NULL);
    append_chars(out, p, cch, high);
    return g_string_free(out, FALSE);
}

/* --- Shared strings: the SST record and its CONTINUE records --- */

typedef struct {
    GPtrArray* segments;  /* GBytes views */
    guint seg;
    gsize off;
} Reader;

static gsize reader_left(const Reader* r) {
    if (r->seg >= r->segments->len) return 0;
    return g_bytes_get_size(g_ptr_array_index(r->segments, r->seg)) - r->off;
}

static const guint8* reader_ptr(const Reader* r) {
    return (const guint8*)g_bytes_get_data(g_ptr_array_index(r->segments, r->seg), NULL) + r->off;
}

/* Skip n bytes across segments; FALSE when the data ends */
static gboolean reader_skip(Reader* r, gsize n) {
    while (n > 0) {
        if (r->seg >= r->segments->len) return FALSE;
        gsize left = reader_left(r);
        if (left == 0) {
            r->seg++;
            r->off = 0;
            continue;
        }
        gsize k = MIN(left, n);
        r->off += k;
        n -= k;
    }
    return TRUE;
}

static gboolean reader_bytes(Reader* r, guint8* out, gsize n) {
    for (gsize i = 0; i < n; i++) {
        while (r->seg < r->segments->len && reader_left(r) == 0) {
            r->seg++;
            r->off = 0;
        }
        if (r->seg >= r->segments->len) return FALSE;
        out[i] = *reader_ptr(r);
        r->off++;
    }
    return TRUE;
}

/* One XLUnicodeRichExtendedString; the characters may continue in the next
 * segment, which starts with a new flags byte */
static gchar* reader_string(Reader* r) {
    guint8 head[3];
    if (!reader_bytes(r, head, 3)) return NULL;
    gsize cch = u16(head);
    guint8 flags = head[2];
    guint16 runs = 0;
    guint32 ext = 0;
    guint8 tmp[4];
    if (flags & 0x08) {
        if (!reader_bytes(r, tmp, 2)) return NULL;
        runs = u16(tmp);
    }
    if (flags & 0x04) {
        if (!reader_bytes(r, tmp, 4)) return NULL;
        ext = u32(tmp);
    }
    gboolean high = (flags & 0x01) != 0;
    GString* out = g_string_new(NULL);
    while (cch > 0) {
        gsize left = r->seg < r->segments->len ? reader_left(r) : 0;
        if (left == 0) {
            /* Continued: a new flags byte tells the width of the rest */
            r->seg++;
            r->off = 0;
            if (r->seg >= r->segments->len || reader_left(r) == 0) break;
            high = (*reader_ptr(r) & 0x01) != 0;
            r->off++;
            continue;
        }
        gsize width = high ? 2 : 1;
        gsize k = MIN(cch, left / width);
        if (k == 0) break;
        append_chars(out, reader_ptr(r), k, high);
        r->off += k * width;
        cch -= k;
    }
    reader_skip(r, (gsize)runs * 4 + ext);
    return g_string_free(out, FALSE);
}

static void read_sst(Book* b, const guint8* data, guint16 len, gsize* pos) {
    if (len < 8) return;
    Reader r = {g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref), 0, 8};
    g_ptr_array_add(r.segments, g_bytes_new_static(data, len));
    guint32 unique = u32(data + 4);
    /* The CONTINUE records that follow */
    gsize p = *pos;
    guint16 type, clen;
    const guint8* cdata;
    while (TRUE) {
        gsize before = p;
        if (!next_record(b, &p, &type, &cdata, &clen) || type != REC_CONTINUE) {
            p = before;
            break;
        }
        g_ptr_array_add(r.segments, g_bytes_new_static(cdata, clen));
    }
    *pos = p;
    for (guint32 i = 0; i < unique && i < MAX_STRINGS; i++) {
        gchar* s = reader_string(&r);
        if (s == NULL) break;
        g_ptr_array_add(b->strings, s);
    }
    g_ptr_array_unref(r.segments);
}

/* --- Cells --- */

typedef struct {
    GPtrArray* rows;      /* GPtrArray of gchar* (NULL: empty) */
    guint n_cols;
    guint max_rows;
    gboolean truncated;
} Grid;

static void set_cell(Grid* g, guint row, guint col, gchar* text) {
    if (text == NULL) return;
    if (row >= g->max_rows) {
        g->truncated = TRUE;
        g_free(text);
        return;
    }
    if (col >= SPREADSHEET_MAX_COLS || text[0] == '\0') {
        g_free(text);
        return;
    }
    while (g->rows->len <= row) g_ptr_array_add(g->rows, NULL);
    GPtrArray* cells = g_ptr_array_index(g->rows, row);
    if (cells == NULL) {
        cells = g_ptr_array_new_with_free_func(g_free);
        g_ptr_array_index(g->rows, row) = cells;
    }
    while (cells->len <= col) g_ptr_array_add(cells, NULL);
    g_free(g_ptr_array_index(cells, col));
    g_ptr_array_index(cells, col) = text;
    g->n_cols = MAX(g->n_cols, col + 1);
}

static gdouble rk_value(guint32 rk) {
    gdouble v;
    if (rk & 0x02) {
        v = (gdouble)((gint32)rk >> 2);
    } else {
        guint64 bits = (guint64)(rk & 0xFFFFFFFCu) << 32;
        memcpy(&v, &bits, sizeof v);
    }
    return (rk & 0x01) ? v / 100.0 : v;
}

/* A date / time format (the 1904 system shifts these only) */
static gboolean is_date_format(guint ifmt, const gchar* code) {
    if ((ifmt >= 14 && ifmt <= 22) || (ifmt >= 45 && ifmt <= 47)) return TRUE;
    if (code == NULL) return FALSE;
    gboolean quoted = FALSE;
    for (const gchar* p = code; *p != '\0'; p++) {
        if (*p == '"') quoted = !quoted;
        else if (*p == '[' ) {
            const gchar* e = strchr(p, ']');
            if (e == NULL) break;
            p = e;
        } else if (!quoted && strchr("yYdDhH", *p) != NULL) return TRUE;
    }
    return FALSE;
}

/* Formats whose sections are only quoted text ("TRUE";"TRUE";"FALSE", the
 * booleans of LibreOffice): the text of the section for v, else NULL */
static gchar* literal_section(const gchar* code, gdouble v) {
    if (code == NULL || code[0] != '"') return NULL;
    gchar** sections = g_strsplit(code, ";", 4);
    guint n = g_strv_length(sections);
    gchar* out = NULL;
    gboolean literal = TRUE;
    for (guint i = 0; i < n && literal; i++) {
        gsize len = strlen(sections[i]);
        literal = len >= 2 && sections[i][0] == '"' && sections[i][len - 1] == '"' &&
                  strchr(sections[i] + 1, '"') == sections[i] + len - 1;
    }
    if (literal && n >= 1) {
        guint pick = v > 0 || n == 1 ? 0 : v < 0 ? (n >= 2 ? 1 : 0) : (n >= 3 ? 2 : 0);
        const gchar* s = sections[pick];
        out = g_strndup(s + 1, strlen(s) - 2);
    }
    g_strfreev(sections);
    return out;
}

static gchar* number_text(const Book* b, guint16 ixfe, gdouble v) {
    guint16 ifmt = ixfe < b->xf_formats->len ? g_array_index(b->xf_formats, guint16, ixfe) : 0;
    const gchar* code = g_hash_table_lookup(b->formats, GUINT_TO_POINTER(ifmt));
    gchar* literal = literal_section(code, v);
    if (literal != NULL) return literal;
    if (b->date1904 && is_date_format(ifmt, code)) v += 1462;
    if (!isfinite(v)) return g_strdup("#NUM!");
    return spreadsheet_format_number(v, ifmt, code);
}

static const gchar* error_text(guint8 code) {
    switch (code) {
        case 0x00: return "#NULL!";
        case 0x07: return "#DIV/0!";
        case 0x0F: return "#VALUE!";
        case 0x17: return "#REF!";
        case 0x1D: return "#NAME?";
        case 0x24: return "#NUM!";
        case 0x2A: return "#N/A";
        default: return "#ERR";
    }
}

static void read_sheet(const Book* b, gsize pos, Sheet* sheet, guint max_rows) {
    Grid g = {g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref), 0, max_rows, FALSE};
    guint16 type, len;
    const guint8* d;
    gint string_row = -1, string_col = -1; /* a FORMULA waiting for its STRING */
    gboolean first = TRUE;
    while (next_record(b, &pos, &type, &d, &len)) {
        if (first) {
            first = FALSE;
            if (type != REC_BOF) break;
            continue;
        }
        if (type == REC_EOF || type == REC_BOF) break;
        if (len < 6 && type != REC_STRING) continue;
        guint row = len >= 2 ? u16(d) : 0, col = len >= 4 ? u16(d + 2) : 0;
        guint16 ixfe = len >= 6 ? u16(d + 4) : 0;
        switch (type) {
            case REC_LABELSST:
                if (len >= 10) {
                    guint32 isst = u32(d + 6);
                    if (isst < b->strings->len) set_cell(&g, row, col, g_strdup(g_ptr_array_index(b->strings, isst)));
                }
                break;
            case REC_LABEL:
            case REC_RSTRING:
                set_cell(&g, row, col, read_string(b, d + 6, d + len, 2));
                break;
            case REC_NUMBER:
                if (len >= 14) set_cell(&g, row, col, number_text(b, ixfe, f64(d + 6)));
                break;
            case REC_RK:
                if (len >= 10) set_cell(&g, row, col, number_text(b, ixfe, rk_value(u32(d + 6))));
                break;
            case REC_MULRK:
                for (guint k = 0; 4 + 6 * (k + 1) + 2 <= len; k++) {
                    const guint8* e = d + 4 + 6 * k;
                    set_cell(&g, row, col + k, number_text(b, u16(e), rk_value(u32(e + 2))));
                }
                break;
            case REC_BOOLERR:
                if (len >= 8) set_cell(&g, row, col, d[7] ? g_strdup(error_text(d[6])) : g_strdup(d[6] ? "TRUE" : "FALSE"));
                break;
            case REC_FORMULA:
                if (len >= 14) {
                    const guint8* v = d + 6;
                    if (u16(v + 6) == 0xFFFF) {
                        switch (v[0]) {
                            case 0: string_row = (gint)row; string_col = (gint)col; break;
                            case 1: set_cell(&g, row, col, g_strdup(v[2] ? "TRUE" : "FALSE")); break;
                            case 2: set_cell(&g, row, col, g_strdup(error_text(v[2]))); break;
                            default: break;
                        }
                    } else {
                        set_cell(&g, row, col, number_text(b, ixfe, f64(v)));
                    }
                }
                break;
            case REC_STRING:
                if (string_row >= 0) {
                    set_cell(&g, (guint)string_row, (guint)string_col, read_string(b, d, d + len, 2));
                    string_row = string_col = -1;
                }
                break;
            default:
                break;
        }
    }
    /* Rows of n_cols cells, trailing empty rows dropped */
    guint last = 0;
    for (guint r = 0; r < g.rows->len; r++) {
        if (g_ptr_array_index(g.rows, r) != NULL) last = r + 1;
    }
    for (guint r = 0; r < last; r++) {
        GPtrArray* cells = g_ptr_array_index(g.rows, r);
        gchar** out = g_new0(gchar*, g.n_cols + 1);
        for (guint c = 0; c < g.n_cols; c++) {
            const gchar* t = cells != NULL && c < cells->len ? g_ptr_array_index(cells, c) : NULL;
            out[c] = g_strdup(t != NULL ? t : "");
        }
        g_ptr_array_add(sheet->rows, out);
    }
    sheet->n_cols = g.n_cols;
    sheet->truncated = g.truncated;
    sheet->loaded = TRUE;
    g_ptr_array_unref(g.rows);
}

Workbook* xls_reader_read(OleReader* ole, guint max_sheets, guint max_rows, gboolean* encrypted, GError** error) {
    if (encrypted != NULL) *encrypted = FALSE;
    gboolean biff8 = ole_reader_has(ole, "Workbook");
    GBytes* stream = ole_reader_read(ole, biff8 ? "Workbook" : "Book", BOOK_MAX_STREAM, error);
    if (stream == NULL) return NULL;
    Book b = {0};
    b.data = g_bytes_get_data(stream, &b.len);
    b.codepage = 1252;
    b.strings = g_ptr_array_new_with_free_func(g_free);
    b.formats = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    b.xf_formats = g_array_new(FALSE, FALSE, sizeof(guint16));
    GArray* positions = g_array_new(FALSE, FALSE, sizeof(guint32));
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    GArray* kinds = g_array_new(FALSE, FALSE, sizeof(guint8));
    gsize pos = 0;
    guint16 type, len;
    const guint8* d;
    Workbook* book = NULL;
    gboolean ok = next_record(&b, &pos, &type, &d, &len) && type == REC_BOF && len >= 4;
    if (ok) {
        guint16 vers = u16(d);
        b.biff8 = vers >= 0x0600;
    }
    while (ok && next_record(&b, &pos, &type, &d, &len)) {
        if (type == REC_EOF) break;
        switch (type) {
            case REC_FILEPASS:
                if (encrypted != NULL) *encrypted = TRUE;
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "the workbook is encrypted");
                ok = FALSE;
                break;
            case REC_CODEPAGE:
                if (len >= 2) b.codepage = u16(d);
                break;
            case REC_DATEMODE:
                if (len >= 2) b.date1904 = u16(d) != 0;
                break;
            case REC_BOUNDSHEET:
                if (len >= 7) {
                    guint32 p = u32(d);
                    guint8 kind = d[5];
                    gchar* name = read_string(&b, d + 6, d + len, 1);
                    g_array_append_val(positions, p);
                    g_array_append_val(kinds, kind);
                    g_ptr_array_add(names, name != NULL ? name : g_strdup("Sheet"));
                }
                break;
            case REC_SST:
                read_sst(&b, d, len, &pos);
                break;
            case REC_FORMAT:
            case REC_FORMAT_OLD:
                if (len >= 3) {
                    guint16 ifmt = u16(d);
                    gchar* code = read_string(&b, d + 2, d + len, b.biff8 ? 2 : 1);
                    if (code != NULL) g_hash_table_replace(b.formats, GUINT_TO_POINTER(ifmt), code);
                }
                break;
            case REC_XF:
                if (len >= 4) {
                    guint16 ifmt = u16(d + 2);
                    g_array_append_val(b.xf_formats, ifmt);
                }
                break;
            default:
                break;
        }
    }
    if (ok && names->len == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "the workbook has no sheet");
        ok = FALSE;
    } else if (!ok && error != NULL && *error == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not an Excel workbook");
    }
    if (ok) {
        book = spreadsheet_workbook_new(b.biff8 ? "Excel 97-2003 workbook" : "Excel 5.0/95 workbook");
        guint loaded = 0;
        for (guint i = 0; i < names->len; i++) {
            Sheet* sheet = spreadsheet_sheet_add(book, g_ptr_array_index(names, i));
            guint8 kind = g_array_index(kinds, guint8, i);
            if (kind == 0 && loaded < max_sheets) {
                read_sheet(&b, g_array_index(positions, guint32, i), sheet, max_rows);
                loaded++;
            }
        }
    }
    g_array_free(positions, TRUE);
    g_array_free(kinds, TRUE);
    g_ptr_array_unref(names);
    g_ptr_array_unref(b.strings);
    g_hash_table_destroy(b.formats);
    g_array_free(b.xf_formats, TRUE);
    g_bytes_unref(stream);
    return book;
}
