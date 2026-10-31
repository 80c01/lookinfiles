/* Structured preview builders, one per FileKind (see preview_formats.h). */
#include "preview_formats.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "archive_listing.h"
#include "card_view.h"
#include "cert_view.h"
#include "csv.h"
#include "diff_view.h"
#include "docx.h"
#include "elf_info.h"
#include "epub.h"
#include "font_info.h"
#include "gettext_view.h"
#include "hex_dump.h"
#include "json_view.h"
#include "log_view.h"
#include "man_view.h"
#include "notebook.h"
#include "odf.h"
#include "mail_box.h"
#include "mail_message.h"
#include "mail_store.h"
#include "mail_tnef.h"
#include "mail_view.h"
#include "office97.h"
#include "pptx.h"
#include "rtf.h"
#include "spreadsheet.h"
#include "text_encoding.h"
#include "sqlite_view.h"

typedef PreviewDoc* (*PreviewBuildFunc)(const PreviewInput* in, GCancellable* cancellable,
                                        GError** error);

gchar* preview_format_size(gint64 size) {
    return size < 0 ? g_strdup("") : g_format_size((guint64)size);
}

/* "12,345" */
static gchar* format_count(guint64 n) {
    gchar* digits = g_strdup_printf("%" G_GUINT64_FORMAT, n);
    gsize len = strlen(digits);
    GString* out = g_string_new(NULL);
    for (gsize i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) {
            g_string_append_c(out, ',');
        }
        g_string_append_c(out, digits[i]);
    }
    g_free(digits);
    return g_string_free(out, FALSE);
}

/* TRUE if the first bytes are an OpenDocument package (mimetype entry
 * first); the bytes hold NULs, so no string search */
static gboolean is_opendocument(const PreviewInput* in) {
    static const gchar NEEDLE[] = "application/vnd.oasis.opendocument";
    gsize n = sizeof(NEEDLE) - 1;
    for (gsize i = 0; in->head != NULL && i + n <= in->head_len; i++) {
        if (memcmp(in->head + i, NEEDLE, n) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Dimmed note saying only the start of the file was read */
static void note_truncated_text(PreviewDoc* doc, const PreviewInput* in) {
    if (in->text_truncated) {
        gchar* shown = g_format_size(in->text_len);
        gchar* total = preview_format_size(in->size);
        gchar* note = g_strdup_printf("Only the first %s of %s were read.", shown, total);
        preview_doc_note(doc, note);
        g_free(note);
        g_free(total);
        g_free(shown);
    }
}

/* Append the text and spans of src to doc */
static void append_doc(PreviewDoc* doc, const PreviewDoc* src) {
    gint base = preview_doc_offset(doc);
    preview_doc_append_len(doc, src->text->str, (gssize)src->text->len);
    for (guint i = 0; i < src->spans->len; i++) {
        const PreviewSpan* span = &g_array_index(src->spans, PreviewSpan, i);
        if (span->style == PREVIEW_STYLE_FONT) {
            continue;
        }
        preview_doc_add_span(doc, base + span->start, base + span->end, span->style);
    }
}

/* --- CSV / TSV --- */

#define TABLE_MAX_ROWS 1000
#define TABLE_MAX_COLS 100

static PreviewDoc* build_csv(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    gchar sep = csv_sniff_separator(in->text, in->text_len, in->name);
    g_autoptr(CsvData) data = csv_parse(in->text, in->text_len, sep, TABLE_MAX_ROWS + 1);
    if (data->n_rows == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "no rows");
        return NULL;
    }
    gboolean header = csv_has_header(data);
    guint n_cols = MIN(data->n_cols, TABLE_MAX_COLS);
    guint body_rows = data->n_rows - (header ? 1 : 0);

    PreviewDoc* doc = preview_doc_new();
    gchar* rows = format_count(body_rows);
    gchar* cols = format_count(data->n_cols);
    preview_doc_appendf(doc, PREVIEW_STYLE_DIM, "%s %s × %s %s · %s-separated%s\n", rows,
                        body_rows == 1 ? "row" : "rows", cols, data->n_cols == 1 ? "column" : "columns",
                        csv_separator_name(sep), in->text_truncated ? " (start of the file)" : "");
    g_free(cols);
    g_free(rows);
    note_truncated_text(doc, in);
    preview_doc_append_len(doc, "\n", 1);

    g_autoptr(PreviewTable) table = preview_table_new(n_cols);
    preview_table_set_row_numbers(table, TRUE);
    guint first = 0;
    if (header) {
        preview_table_set_header(table, (const gchar* const*)g_ptr_array_index(data->rows, 0));
        first = 1;
    }
    for (guint r = first; r < data->rows->len && r - first < TABLE_MAX_ROWS; r++) {
        gchar** cells = g_ptr_array_index(data->rows, r);
        preview_table_add_row(table, (const gchar* const*)cells, g_strv_length(cells));
    }
    preview_doc_add_table(doc, table);
    if (body_rows > TABLE_MAX_ROWS) {
        gchar* shown = format_count(TABLE_MAX_ROWS);
        gchar* note = g_strdup_printf("\nShowing the first %s rows.", shown);
        preview_doc_note(doc, note);
        g_free(note);
        g_free(shown);
    }
    if (data->n_cols > TABLE_MAX_COLS) {
        preview_doc_note(doc, "Showing the first 100 columns.");
    }
    return doc;
}

/* --- JSON --- */

static PreviewDoc* build_json(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    g_autoptr(PreviewDoc) body = preview_doc_new();
    JsonViewInfo info;
    if (!json_view_append(body, in->text, in->text_len, &info, error)) {
        return NULL;
    }
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* count = format_count(info.lines ? info.n_values : info.n_root_items);
    if (info.lines) {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, "JSON Lines · %s records\n", count);
    } else if (info.root == '{') {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, "Object · %s %s\n", count,
                            info.n_root_items == 1 ? "key" : "keys");
    } else if (info.root == '[') {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, "Array · %s %s\n", count,
                            info.n_root_items == 1 ? "item" : "items");
    }
    g_free(count);
    note_truncated_text(doc, in);
    if (doc->text->len > 0) {
        preview_doc_append_len(doc, "\n", 1);
    }
    append_doc(doc, body);
    return doc;
}

