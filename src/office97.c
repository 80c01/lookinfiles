/* Previews of OLE2 compound files (see office97.h) */
#include "office97.h"
#include "i18n.h"
#include "doc_reader.h"
#include "mail_msg.h"
#include "mail_view.h"
#include "ole_props.h"
#include "ppt_reader.h"
#include "slide_render.h"
#include "spreadsheet.h"
#include "xls_reader.h"
#include <gio/gio.h>
#include <string.h>

#define MAX_STREAMS_LISTED 200
#define SHEETS_SHOWN 3
#define SHEET_ROWS 300
#define OFFICE97_MAX_DRAWN 20  /* slides drawn as pictures, like pptx */

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
        case OFFICE97_WORD: return _("Word 97-2003 document");
        case OFFICE97_EXCEL: return _("Excel 97-2003 workbook");
        case OFFICE97_EXCEL5: return _("Excel 5.0/95 workbook");
        case OFFICE97_POWERPOINT: return _("PowerPoint 97-2003 presentation");
        case OFFICE97_OUTLOOK_MSG: return _("Outlook message");
        case OFFICE97_UNKNOWN: break;
    }
    return _("Compound file");
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
    preview_doc_section(doc, _("Streams"));
    guint n = ole_reader_get_n_entries(ole);
    g_autoptr(PreviewTable) table = preview_table_new(2);
    const gchar* header[] = {_("Name"), _("Size")};
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
        gchar* note = g_strdup_printf(_("The first %1$u of %2$u entries are listed."), MAX_STREAMS_LISTED, n);
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
        return office97_summary(ole, name, size, _("Encrypted: the content needs the password."));
    }
    Office97Format format = office97_format(ole);
    if (format == OFFICE97_OUTLOOK_MSG) {
        GError* err = NULL;
        g_autoptr(MailMessage) msg = mail_msg_read(ole, MAIL_VIEW_MAX_PART, &err);
        if (msg == NULL) {
            gchar* reason = g_strdup_printf(_("The message cannot be read: %s."), err->message);
            g_clear_error(&err);
            PreviewDoc* doc = office97_summary(ole, name, size, reason);
            g_free(reason);
            return doc;
        }
        gchar* size_text = size >= 0 ? g_format_size((guint64)size) : g_strdup("");
        gchar* subtitle = g_strdup_printf(_("Outlook message · %s"), size_text);
        PreviewDoc* doc = mail_view_message(msg, subtitle);
        g_free(subtitle);
        g_free(size_text);
        return doc;
    }
    if (format == OFFICE97_WORD) {
        g_autoptr(OleProperties) props = ole_properties_read(ole);
        gchar* subtitle = ole_properties_subtitle(props, office97_format_label(format));
        gchar* size_text = size >= 0 ? g_format_size((guint64)size) : NULL;
        if (size_text != NULL) {
            gchar* with_size = g_strdup_printf("%s · %s", subtitle, size_text);
            g_free(subtitle);
            subtitle = with_size;
        }
        g_free(size_text);
        gboolean encrypted = FALSE;
        GError* err = NULL;
        PreviewDoc* text = doc_reader_preview(ole, props != NULL && props->title != NULL ? props->title : name,
                                              subtitle, max_text, &encrypted, &err);
        g_free(subtitle);
        if (text != NULL) return text;
        gchar* reason = encrypted ? g_strdup(_("Encrypted: the content needs the password."))
                                  : g_strdup_printf(_("The text cannot be read: %s."), err->message);
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
            gchar* reason = encrypted ? g_strdup(_("Encrypted: the content needs the password."))
                                      : g_strdup_printf(_("The cells cannot be read: %s."), err->message);
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
        gchar* subtitle = g_strdup_printf(ngettext("%1$s · %2$u sheet · %3$s", "%1$s · %2$u sheets · %3$s", book->sheets->len),
                                          book->format, book->sheets->len, size_text);
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
            preview_doc_field(doc, _("Sheets"), names->str);
            g_string_free(names, TRUE);
        }
        spreadsheet_append_sheets(doc, book, SHEETS_SHOWN, SHEET_ROWS);
        return doc;
    }
    if (format == OFFICE97_POWERPOINT) {
        gboolean encrypted = FALSE;
        GError* err = NULL;
        g_autoptr(SlideDeck) deck = ppt_reader_read(ole, &encrypted, &err);
        if (deck == NULL) {
            gchar* reason = encrypted ? g_strdup(_("Encrypted: the content needs the password."))
                                      : g_strdup_printf(_("The slides cannot be read: %s."), err->message);
            g_clear_error(&err);
            PreviewDoc* doc = office97_summary(ole, name, size, reason);
            g_free(reason);
            return doc;
        }
        /* Title and subtitle with the slide count found */
        g_autoptr(OleProperties) props = ole_properties_read(ole);
        if (props == NULL) props = ole_properties_new();
        props->slides = (gint)deck->slides->len;
        gchar* subtitle = ole_properties_subtitle(props, office97_format_label(format));
        if (size >= 0) {
            gchar* size_text = g_format_size((guint64)size);
            gchar* with_size = g_strdup_printf("%s · %s", subtitle, size_text);
            g_free(subtitle);
            g_free(size_text);
            subtitle = with_size;
        }
        PreviewDoc* doc = slide_deck_preview(deck, props->title != NULL ? props->title : name, subtitle,
                                             OFFICE97_MAX_DRAWN);
        g_free(subtitle);
        return doc;
    }
    return office97_summary(ole, name, size, NULL);
}
