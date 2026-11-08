/* PST / OST / DBX header cards (see mail_store.h) */
#include "mail/mail_store.h"
#include "core/i18n.h"
#include <string.h>

#define DBX_MAGIC "\xCF\xAD\x12\xFE"
#define DBX_MESSAGES 0x6F74FDC5u
#define DBX_FOLDERS 0x6F74FDC6u
#define DBX_COUNT_OFFSET 0xC4

static guint16 u16(const guint8* p) {
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 u32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static guint64 u64(const guint8* p) {
    return (guint64)u32(p) | ((guint64)u32(p + 4) << 32);
}

gboolean mail_store_looks_like(const guint8* head, gsize len) {
    return head != NULL && len >= 4 && (memcmp(head, "!BDN", 4) == 0 || memcmp(head, DBX_MAGIC, 4) == 0);
}

static gchar* size_text(guint64 bytes) {
    gchar* s = g_format_size(bytes);
    gchar* count = g_strdup_printf("%" G_GUINT64_FORMAT, bytes);
    gchar* out = g_strdup_printf(ngettext("%1$s (%2$s byte)", "%1$s (%2$s bytes)", bytes > 1000000 ? 1000000 : (gulong)bytes),
                                 s, count);
    g_free(count);
    g_free(s);
    return out;
}

static void size_fields(PreviewDoc* doc, guint64 recorded, gint64 file_size) {
    if (recorded == 0) return;
    gchar* s = size_text(recorded);
    preview_doc_field(doc, _("Size in header"), s);
    g_free(s);
    if (file_size >= 0 && (guint64)file_size < recorded) {
        preview_doc_note(doc, _("\nThe file is shorter than its header says: it is truncated."));
    }
}

static PreviewDoc* pst_card(const guint8* h, gsize len, const gchar* name, gint64 file_size) {
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE; /* aligned fields */
    gboolean ost = len >= 10 && memcmp(h + 8, "SO", 2) == 0;
    guint16 ver = len >= 12 ? u16(h + 10) : 0;
    gboolean unicode = ver >= 23;
    gboolean ansi = ver == 14 || ver == 15;
    gchar* subtitle = g_strdup_printf("%s · %s", ost ? _("Outlook offline store (OST)") : _("Outlook data file (PST)"),
                                      unicode ? "Unicode" : ansi ? "ANSI" : _("unknown version"));
    preview_doc_title(doc, name, subtitle);
    g_free(subtitle);
    gchar* v = g_strdup_printf("%u%s", ver,
                               ver == 36   ? _(" (Unicode, 4 KB pages: Outlook 2013 and later)")
                               : unicode   ? _(" (Unicode: Outlook 2003 and later)")
                               : ansi      ? _(" (ANSI: Outlook 97-2002, 2 GB at most)")
                                           : "");
    preview_doc_field(doc, _("Format version"), v);
    g_free(v);
    /* The root structure and the encryption byte: their place depends on the version */
    guint64 eof = 0;
    gint crypt = -1;
    if (unicode && len > 513) {
        eof = u64(h + 184);
        crypt = h[513];
    } else if (ansi && len > 461) {
        eof = u32(h + 168);
        crypt = h[461];
    }
    size_fields(doc, eof, file_size);
    if (crypt >= 0) {
        preview_doc_field(doc, _("Encryption"), crypt == 0    ? _("none")
                                             : crypt == 1  ? _("compressible (permutation)")
                                             : crypt == 2  ? _("high (cyclic)")
                                             : crypt == 16 ? "Windows Information Protection"
                                                           : _("unknown"));
    }
    preview_doc_note(doc, _("\nFolders and messages are not read: export them from Outlook (or with readpst) to "
                            "mbox or .eml files to preview them here."));
    return doc;
}

static PreviewDoc* dbx_card(const guint8* h, gsize len, const gchar* name, gint64 file_size) {
    PreviewDoc* doc = preview_doc_new();
    doc->monospace = TRUE; /* aligned fields */
    guint32 type = len >= 8 ? u32(h + 4) : 0;
    const gchar* kind = type == DBX_MESSAGES  ? _("Outlook Express mail folder (DBX)")
                        : type == DBX_FOLDERS ? _("Outlook Express folder list (DBX)")
                                              : _("Outlook Express database (DBX)");
    preview_doc_title(doc, name, kind);
    if (type == DBX_MESSAGES && len >= DBX_COUNT_OFFSET + 4) {
        guint32 count = u32(h + DBX_COUNT_OFFSET);
        /* A message takes more than 16 bytes: a larger count is not one */
        if (file_size < 0 || (gint64)count * 16 <= file_size) {
            gchar* c = g_strdup_printf("%u", count);
            preview_doc_field(doc, _("Messages"), c);
            g_free(c);
        }
    }
    if (file_size >= 0) {
        gchar* s = size_text((guint64)file_size);
        preview_doc_field(doc, _("Size"), s);
        g_free(s);
    }
    preview_doc_note(doc, _("\nMessages are not read: export them from Outlook Express (or with undbx) to .eml "
                            "files to preview them here."));
    return doc;
}

PreviewDoc* mail_store_preview(const guint8* head, gsize len, const gchar* name, gint64 file_size) {
    if (!mail_store_looks_like(head, len)) return NULL;
    return memcmp(head, "!BDN", 4) == 0 ? pst_card(head, len, name, file_size) : dbx_card(head, len, name, file_size);
}