/* --- Diff / patch --- */

static PreviewDoc* build_diff(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    note_truncated_text(doc, in);
    if (!diff_view_append(doc, in->text, in->text_len)) {
        preview_doc_free(doc);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "no file changes found");
        return NULL;
    }
    return doc;
}

/* --- Archives --- */

#define ARCHIVE_MAX_ENTRIES 1000

/* "2026-10-29 10:00" (utc: the time is already local, as in ZIP files) */
static gchar* format_time(gint64 t, gboolean utc) {
    if (t < 0) {
        return g_strdup("");
    }
    GDateTime* dt = utc ? g_date_time_new_from_unix_utc(t) : g_date_time_new_from_unix_local(t);
    if (dt == NULL) {
        return g_strdup("");
    }
    gchar* s = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    g_date_time_unref(dt);
    return s;
}

static gchar* format_percent_saved(guint64 packed, guint64 size) {
    if (size == 0 || packed > size) {
        return NULL;
    }
    return g_strdup_printf("%.0f %% saved", 100.0 * (gdouble)(size - packed) / (gdouble)size);
}

/* Decompressed content of a single compressed file: its own structured
 * preview (CSV, JSON, log...), its text, or a hex dump */
static void append_inner_content(PreviewDoc* doc, GBytes* bytes, const gchar* inner_name,
                                 const gchar* outer_name, gint64 inner_size) {
    gsize len = 0;
    const guchar* data = g_bytes_get_data(bytes, &len);
    preview_doc_section(doc, "Content");
    /* "notes.csv.gz" names "notes.csv" when the header has no name */
    gchar* name = inner_name != NULL ? g_strdup(inner_name) : g_strdup(outer_name);
    gchar* dot = strrchr(name, '.');
    if (inner_name == NULL && dot != NULL && dot != name) {
        *dot = '\0';
    }
    gboolean text = len > 0 && file_content_is_text(data, MIN(len, (gsize)FILE_SNIFF_BYTES));
    FileKind kind = file_kind_detect(name, data, MIN(len, (gsize)FILE_SNIFF_BYTES));
    if (text && kind != FILE_KIND_ARCHIVE && file_kind_is_text(kind) && preview_format_has_builder(kind)) {
        gchar* utf8 = g_utf8_make_valid((const gchar*)data, (gssize)len);
        PreviewInput in = { .path = NULL, .name = name, .size = inner_size, .kind = kind,
                            .head = (const guchar*)utf8, .head_len = MIN(strlen(utf8), (gsize)FILE_SNIFF_BYTES),
                            .text = utf8, .text_len = strlen(utf8),
                            .text_truncated = inner_size > (gint64)len };
        g_autoptr(PreviewDoc) inner = preview_format_build(&in, NULL, NULL);
        g_free(utf8);
        if (inner != NULL && inner->format == PREVIEW_DOC_TEXT) {
            append_doc(doc, inner);
            g_free(name);
            return;
        }
    }
    g_free(name);
    if (len == 0) {
        preview_doc_note(doc, "(empty)");
    } else if (text) {
        preview_doc_append_len(doc, (const gchar*)data, (gssize)len);
        preview_doc_append_len(doc, "\n", 1);
    } else {
        g_autoptr(PreviewDoc) hex = preview_format_hex_dump("", FILE_KIND_BINARY, -1, data,
                                                            MIN(len, (gsize)4096), NULL);
        /* Skip the header line of the hex dump */
        const gchar* body = strstr(hex->text->str, "\n\n");
        preview_doc_append_len(doc, body != NULL ? body + 2 : hex->text->str, -1);
    }
}

