/* Structured preview builders, one per FileKind (see preview_formats.h). */
#include "preview_formats.h"

#include <glib/gstdio.h>
#include <string.h>

#include "docx.h"
#include "elf_info.h"
#include "hex_dump.h"

typedef PreviewDoc* (*PreviewBuildFunc)(const PreviewInput* in, GCancellable* cancellable,
                                        GError** error);

gchar* preview_format_size(gint64 size) {
    return size < 0 ? g_strdup("") : g_format_size((guint64)size);
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
