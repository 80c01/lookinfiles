/* Previews of OLE2 compound files (see office97.h) */
#include "office97.h"
#include "doc_reader.h"
#include "ole_props.h"
#include <gio/gio.h>
#include <string.h>

#define MAX_STREAMS_LISTED 200

Office97Format office97_format(const OleReader* ole) {
    if (ole_reader_has(ole, "WordDocument")) return OFFICE97_WORD;
    if (ole_reader_has(ole, "Workbook")) return OFFICE97_EXCEL;
    if (ole_reader_has(ole, "Book")) return OFFICE97_EXCEL5;
    if (ole_reader_has(ole, "PowerPoint Document")) return OFFICE97_POWERPOINT;
    for (guint i = 0; i < ole_reader_get_n_entries(ole); i++) {
        OleEntryInfo info;
        if (ole_reader_get_info(ole, i, &info) && info.depth == 0 && g_str_has_prefix(info.name, "__substg1.0_")) {
            return OFFICE97_OUTLOOK_MSG;
        }
    }
    return OFFICE97_UNKNOWN;
}

const gchar* office97_format_label(Office97Format format) {
    switch (format) {
        case OFFICE97_WORD: return "Word 97-2003 document";
        case OFFICE97_EXCEL: return "Excel 97-2003 workbook";
        case OFFICE97_EXCEL5: return "Excel 5.0/95 workbook";
        case OFFICE97_POWERPOINT: return "PowerPoint 97-2003 presentation";
        case OFFICE97_OUTLOOK_MSG: return "Outlook message";
        case OFFICE97_UNKNOWN: break;
    }
    return "Compound file";
}

/* Stream names start with control characters ("\005SummaryInformation") */
static gchar* printable(const gchar* name) {
    GString* s = g_string_new(NULL);
    for (const gchar* p = name; *p != '\0'; p++) {
        if ((guchar)*p < 0x20) g_string_append_printf(s, "\\%03o", (guchar)*p);
        else g_string_append_c(s, *p);
    }
    return g_string_free(s, FALSE);
}

PreviewDoc* office97_summary(OleReader* ole, const gchar* name, gint64 size, const gchar* reason) {
    PreviewDoc* doc = preview_doc_new();
    Office97Format format = office97_format(ole);
    gchar* size_text = size >= 0 ? g_format_size((guint64)size) : g_strdup("");
    gchar* subtitle = g_strdup_printf("%s · %s", office97_format_label(format), size_text);
    preview_doc_title(doc, name, subtitle);
    g_free(subtitle);
    g_free(size_text);
    g_autoptr(OleProperties) props = ole_properties_read(ole);
    if (props != NULL) ole_properties_card(doc, props);
    if (reason != NULL) {
        if (props != NULL) preview_doc_append_len(doc, "\n", 1);
        preview_doc_note(doc, reason);
    }
    preview_doc_section(doc, "Streams");
    guint n = ole_reader_get_n_entries(ole);
    g_autoptr(PreviewTable) table = preview_table_new(2);
    const gchar* header[] = {"Name", "Size"};
    preview_table_set_header(table, header);
    preview_table_set_right_aligned(table, 1);
    for (guint i = 0; i < n && i < MAX_STREAMS_LISTED; i++) {
        OleEntryInfo info;
        if (!ole_reader_get_info(ole, i, &info)) continue;
        gchar* shown = printable(info.name);
        gchar* label = g_strdup_printf("%*s%s%s", info.depth * 2, "", shown, info.type == OLE_ENTRY_STORAGE ? "/" : "");
        gchar* bytes = info.type == OLE_ENTRY_STREAM ? g_format_size(info.size) : g_strdup("");
        const gchar* row[] = {label, bytes};
        preview_table_add_row(table, row, 2);
        g_free(bytes);
        g_free(label);
        g_free(shown);
    }
    preview_doc_add_table(doc, table);
    if (n > MAX_STREAMS_LISTED) {
        gchar* note = g_strdup_printf("The first %u of %u entries are listed.", MAX_STREAMS_LISTED, n);
        preview_doc_note(doc, note);
        g_free(note);
    }
    return doc;
}

PreviewDoc* office97_preview(const gchar* path, const gchar* name, gint64 size, gsize max_text, GError** error) {
    g_autoptr(OleReader) ole = ole_reader_open(path, error);
    if (ole == NULL) return NULL;
    /* Encrypted documents: only their properties can be read */
    if (ole_reader_has(ole, "EncryptedSummary") || ole_reader_has(ole, "EncryptionInfo")) {
        return office97_summary(ole, name, size, "Encrypted: the content needs the password.");
    }
    Office97Format format = office97_format(ole);
    if (format == OFFICE97_WORD) {
        g_autoptr(OleProperties) props = ole_properties_read(ole);
        gchar* head = ole_properties_html_head(props, office97_format_label(format));
        gboolean encrypted = FALSE;
        GError* err = NULL;
        gchar* html = doc_reader_to_html(ole, head, max_text, &encrypted, &err);
        g_free(head);
        if (html != NULL) return preview_doc_new_source(PREVIEW_DOC_HTML, html);
        gchar* reason = encrypted ? g_strdup("Encrypted: the content needs the password.")
                                  : g_strdup_printf("The text cannot be read: %s.", err->message);
        g_clear_error(&err);
        PreviewDoc* doc = office97_summary(ole, name, size, reason);
        g_free(reason);
        return doc;
    }
    return office97_summary(ole, name, size, NULL);
}