static PreviewDoc* build_archive(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    GError* err = NULL;
    g_autoptr(ArchiveListing) l = archive_list(in->path, ARCHIVE_MAX_ENTRIES, cancellable, &err);
    const gchar* format = l != NULL ? l->format : archive_listing_format_name(in->head, in->head_len);
    if (l == NULL && (format == NULL || !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))) {
        g_propagate_error(error, err);
        return NULL;
    }
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* size = preview_format_size(in->size);
    gchar* subtitle = g_strdup_printf("%s · %s", format, size);
    gchar* title_format = g_strdup(subtitle);
    title_format[0] = g_ascii_toupper(title_format[0]);
    preview_doc_title(doc, in->name, title_format);
    g_free(title_format);
    g_free(subtitle);
    g_free(size);
    if (l == NULL) {
        g_clear_error(&err);
        preview_doc_note(doc, "Listing the content of this format is not supported yet.");
        return doc;
    }

    if (l->single) {
        preview_doc_field(doc, "Original name", l->inner_name);
        if (l->inner_size >= 0) {
            gchar* inner = g_format_size((guint64)l->inner_size);
            preview_doc_field(doc, "Original size", inner);
            g_free(inner);
            gchar* saved = in->size >= 0 ? format_percent_saved((guint64)in->size, (guint64)l->inner_size) : NULL;
            preview_doc_field(doc, "Compression", saved);
            g_free(saved);
        }
        append_inner_content(doc, l->inner_head, l->inner_name, in->name, l->inner_size);
        return doc;
    }

    gchar* files = format_count(l->n_files);
    gchar* contents = g_strdup_printf("%s%s %s, %u %s", l->incomplete ? "at least " : "", files,
                                      l->n_files == 1 ? "file" : "files", l->n_dirs,
                                      l->n_dirs == 1 ? "folder" : "folders");
    preview_doc_field(doc, "Contents", contents);
    g_free(contents);
    g_free(files);
    gchar* unpacked = g_format_size(l->total_size);
    preview_doc_field(doc, "Unpacked", unpacked);
    g_free(unpacked);
    gboolean zip = g_str_has_prefix(l->format, "ZIP");
    if (zip) {
        gchar* saved = format_percent_saved(l->total_packed, l->total_size);
        preview_doc_field(doc, "Compression", saved);
        g_free(saved);
    }
    if (l->manifest != NULL) {
        gchar** lines = g_strsplit(l->manifest, "\n", -1);
        for (gint i = 0; lines[i] != NULL; i++) {
            gchar* colon = strstr(lines[i], ": ");
            if (colon != NULL && (g_str_has_prefix(lines[i], "Main-Class") ||
                                  g_str_has_prefix(lines[i], "Implementation-") ||
                                  g_str_has_prefix(lines[i], "Bundle-Name") ||
                                  g_str_has_prefix(lines[i], "Bundle-Version") ||
                                  g_str_has_prefix(lines[i], "Automatic-Module-Name"))) {
                *colon = '\0';
                preview_doc_field(doc, lines[i], colon + 2);
            }
        }
        g_strfreev(lines);
    }
    preview_doc_append_len(doc, "\n", 1);

    guint n_cols = zip ? 4 : 3;
    g_autoptr(PreviewTable) table = preview_table_new(n_cols);
    const gchar* zip_header[] = { "Name", "Size", "Packed", "Modified" };
    const gchar* header[] = { "Name", "Size", "Modified" };
    preview_table_set_header(table, zip ? zip_header : header);
    preview_table_set_right_aligned(table, 1);
    if (zip) {
        preview_table_set_right_aligned(table, 2);
    }
    preview_table_set_max_cell_width(table, 80);
    for (guint i = 0; i < l->entries->len; i++) {
        const ArchiveEntry* e = &g_array_index(l->entries, ArchiveEntry, i);
        gchar* name = e->link != NULL ? g_strdup_printf("%s → %s", e->path, e->link)
                      : e->encrypted ? g_strdup_printf("%s (encrypted)", e->path)
                      : g_strdup(e->path);
        gboolean dir = (e->type == ARCHIVE_ENTRY_DIR);
        gchar* esize = dir ? g_strdup("") : g_format_size(e->size);
        gchar* packed = (dir || e->packed < 0) ? g_strdup("") : g_format_size((guint64)e->packed);
        gchar* mtime = format_time(e->mtime, zip);
        const gchar* zip_row[] = { name, esize, packed, mtime };
        const gchar* row[] = { name, esize, mtime };
        preview_table_add_row(table, zip ? zip_row : row, n_cols);
        g_free(mtime);
        g_free(packed);
        g_free(esize);
        g_free(name);
    }
    preview_doc_add_table(doc, table);
    if (l->n_entries > l->entries->len || l->incomplete) {
        gchar* shown = format_count(l->entries->len);
        gchar* note = g_strdup_printf("\nShowing the first %s entries%s.", shown,
                                      l->incomplete ? " (the archive was not read to the end)" : "");
        preview_doc_note(doc, note);
        g_free(note);
        g_free(shown);
    }
    return doc;
}

/* --- Certificates and keys --- */

static PreviewDoc* build_certificate(const PreviewInput* in, GCancellable* cancellable,
                                     GError** error) {
    (void)cancellable;
    gchar* data = NULL;
    gsize len = 0;
    if (in->text != NULL) {
        data = g_strndup(in->text, in->text_len);
        len = in->text_len;
    } else if (!g_file_get_contents(in->path, &data, &len, error)) {
        return NULL;
    }
    g_autoptr(PreviewDoc) body = preview_doc_new();
    gboolean ok = cert_view_append(body, (const guchar*)data, len, g_get_real_time() / G_USEC_PER_SEC,
                                   error);
    g_free(data);
    if (!ok) {
        return NULL;
    }
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* size = preview_format_size(in->size);
    gchar* subtitle = g_strdup_printf("%s · %s", in->text != NULL ? "PEM" : "DER", size);
    preview_doc_title(doc, in->name, subtitle);
    g_free(subtitle);
    g_free(size);
    append_doc(doc, body);
    return doc;
}

/* --- Calendars, contacts, desktop entries --- */

typedef gboolean (*CardAppendFunc)(PreviewDoc* doc, const gchar* text, gsize len);

static PreviewDoc* build_cards(const PreviewInput* in, CardAppendFunc append, GError** error) {
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    note_truncated_text(doc, in);
    if (!append(doc, in->text, in->text_len)) {
        preview_doc_free(doc);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "nothing to show");
        return NULL;
    }
    return doc;
}

static PreviewDoc* build_calendar(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    return build_cards(in, card_view_append_calendar, error);
}

static PreviewDoc* build_contacts(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    return build_cards(in, card_view_append_contacts, error);
}

static PreviewDoc* build_desktop(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    return build_cards(in, card_view_append_desktop, error);
}

/* --- Logs --- */

/* Bytes read from the end of a log larger than what the viewer read */
#define LOG_TAIL_BYTES (256 * 1024)

/* Last bytes of a file, starting at a line start; NULL if it cannot be read */
static gchar* read_tail(const gchar* path, gint64 size, gsize max, gsize* len) {
    FILE* fp = g_fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    gint64 from = size > (gint64)max ? size - (gint64)max : 0;
    gchar* data = g_malloc(max + 1);
    gsize n = 0;
    if (fseeko(fp, (off_t)from, SEEK_SET) == 0) {
        n = fread(data, 1, max, fp);
    }
    fclose(fp);
    data[n] = '\0';
    gsize skip = 0;
    if (from > 0) {
        const gchar* nl = memchr(data, '\n', n);
        skip = nl != NULL ? (gsize)(nl - data) + 1 : 0;
    }
    memmove(data, data + skip, n - skip + 1);
    *len = n - skip;
    gchar* valid = g_utf8_make_valid(data, (gssize)*len);
    g_free(data);
    *len = strlen(valid);
    return valid;
}

