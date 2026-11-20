/* Apache Parquet preview (see parquet_view.h). */
#include "formats/parquet_view.h"
#include "core/i18n.h"
#include "formats/parquet_meta.h"
#include "formats/parquet_pages.h"

#include <string.h>

/* Row groups listed */
#define ROW_GROUPS_SHOWN 100

typedef struct {
    ParquetReadFunc read;
    gpointer source;
    guint64 size;
    GCancellable* cancellable;
} Source;

/* --- Sources --- */

typedef struct {
    const guchar* data;
    gsize len;
} MemorySource;

static gboolean read_memory(gpointer source, guint64 offset, guchar* buf, gsize len) {
    MemorySource* m = source;
    if (offset > m->len || len > m->len - offset) return FALSE;
    memcpy(buf, m->data + offset, len);
    return TRUE;
}

typedef struct {
    GInputStream* stream;
    GCancellable* cancellable;
} FileSource;

static gboolean read_file(gpointer source, guint64 offset, guchar* buf, gsize len) {
    FileSource* f = source;
    gsize got = 0;
    if (offset > G_MAXINT64 ||
        !g_seekable_seek(G_SEEKABLE(f->stream), (goffset)offset, G_SEEK_SET, f->cancellable, NULL)) {
        return FALSE;
    }
    return g_input_stream_read_all(f->stream, buf, len, &got, f->cancellable, NULL) && got == len;
}

/* --- Parts of the preview --- */

static gchar* count_text(guint64 n) {
    return preview_doc_count(n);
}

/* The codecs used, by name ("SNAPPY", "GZIP, ZSTD") */
static gchar* codecs_text(const ParquetMeta* meta) {
    guint seen = 0;
    for (guint g = 0; g < meta->n_row_groups; g++) {
        for (guint c = 0; c < meta->row_groups[g].n_chunks; c++) {
            gint codec = meta->row_groups[g].chunks[c].codec;
            if (codec >= 0 && codec < 32) seen |= 1u << codec;
        }
    }
    GString* s = g_string_new(NULL);
    for (gint codec = 0; codec < 32; codec++) {
        if ((seen & (1u << codec)) == 0) continue;
        if (s->len > 0) g_string_append(s, ", ");
        g_string_append(s, parquet_codec_name(codec));
    }
    return g_string_free(s, FALSE);
}

static void append_summary(PreviewDoc* doc, const ParquetMeta* meta) {
    gchar* rows = count_text((guint64)MAX(meta->num_rows, 0));
    gchar* cols = count_text(meta->n_leaves);
    gchar* groups = count_text(meta->n_row_groups);
    gchar* rows_text = g_strdup_printf(ngettext("%s row", "%s rows", (gulong)MAX(meta->num_rows, 0)), rows);
    gchar* cols_text = g_strdup_printf(ngettext("%s column", "%s columns", meta->n_leaves), cols);
    gchar* groups_text = g_strdup_printf(ngettext("%s row group", "%s row groups", meta->n_row_groups), groups);
    gchar* codecs = codecs_text(meta);
    /* TRANSLATORS: "1,000 rows × 9 columns · 2 row groups · SNAPPY" */
    preview_doc_appendf(doc, PREVIEW_STYLE_DIM, *codecs != '\0' ? _("%1$s × %2$s · %3$s · %4$s\n\n")
                                                               : _("%1$s × %2$s · %3$s\n\n"),
                        rows_text, cols_text, groups_text, codecs);
    g_free(codecs);
    g_free(groups_text); g_free(cols_text); g_free(rows_text);
    g_free(groups); g_free(cols); g_free(rows);
}

/* The first rows, one column after the other; notes for the columns that
 * could not be read */
