/* Previews of OLE2 compound files (see office97.h) */
#include "office97.h"
#include "doc_reader.h"
#include "ole_props.h"
#include "ppt_reader.h"
#include "spreadsheet.h"
#include "xls_reader.h"
#include <gio/gio.h>
#include <string.h>

#define MAX_STREAMS_LISTED 200
#define SHEETS_SHOWN 3
#define SHEET_ROWS 300

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
    if (format == OFFICE97_EXCEL || format == OFFICE97_EXCEL5) {
        gboolean encrypted = FALSE;
        GError* err = NULL;
        g_autoptr(Workbook) book = xls_reader_read(ole, SHEETS_SHOWN, SHEET_ROWS, &encrypted, &err);
        if (book == NULL) {
            gchar* reason = encrypted ? g_strdup("Encrypted: the content needs the password.")
                                      : g_strdup_printf("The cells cannot be read: %s.", err->message);
            g_clear_error(&err);
            PreviewDoc* doc = office97_summary(ole, name, size, reason);
            g_free(reason);
            return doc;
        }
        /* Like .xlsx: title, properties, sheet names, then the grids */
        PreviewDoc* doc = preview_doc_new();
        doc->monospace = TRUE;
        doc->wrap = FALSE;
        gchar* size_text = size >= 0 ? g_format_size((guint64)size) : g_strdup("");
        gchar* subtitle = g_strdup_printf("%s · %u %s · %s", book->format, book->sheets->len,
                                          book->sheets->len == 1 ? "sheet" : "sheets", size_text);
        preview_doc_title(doc, name, subtitle);
        g_free(subtitle);
        g_free(size_text);
        g_autoptr(OleProperties) props = ole_properties_read(ole);
        if (props != NULL) ole_properties_card(doc, props);
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
    if (format == OFFICE97_POWERPOINT) {
        gboolean encrypted = FALSE;
        guint slides = 0;
        GError* err = NULL;
        gchar* body = ppt_reader_to_html(ole, "<head></head>", max_text, &slides, &encrypted, &err);
        if (body == NULL) {
            gchar* reason = encrypted ? g_strdup("Encrypted: the content needs the password.")
                                      : g_strdup_printf("The slides cannot be read: %s.", err->message);
            g_clear_error(&err);
            PreviewDoc* doc = office97_summary(ole, name, size, reason);
            g_free(reason);
            return doc;
        }
        /* The head, with the slide count found */
        g_autoptr(OleProperties) props = ole_properties_read(ole);
        if (props == NULL) props = ole_properties_new();
        props->slides = (gint)slides;
        gchar* head = ole_properties_html_head(props, office97_format_label(format));
        gchar* at = strstr(body, "<head></head>");
        gchar* html = g_strdup_printf("%.*s%s%s", (gint)(at - body), body, head, at + strlen("<head></head>"));
        g_free(head);
        g_free(body);
        return preview_doc_new_source(PREVIEW_DOC_HTML, html);
    }
    return office97_summary(ole, name, size, NULL);
}