static PreviewDoc* build_log(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    (void)error;
    gchar* tail = NULL;
    gsize tail_len = 0;
    if (in->text_truncated && in->path != NULL) {
        tail = read_tail(in->path, in->size, LOG_TAIL_BYTES, &tail_len);
    }
    const gchar* text = tail != NULL ? tail : in->text;
    gsize len = tail != NULL ? tail_len : in->text_len;

    g_autoptr(PreviewDoc) body = preview_doc_new();
    LogStats stats;
    log_view_append(body, text, len, &stats);
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    doc->scroll_to_end = TRUE;
    gchar* lines = format_count(stats.lines);
    gint start = preview_doc_offset(doc);
    preview_doc_appendf(doc, PREVIEW_STYLE_PLAIN, "%s%s %s", (tail != NULL || in->text_truncated) ? "last " : "",
                        lines, stats.lines == 1 ? "line" : "lines");
    preview_doc_add_span(doc, start, preview_doc_offset(doc), PREVIEW_STYLE_DIM);
    if (stats.errors > 0) {
        preview_doc_append(doc, PREVIEW_STYLE_DIM, " · ");
        preview_doc_appendf(doc, PREVIEW_STYLE_ERROR, "%u %s", stats.errors, stats.errors == 1 ? "error" : "errors");
    }
    if (stats.warnings > 0) {
        preview_doc_append(doc, PREVIEW_STYLE_DIM, " · ");
        preview_doc_appendf(doc, PREVIEW_STYLE_WARNING, "%u %s", stats.warnings,
                            stats.warnings == 1 ? "warning" : "warnings");
    }
    preview_doc_append_len(doc, "\n", 1);
    g_free(lines);
    if (tail != NULL) {
        gchar* shown = g_format_size(tail_len);
        gchar* total = preview_format_size(in->size);
        gchar* note = g_strdup_printf("Showing the end of the file: the last %s of %s.", shown, total);
        preview_doc_note(doc, note);
        g_free(note);
        g_free(total);
        g_free(shown);
    } else {
        note_truncated_text(doc, in);
    }
    preview_doc_append_len(doc, "\n", 1);
    append_doc(doc, body);
    g_free(tail);
    return doc;
}

/* --- gettext catalogs --- */

static PreviewDoc* build_translation(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    g_autoptr(GettextCatalog) catalog = NULL;
    if (in->text != NULL) {
        catalog = gettext_parse_po(in->text, in->text_len, error);
    } else {
        gchar* data = NULL;
        gsize len = 0;
        if (in->size > 64 * 1024 * 1024 || !g_file_get_contents(in->path, &data, &len, error)) {
            if (error != NULL && *error == NULL) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "catalog too large");
            }
            return NULL;
        }
        catalog = gettext_parse_mo((const guchar*)data, len, error);
        g_free(data);
    }
    if (catalog == NULL) {
        return NULL;
    }
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* size = preview_format_size(in->size);
    gchar* subtitle = g_strdup_printf("gettext %s · %s", in->text != NULL ? "translations (.po)" : "compiled translations (.mo)",
                                      size);
    preview_doc_title(doc, in->name, subtitle);
    g_free(subtitle);
    g_free(size);
    note_truncated_text(doc, in);
    gettext_view_append(doc, catalog);
    return doc;
}

/* --- SQLite databases --- */

static PreviewDoc* build_database(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    g_autoptr(PreviewDoc) body = preview_doc_new();
    if (!sqlite_view_append(body, in->path, in->head, in->head_len, error)) {
        return NULL;
    }
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* size = preview_format_size(in->size);
    gchar* subtitle = g_strdup_printf("SQLite database · %s", size);
    preview_doc_title(doc, in->name, subtitle);
    g_free(subtitle);
    g_free(size);
    append_doc(doc, body);
    return doc;
}

/* --- Fonts --- */

static const gchar* const FONT_SAMPLE = "The quick brown fox jumps over the lazy dog";