static void append_rows(PreviewDoc* doc, const ParquetMeta* meta, const Source* src) {
    guint n_cols = MIN(meta->n_leaves, PARQUET_VIEW_COLUMNS);
    guint n_rows = (guint)CLAMP(meta->num_rows, 0, PARQUET_VIEW_ROWS);
    GPtrArray** columns = g_new0(GPtrArray*, n_cols);
    GPtrArray* problems = g_ptr_array_new_with_free_func(g_free);
    for (guint c = 0; c < n_cols; c++) {
        columns[c] = g_ptr_array_new_with_free_func(g_free);
        if (g_cancellable_is_cancelled(src->cancellable)) continue;
        GError* error = NULL;
        if (!parquet_column_read(meta, c, src->read, src->source, src->size, n_rows, columns[c], &error)) {
            /* TRANSLATORS: "Column city: SNAPPY compression is not supported." */
            g_ptr_array_add(problems, g_strdup_printf(_("Column %1$s: %2$s."), meta->leaves[c].label,
                                                      error->message));
            g_error_free(error);
        }
    }
    g_autoptr(PreviewTable) table = preview_table_new(MAX(n_cols, 1));
    const gchar** cells = g_new0(const gchar*, MAX(n_cols, 1));
    for (guint c = 0; c < n_cols; c++) cells[c] = meta->leaves[c].label;
    preview_table_set_header(table, cells);
    preview_table_set_row_numbers(table, TRUE);
    for (guint r = 0; r < n_rows; r++) {
        for (guint c = 0; c < n_cols; c++) {
            const gchar* cell = r < columns[c]->len ? g_ptr_array_index(columns[c], r) : NULL;
            cells[c] = cell != NULL ? cell : "null";
        }
        preview_table_add_row(table, cells, n_cols);
    }
    g_free(cells);
    preview_doc_add_table(doc, table);
    if (n_rows < meta->num_rows) {
        gchar* shown = count_text(n_rows);
        gchar* total = count_text((guint64)meta->num_rows);
        gchar* note = g_strdup_printf(_("First %1$s of %2$s rows."), shown, total);
        preview_doc_note(doc, note);
        g_free(note); g_free(total); g_free(shown);
    }
    if (n_cols < meta->n_leaves) {
        gchar* shown = count_text(n_cols);
        gchar* total = count_text(meta->n_leaves);
        gchar* note = g_strdup_printf(_("First %1$s of %2$s columns."), shown, total);
        preview_doc_note(doc, note);
        g_free(note); g_free(total); g_free(shown);
    }
    for (guint i = 0; i < problems->len; i++) preview_doc_note(doc, g_ptr_array_index(problems, i));
    g_ptr_array_unref(problems);
    for (guint c = 0; c < n_cols; c++) g_ptr_array_unref(columns[c]);
    g_free(columns);
}

static const gchar* repetition_name(gint repetition) {
    switch (repetition) {
    case PARQUET_REQUIRED: return C_("parquet column", "required");
    case PARQUET_OPTIONAL: return C_("parquet column", "optional");
    case PARQUET_REPEATED: return C_("parquet column", "repeated");
    default: return "";
    }
}

static void append_schema(PreviewDoc* doc, const ParquetMeta* meta) {
    preview_doc_section(doc, _("Schema"));
    g_autoptr(PreviewTable) table = preview_table_new(6);
    const gchar* header[] = { _("Column"), _("Type"), _("Repetition"), _("Encodings"), _("Compression"),
                              _("Size") };
    preview_table_set_header(table, header);
    preview_table_set_right_aligned(table, 5);
    preview_table_set_max_cell_width(table, 60);
    for (guint l = 0; l < meta->n_leaves; l++) {
        const ParquetLeaf* leaf = &meta->leaves[l];
        guint encodings = 0;
        gint codec = -1;
        gint64 size = 0;
        for (guint g = 0; g < meta->n_row_groups; g++) {
            if (meta->row_groups[g].chunks == NULL) continue;
            const ParquetChunk* chunk = &meta->row_groups[g].chunks[l];
            encodings |= chunk->encodings;
            size += chunk->compressed_size;
            if (codec < 0) codec = chunk->codec;
        }
        GString* enc = g_string_new(NULL);
        for (gint e = 0; e < PARQUET_ENCODING_COUNT; e++) {
            if ((encodings & (1u << e)) == 0) continue;
            if (enc->len > 0) g_string_append(enc, ", ");
            g_string_append(enc, parquet_encoding_name(e));
        }
        gchar* type = parquet_leaf_type(meta, leaf);
        gchar* size_text = g_format_size((guint64)MAX(size, 0));
        const gchar* row[] = { leaf->label, type, repetition_name(meta->nodes[leaf->node].repetition), enc->str,
                               codec >= 0 ? parquet_codec_name(codec) : "", size_text };
        preview_table_add_row(table, row, 6);
        g_free(size_text);
        g_free(type);
        g_string_free(enc, TRUE);
    }
    preview_doc_add_table(doc, table);
}

static void append_row_groups(PreviewDoc* doc, const ParquetMeta* meta) {
    if (meta->n_row_groups == 0) return;
    preview_doc_section(doc, _("Row groups"));
    g_autoptr(PreviewTable) table = preview_table_new(4);
    const gchar* header[] = { "#", _("Rows"), _("Size"), _("Compressed") };
    preview_table_set_header(table, header);
    for (guint i = 1; i < 4; i++) preview_table_set_right_aligned(table, i);
    guint shown = MIN(meta->n_row_groups, ROW_GROUPS_SHOWN);
    for (guint g = 0; g < shown; g++) {
        const ParquetRowGroup* rg = &meta->row_groups[g];
        gchar* index = g_strdup_printf("%u", g + 1);
        gchar* rows = count_text((guint64)MAX(rg->num_rows, 0));
        gchar* size = g_format_size((guint64)MAX(rg->total_byte_size, 0));
        gchar* compressed = g_format_size((guint64)MAX(rg->compressed_size, 0));
        const gchar* row[] = { index, rows, size, compressed };
        preview_table_add_row(table, row, 4);
        g_free(compressed); g_free(size); g_free(rows); g_free(index);
    }
    preview_doc_add_table(doc, table);
    if (shown < meta->n_row_groups) {
        gchar* total = count_text(meta->n_row_groups);
        gchar* note = g_strdup_printf(_("First %1$u of %2$s row groups."), shown, total);
        preview_doc_note(doc, note);
        g_free(note);
        g_free(total);
    }
}

