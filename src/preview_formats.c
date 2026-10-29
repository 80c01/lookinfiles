/* Structured preview builders, one per FileKind (see preview_formats.h). */
#include "preview_formats.h"

#include <glib/gstdio.h>
#include <string.h>

#include "csv.h"
#include "docx.h"
#include "elf_info.h"
#include "hex_dump.h"
#include "json_view.h"

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

static PreviewDoc* build_document(const PreviewInput* in, GCancellable* cancellable,
                                  GError** error) {
    (void)cancellable;
    gchar* html = docx_file_to_html(in->path, PREVIEW_FORMAT_MAX_TEXT, error);
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
        text = g_utf8_make_valid(data, (gssize)len);
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