static PreviewDoc* build_font(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    g_autoptr(FontInfo) info = font_info_read_file(in->path, error);
    if (info == NULL) {
        return NULL;
    }
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    gchar* size = preview_format_size(in->size);
    const gchar* style = info->style != NULL ? info->style : font_info_weight_name(info->weight);
    gchar* subtitle = g_strdup_printf("%s font%s%s · %s", info->format, style != NULL ? " · " : "",
                                      style != NULL ? style : "", size);
    preview_doc_title(doc, info->family != NULL ? info->family : in->name, subtitle);
    g_free(subtitle);
    g_free(size);
    preview_doc_field(doc, "Full name", info->full_name);
    preview_doc_field(doc, "Version", info->version);
    preview_doc_field(doc, "Designer", info->designer);
    preview_doc_field(doc, "Foundry", info->manufacturer);
    if (info->n_glyphs > 0) {
        gchar* glyphs = format_count(info->n_glyphs);
        preview_doc_field(doc, "Glyphs", glyphs);
        g_free(glyphs);
    }
    if (info->n_fonts > 1) {
        gchar* fonts = g_strdup_printf("%u (the first one is shown)", info->n_fonts);
        preview_doc_field(doc, "Fonts", fonts);
        g_free(fonts);
    }
    preview_doc_field(doc, "Copyright", info->copyright);
    preview_doc_field(doc, "License URL", info->license_url);

    if (info->family == NULL) {
        preview_doc_note(doc, "\nNo sample: the font names cannot be read (WOFF2 tables are compressed).");
        return doc;
    }
    /* Samples in the font itself: family, weight and slant from the file */
    preview_doc_add_font_file(doc, in->path);
    GString* face = g_string_new(info->family);
    const gchar* weight = font_info_weight_name(info->weight);
    g_string_append_printf(face, ", %s", weight != NULL ? weight : "Regular");
    if (style != NULL && (strstr(style, "Italic") != NULL || strstr(style, "Oblique") != NULL)) {
        g_string_append(face, " Italic");
    }
    static const gint SIZES[] = { 12, 18, 24, 36, 48 };
    const gchar* const LINES[] = { "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz",
                                   "0123456789 .,;:!? &@#%$€ ()[]{} «»“”" };
    preview_doc_section(doc, "Sample");
    for (gsize i = 0; i < G_N_ELEMENTS(LINES); i++) {
        gint start = preview_doc_offset(doc);
        preview_doc_append(doc, PREVIEW_STYLE_PLAIN, LINES[i]);
        gchar* desc = g_strdup_printf("%s 20", face->str);
        preview_doc_add_font_span(doc, start, preview_doc_offset(doc), desc);
        g_free(desc);
        preview_doc_append_len(doc, "\n", 1);
    }
    preview_doc_append_len(doc, "\n", 1);
    for (gsize i = 0; i < G_N_ELEMENTS(SIZES); i++) {
        preview_doc_appendf(doc, PREVIEW_STYLE_DIM, "%2d  ", SIZES[i]);
        gint start = preview_doc_offset(doc);
        preview_doc_append(doc, PREVIEW_STYLE_PLAIN, FONT_SAMPLE);
        gchar* desc = g_strdup_printf("%s %d", face->str, SIZES[i]);
        preview_doc_add_font_span(doc, start, preview_doc_offset(doc), desc);
        g_free(desc);
        preview_doc_append_len(doc, "\n", 1);
    }
    g_string_free(face, TRUE);
    if (info->license != NULL) {
        preview_doc_section(doc, "License");
        gchar* license = g_utf8_substring(info->license, 0, 600);
        preview_doc_append(doc, PREVIEW_STYLE_DIM, license);
        if (g_utf8_strlen(info->license, -1) > 600) {
            preview_doc_append(doc, PREVIEW_STYLE_DIM, "…");
        }
        preview_doc_append_len(doc, "\n", 1);
        g_free(license);
    }
    return doc;
}

/* --- Rich Text Format --- */

static PreviewDoc* build_rtf(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    /* Pictures make RTF files large: read the whole file (bounded) */
    gchar* whole = NULL;
    gsize len = in->text_len;
    const gchar* text = in->text;
    if (in->text_truncated && in->path != NULL && in->size <= 32 * 1024 * 1024) {
        if (!g_file_get_contents(in->path, &whole, &len, error)) {
            return NULL;
        }
        text = whole;
    }
    gchar* html = rtf_to_html(text, len, PREVIEW_FORMAT_MAX_TEXT, error);
    g_free(whole);
    return html != NULL ? preview_doc_new_source(PREVIEW_DOC_HTML, html) : NULL;
}

/* --- E-books --- */

static PreviewDoc* build_ebook(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    gchar* html = epub_file_to_html(in->path, PREVIEW_FORMAT_MAX_TEXT, NULL, error);
    return html != NULL ? preview_doc_new_source(PREVIEW_DOC_HTML, html) : NULL;
}

/* --- Presentations --- */

static PreviewDoc* build_presentation(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    if (!is_opendocument(in)) {
        gchar* size = preview_format_size(in->size);
        PreviewDoc* doc = pptx_file_preview(in->path, in->name, size, NULL, error);
        g_free(size);
        return doc;
    }
    OdfInfo info = { 0 };
    gchar* html = odf_file_to_html(in->path, PREVIEW_FORMAT_MAX_TEXT, &info, error);
    if (html == NULL) {
        return NULL;
    }
    /* Slide count in the metadata banner */
    guint slides = info.n_slides;
    gchar* count = g_strdup_printf("<meta name=\"description\" content=\"Presentation · %u %s\"></head>", slides,
                                   slides == 1 ? "slide" : "slides");
    gchar* head_end = strstr(html, "</head>");
    gchar* page;
    if (head_end != NULL) {
        *head_end = '\0';
        page = g_strconcat(html, count, head_end + 7, NULL);
    } else {
        page = g_strdup(html);
    }
    g_free(count);
    g_free(html);
    return preview_doc_new_source(PREVIEW_DOC_HTML, page);
}

/* --- Spreadsheets --- */

#define SHEETS_SHOWN 3
#define SHEET_ROWS 300

static PreviewDoc* build_spreadsheet(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    g_autoptr(Workbook) book = spreadsheet_read(in->path, SHEETS_SHOWN, SHEET_ROWS, error);
    if (book == NULL) {
        return NULL;
    }
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    gchar* size = preview_format_size(in->size);
    gchar* subtitle = g_strdup_printf("%s · %u %s · %s", book->format, book->sheets->len,
                                      book->sheets->len == 1 ? "sheet" : "sheets", size);
    preview_doc_title(doc, in->name, subtitle);
    g_free(subtitle);
    g_free(size);
    if (book->sheets->len > 1) {
        GString* names = g_string_new(NULL);
        for (guint i = 0; i < book->sheets->len; i++) {
            g_string_append_printf(names, "%s%s", i > 0 ? " · " : "", ((Sheet*)g_ptr_array_index(book->sheets, i))->name);
        }
        preview_doc_field(doc, "Sheets", names->str);
        g_string_free(names, TRUE);
    }
    spreadsheet_append_sheets(doc, book, SHEETS_SHOWN, SHEET_ROWS);
    return doc;
}

/* --- Jupyter notebooks --- */