static void append_file_details(PreviewDoc* doc, const ParquetMeta* meta, gsize footer_len) {
    preview_doc_section(doc, _("File"));
    gchar* version = g_strdup_printf("%d", meta->version);
    preview_doc_field(doc, _("Format version"), version);
    g_free(version);
    preview_doc_field(doc, _("Created by"), meta->created_by);
    gchar* footer = g_format_size(footer_len);
    preview_doc_field(doc, _("Footer"), footer);
    g_free(footer);
    if (meta->n_keys == 0) return;
    preview_doc_section(doc, _("Metadata"));
    g_autoptr(PreviewTable) table = preview_table_new(2);
    const gchar* header[] = { _("Key"), _("Value") };
    preview_table_set_header(table, header);
    preview_table_set_max_cell_width(table, 100);
    for (guint i = 0; i < meta->n_keys; i++) {
        const gchar* value = meta->values[i] != NULL ? meta->values[i] : "";
        gchar* text;
        if (strcmp(meta->keys[i], "ARROW:schema") == 0) {
            /* A base64 FlatBuffer: its size says enough */
            gchar* size = g_format_size(strlen(value));
            text = g_strdup_printf(_("Arrow schema (%s)"), size);
            g_free(size);
        } else {
            text = g_strndup(value, 1024);
            for (gchar* p = text; *p != '\0'; p++) {
                if ((guchar)*p < 0x20) *p = ' ';
            }
        }
        const gchar* row[] = { meta->keys[i], text };
        preview_table_add_row(table, row, 2);
        g_free(text);
    }
    preview_doc_add_table(doc, table);
}

static gboolean append(PreviewDoc* doc, const Source* src, GError** error) {
    guchar head[4], tail[8];
    if (src->size < 12 || !src->read(src->source, 0, head, 4) || !src->read(src->source, src->size - 8, tail, 8) ||
        (memcmp(head, PARQUET_MAGIC, 4) != 0 && memcmp(head, PARQUET_MAGIC_ENCRYPTED, 4) != 0)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not a Parquet file"));
        return FALSE;
    }
    if (memcmp(tail + 4, PARQUET_MAGIC_ENCRYPTED, 4) == 0) {
        preview_doc_field(doc, _("Encryption"), _("Encrypted footer: the schema and the data cannot be read."));
        return TRUE;
    }
    guint32 footer_len = (guint32)tail[0] | ((guint32)tail[1] << 8) | ((guint32)tail[2] << 16) |
                         ((guint32)tail[3] << 24);
    if (memcmp(tail + 4, PARQUET_MAGIC, 4) != 0 || footer_len == 0 || footer_len > src->size - 12 ||
        footer_len > PARQUET_VIEW_FOOTER_MAX) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("invalid Parquet footer"));
        return FALSE;
    }
    guchar* footer = g_malloc(footer_len);
    if (!src->read(src->source, src->size - 8 - footer_len, footer, footer_len)) {
        g_free(footer);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("invalid Parquet footer"));
        return FALSE;
    }
    g_autoptr(ParquetMeta) meta = parquet_meta_parse(footer, footer_len, error);
    g_free(footer);
    if (meta == NULL) return FALSE;
    append_summary(doc, meta);
    append_rows(doc, meta, src);
    append_schema(doc, meta);
    append_row_groups(doc, meta);
    append_file_details(doc, meta, footer_len);
    return TRUE;
}

gboolean parquet_view_append_bytes(PreviewDoc* doc, const guchar* data, gsize len, GError** error) {
    MemorySource m = { data, len };
    Source src = { read_memory, &m, data != NULL ? len : 0, NULL };
    return append(doc, &src, error);
}

gboolean parquet_view_append_file(PreviewDoc* doc, const gchar* path, GCancellable* cancellable,
                                  GError** error) {
    GFile* file = g_file_new_for_path(path);
    GFileInputStream* stream = g_file_read(file, cancellable, error);
    GFileInfo* info = stream != NULL ? g_file_input_stream_query_info(stream, G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                                                       cancellable, error)
                                     : NULL;
    g_object_unref(file);
    gboolean ok = FALSE;
    if (info != NULL) {
        FileSource f = { G_INPUT_STREAM(stream), cancellable };
        Source src = { read_file, &f, (guint64)MAX(g_file_info_get_size(info), 0), cancellable };
        ok = append(doc, &src, error);
        g_object_unref(info);
    }
    if (stream != NULL) g_object_unref(stream);
    return ok;
}