static PreviewDoc* build_notebook(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    /* A notebook with images is often larger than the text the viewer
     * reads: read it whole (bounded) */
    gchar* whole = NULL;
    gsize len = in->text_len;
    const gchar* text = in->text;
    if (in->text_truncated && in->path != NULL && in->size <= 32 * 1024 * 1024) {
        if (!g_file_get_contents(in->path, &whole, &len, error)) {
            return NULL;
        }
        text = whole;
    }
    NotebookInfo info = { 0 };
    gchar* md = notebook_to_markdown(text, len, &info, error);
    g_free(whole);
    if (md == NULL) {
        return NULL;
    }
    GString* page = g_string_new(NULL);
    g_string_append_printf(page, "*Jupyter notebook · %u cells (%u code)%s%s*\n\n", info.n_cells, info.n_code,
                           info.kernel != NULL ? " · " : "", info.kernel != NULL ? info.kernel : "");
    g_string_append(page, md);
    g_free(md);
    notebook_info_clear(&info);
    return preview_doc_new_source(PREVIEW_DOC_MARKDOWN, g_string_free(page, FALSE));
}

/* --- Manual pages --- */

#define MAN_MAX_TEXT (2 * 1024 * 1024)

/* Start of a gzipped file, decompressed (at most max bytes), valid UTF-8 */
static gchar* gunzip_text(const gchar* path, gsize max, GError** error) {
    GFile* file = g_file_new_for_path(path);
    GFileInputStream* raw = g_file_read(file, NULL, error);
    g_object_unref(file);
    if (raw == NULL) {
        return NULL;
    }
    GZlibDecompressor* dec = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP);
    GInputStream* in = g_converter_input_stream_new(G_INPUT_STREAM(raw), G_CONVERTER(dec));
    g_object_unref(dec);
    g_object_unref(raw);
    gchar* data = g_malloc(max + 1);
    gsize len = 0;
    gboolean ok = g_input_stream_read_all(in, data, max, &len, NULL, error);
    g_object_unref(in);
    if (!ok && len == 0) {
        g_free(data);
        return NULL;
    }
    g_clear_error(error);
    gchar* text = g_utf8_make_valid(data, (gssize)len);
    g_free(data);
    return text;
}

static PreviewDoc* build_manpage(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    gchar* unzipped = NULL;
    const gchar* text = in->text;
    gsize len = in->text_len;
    if (text == NULL) {
        if (in->head_len < 2 || in->head[0] != 0x1F || in->head[1] != 0x8B) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "only gzip-compressed pages can be read");
            return NULL;
        }
        unzipped = gunzip_text(in->path, MAN_MAX_TEXT, error);
        if (unzipped == NULL) {
            return NULL;
        }
        text = unzipped;
        len = strlen(unzipped);
    }
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    gboolean ok = man_view_append(doc, text, len);
    g_free(unzipped);
    if (!ok) {
        preview_doc_free(doc);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not a manual page");
        return NULL;
    }
    return doc;
}

/* --- ELF executables, libraries, objects --- */

static PreviewDoc* build_elf(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    gchar* why = NULL;
    ElfInfo* info = elf_info_read_file(in->path, &why);
    if (info == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s",
                    why != NULL ? why : "not a valid ELF file");
        g_free(why);
        return NULL;
    }
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;
    doc->label_width = ELF_INFO_LABEL_WIDTH;
    gchar* size = preview_format_size(in->size);
    gchar* subtitle = g_strdup_printf("%s · %s", elf_info_type_name(info), size);
    preview_doc_title(doc, in->name, subtitle);

    /* Field lines: label padded to ELF_INFO_LABEL_WIDTH; list items start with
     * two spaces; any other non-empty line is a section title */
    gchar* summary = elf_info_format(info);
    gchar** lines = g_strsplit(summary, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        const gchar* line = lines[i];
        gsize len = strlen(line);
        if (lines[i + 1] == NULL && len == 0) {
            break;
        }
        if (len > ELF_INFO_LABEL_WIDTH && line[0] != ' ' &&
            line[ELF_INFO_LABEL_WIDTH - 1] == ' ' && line[ELF_INFO_LABEL_WIDTH] == ' ') {
            gchar* label = g_strndup(line, ELF_INFO_LABEL_WIDTH);
            preview_doc_append(doc, PREVIEW_STYLE_DIM, label);
            preview_doc_append_len(doc, line + ELF_INFO_LABEL_WIDTH, -1);
            g_free(label);
        } else if (len > 0 && line[0] != ' ') {
            preview_doc_append(doc, PREVIEW_STYLE_SECTION, line);
        } else {
            preview_doc_append_len(doc, line, -1);
        }
        preview_doc_append_len(doc, "\n", 1);
    }
    g_strfreev(lines);
    g_free(summary);
    g_free(subtitle);
    g_free(size);
    elf_info_free(info);
    return doc;
}

/* --- Word documents --- */

/* --- Mail --- */

#define MAIL_MAX_FILE (32 * 1024 * 1024)

#define MAILBOX_ROWS_SHOWN 500

static void mailbox_row(PreviewTable* table, const MailBoxEntry* e, guint number) {
    gchar* num = g_strdup_printf("%u", number);
    gchar* date = mail_view_format_date(e->date);
    gchar* size = preview_format_size((gint64)e->length);
    const gchar* row[] = {num, date != NULL ? date : "", e->from != NULL ? e->from : "",
                          e->subject != NULL ? e->subject : "(no subject)", size, e->attachments ? "📎" : ""};
    preview_table_add_row(table, row, 6);
    g_free(size);
    g_free(date);
    g_free(num);
}

/* An mbox: the message list (page 0), then one message per page */
static PreviewDoc* build_mailbox(const PreviewInput* in, const gchar* data, gsize len) {
    g_autoptr(MailBox) box = mail_box_scan(data, len);
    guint n = box != NULL ? box->entries->len : 0;
    gchar* size = preview_format_size(in->size);
    PreviewDoc* doc;
    if (in->page > 0 && in->page <= n) {
        g_autoptr(MailMessage) msg = mail_box_message(box, in->page - 1, data, len, MAIL_VIEW_MAX_PART);
        gchar* subtitle = g_strdup_printf("Message %u of %u%s · %s · PgUp / PgDn", in->page, n,
                                          box->complete ? "" : "+", in->name);
        doc = mail_view_message(msg, subtitle);
        g_free(subtitle);
    } else {
        doc = preview_doc_new();
        doc->monospace = TRUE;
        doc->wrap = FALSE;
        gchar* subtitle = box != NULL && box->complete
                              ? g_strdup_printf("Mailbox · %u %s · %s", n, n == 1 ? "message" : "messages", size)
                              : g_strdup_printf("Mailbox · %s · the first %u and last %u messages shown", size,
                                                box != NULL ? box->head_count : 0, n - (box != NULL ? box->head_count : 0));
        preview_doc_title(doc, in->name, subtitle);
        g_free(subtitle);
        g_autoptr(PreviewTable) table = preview_table_new(6);
        const gchar* header[] = {"#", "Date", "From", "Subject", "Size", ""};
        preview_table_set_header(table, header);
        preview_table_set_right_aligned(table, 4);
        guint shown = 0;
        for (guint i = 0; i < n; i++) {
            /* Long mailboxes: the oldest and the newest messages */
            if (n > 2 * MAILBOX_ROWS_SHOWN && i == MAILBOX_ROWS_SHOWN) {
                const gchar* gap[] = {"…", "", "", "", "", ""};
                preview_table_add_row(table, gap, 6);
                i = n - MAILBOX_ROWS_SHOWN;
            }
            mailbox_row(table, g_ptr_array_index(box->entries, i), i + 1);
            shown++;
        }
        preview_doc_add_table(doc, table);
        if (n > 0) preview_doc_note(doc, "\nPgDn opens the messages one by one, PgUp comes back.");
    }
    doc->pages = n;
    g_free(size);
    return doc;
}

static PreviewDoc* build_mail(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    /* The raw bytes: the viewer's text is converted to UTF-8 already */
    if (in->path == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no file");
        return NULL;
    }
    GMappedFile* map = g_mapped_file_new(in->path, FALSE, error);
    if (map == NULL) return NULL;
    gsize full_len = g_mapped_file_get_length(map);
    const gchar* data = g_mapped_file_get_contents(map);
    if (data != NULL && mail_store_looks_like((const guint8*)data, full_len)) {
        /* Outlook stores can be gigabytes: their header only */
        PreviewDoc* doc = mail_store_preview((const guint8*)data, MIN(full_len, (gsize)MAIL_STORE_HEADER_BYTES), in->name,
                                             (gint64)full_len);
        g_mapped_file_unref(map);
        return doc;
    }
    if (data != NULL && mail_box_looks_like(data, full_len)) {
        PreviewDoc* doc = build_mailbox(in, data, full_len);
        g_mapped_file_unref(map);
        return doc;
    }
    if (data != NULL && full_len >= 16 && memcmp(data, "// <!-- <mdb:mork", 17) == 0) {
        /* Thunderbird index of a mailbox */
        g_mapped_file_unref(map);
        PreviewDoc* doc = preview_doc_new();
        gchar* box = g_strndup(in->name, strlen(in->name) > 4 ? strlen(in->name) - 4 : strlen(in->name));
        gchar* line = g_strdup_printf("Thunderbird index of %s", box);
        gchar* size = preview_format_size(in->size);
        preview_doc_title(doc, in->name, size);
        preview_doc_note(doc, line);
        g_free(size);
        g_free(line);
        g_free(box);
        return doc;
    }
    gsize len = MIN(full_len, (gsize)MAIL_MAX_FILE);
    gboolean tnef = mail_tnef_looks_like((const guint8*)data, len);
    g_autoptr(MailMessage) msg = tnef ? mail_tnef_read((const guint8*)data, len, MAIL_VIEW_MAX_PART, NULL)
                                      : mail_message_parse(data != NULL ? data : "", data != NULL ? len : 0, MAIL_VIEW_MAX_PART);
    g_mapped_file_unref(map);
    gchar* size = preview_format_size(in->size);
    gchar* subtitle = g_strdup_printf("%s · %s", tnef ? "Outlook TNEF (winmail.dat)" : "Mail message", size);
    PreviewDoc* doc = mail_view_message(msg, subtitle);
    g_free(subtitle);
    g_free(size);
    return doc;
}

/* --- Office 97-2003 and other compound files --- */

static PreviewDoc* build_ole(const PreviewInput* in, GCancellable* cancellable, GError** error) {
    (void)cancellable;
    return office97_preview(in->path, in->name, in->size, PREVIEW_FORMAT_MAX_TEXT, error);
}

static PreviewDoc* build_document(const PreviewInput* in, GCancellable* cancellable,
                                  GError** error) {
    (void)cancellable;
    if (!is_opendocument(in)) {
        gchar* size = preview_format_size(in->size);
        PreviewDoc* doc = docx_file_preview(in->path, in->name, size, PREVIEW_FORMAT_MAX_TEXT, error);
        g_free(size);
        return doc;
    }
    gchar* html = odf_file_to_html(in->path, PREVIEW_FORMAT_MAX_TEXT, NULL, error);
    return html != NULL ? preview_doc_new_source(PREVIEW_DOC_HTML, html) : NULL;
}

/* --- Registry --- */

static PreviewBuildFunc builder_for(FileKind kind) {
    switch (kind) {
    case FILE_KIND_EXECUTABLE:
        return build_elf;
    case FILE_KIND_DOCUMENT:
        return build_document;
    case FILE_KIND_CSV:
        return build_csv;
    case FILE_KIND_JSON:
        return build_json;
    case FILE_KIND_DIFF:
        return build_diff;
    case FILE_KIND_ARCHIVE:
        return build_archive;
    case FILE_KIND_CERTIFICATE:
        return build_certificate;
    case FILE_KIND_CALENDAR:
        return build_calendar;
    case FILE_KIND_CONTACT:
        return build_contacts;
    case FILE_KIND_DESKTOP:
        return build_desktop;
    case FILE_KIND_LOG:
        return build_log;
    case FILE_KIND_FONT:
        return build_font;
    case FILE_KIND_MANPAGE:
        return build_manpage;
    case FILE_KIND_NOTEBOOK:
        return build_notebook;
    case FILE_KIND_SPREADSHEET:
        return build_spreadsheet;
    case FILE_KIND_PRESENTATION:
        return build_presentation;
    case FILE_KIND_EBOOK:
        return build_ebook;
    case FILE_KIND_RTF:
        return build_rtf;
    case FILE_KIND_OLE:
        return build_ole;
    case FILE_KIND_MAIL:
        return build_mail;
    case FILE_KIND_DATABASE:
        return build_database;
    case FILE_KIND_TRANSLATION:
        return build_translation;
    default:
        return NULL;
    }
}

gboolean preview_format_has_builder(FileKind kind) {
    return builder_for(kind) != NULL;
}

PreviewDoc* preview_format_build(const PreviewInput* input, GCancellable* cancellable,
                                 GError** error) {
    PreviewBuildFunc build = builder_for(input->kind);
    if (build == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no preview for this format yet");
        return NULL;
    }
    return build(input, cancellable, error);
}

PreviewDoc* preview_format_build_file(const gchar* path, const gchar* name, gsize text_limit,
                                      GCancellable* cancellable, FileKind* kind, GError** error) {
    GFile* file = g_file_new_for_path(path);
    GFileInputStream* stream = g_file_read(file, cancellable, error);
    g_object_unref(file);
    if (stream == NULL) {
        return NULL;
    }
    GStatBuf st;
    gint64 size = g_stat(path, &st) == 0 ? (gint64)st.st_size : -1;
    gsize cap = MAX(text_limit, (gsize)FILE_SNIFF_BYTES);
    gchar* data = g_malloc(cap + 1);
    gsize len = 0;
    gboolean ok = g_input_stream_read_all(G_INPUT_STREAM(stream), data, FILE_SNIFF_BYTES, &len,
                                          cancellable, error);
    PreviewInput in = { .path = path, .name = name, .size = size };
    gboolean is_text = FALSE;
    if (ok) {
        in.kind = file_kind_detect(name, (const guchar*)data, len);
        in.head = (const guchar*)data;
        in.head_len = len;
        if (kind != NULL) {
            *kind = in.kind;
        }
        is_text = file_content_is_text((const guchar*)data, len);
        if (is_text && len == FILE_SNIFF_BYTES && cap > len) {
            gsize more = 0;
            ok = g_input_stream_read_all(G_INPUT_STREAM(stream), data + len, cap - len, &more,
                                         cancellable, error);
            len += more;
        }
    }
    g_object_unref(stream);
    if (!ok) {
        g_free(data);
        return NULL;
    }
    data[len] = '\0';
    gchar* text = NULL;
    if (is_text) {
        text = text_encoding_to_utf8(data, len, NULL);
        in.text = text;
        in.text_len = strlen(text);
        in.text_truncated = size > (gint64)len;
    }
    PreviewDoc* doc = preview_format_build(&in, cancellable, error);
    g_free(text);
    g_free(data);
    return doc;
}

/* --- Hex dump --- */

PreviewDoc* preview_format_hex_dump(const gchar* name, FileKind kind, gint64 size,
                                    const guchar* data, gsize len, const gchar* reason) {
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE;
    doc->wrap = FALSE;

    gchar* size_str = preview_format_size(size);
    const gchar* label = file_kind_label(kind);
    GString* header = g_string_new(NULL);
    if (kind == FILE_KIND_BINARY || label == NULL) {
        g_string_append(header, "[ Binary file ]");
    } else {
        g_string_append_printf(header, "[ %s file ]", label);
    }
    g_string_append_printf(header, " %s (%s)\n", name, size_str);
    if (reason != NULL) {
        g_string_append_printf(header, "Cannot show the %s: %s.\n",
                               kind == FILE_KIND_BINARY ? "file" : "content", reason);
    }
    if (size >= 0 && (gint64)len < size) {
        gchar* shown = g_format_size((guint64)len);
        g_string_append_printf(header, "Showing the first %s.\n", shown);
        g_free(shown);
    }
    g_string_append_c(header, '\n');
    preview_doc_append(doc, PREVIEW_STYLE_DIM, header->str);
    g_string_free(header, TRUE);
    g_free(size_str);

    if (len == 0) {
        return doc;
    }
    gchar* dump = hex_dump_format(data, len, 0);
    /* Every dump line has the same layout (see hex_dump.h) */
    for (const gchar* line = dump; *line != '\0';) {
        const gchar* nl = strchr(line, '\n');
        gsize n = nl != NULL ? (gsize)(nl - line) : strlen(line);
        if (n >= HEX_DUMP_LINE_WIDTH) {
            gint start = preview_doc_offset(doc);
            preview_doc_append_len(doc, line, (gssize)n);
            preview_doc_add_span(doc, start, start + HEX_DUMP_OFFSET_WIDTH, PREVIEW_STYLE_DIM);
            preview_doc_add_span(doc, start + HEX_DUMP_ASCII_COLUMN, start + HEX_DUMP_LINE_WIDTH,
                                 PREVIEW_STYLE_STRING);
        } else {
            preview_doc_append_len(doc, line, (gssize)n);
        }
        preview_doc_append_len(doc, "\n", 1);
        line += n + (nl != NULL);
    }
    g_free(dump);
    return doc;
}
