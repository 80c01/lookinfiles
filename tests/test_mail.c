/* Unit tests for src/mail_message.c and src/mail_view.c (and the mail
 * previews of src/preview_formats.c): headers, MIME, charsets and the
 * fixtures of tests/fixtures/mail. */
#include "file_types.h"
#include "mail_box.h"
#include "mail_dir.h"
#include "mail_message.h"
#include "mail_msg.h"
#include "mail_store.h"
#include "mail_tnef.h"
#include "mail_view.h"
#include "preview_formats.h"

#include <glib/gstdio.h>
#include <string.h>

#include "msg_builder.h"
#include "tnef_builder.h"

static MailMessage* parse(const gchar* text) {
    return mail_message_parse(text, strlen(text), 1 << 20);
}

static void test_encoded_words(void) {
    gchar* s = mail_decode_header("=?UTF-8?B?QW5uIE3DvGxsZXI=?= <ann@example.org>");
    g_assert_cmpstr(s, ==, "Ann Müller <ann@example.org>");
    g_free(s);
    /* Adjacent encoded words: the space between them is dropped */
    s = mail_decode_header("=?UTF-8?Q?R=C3=A9union_de?= =?UTF-8?Q?_lundi?= ok");
    g_assert_cmpstr(s, ==, "Réunion de lundi ok");
    g_free(s);
    s = mail_decode_header("=?iso-8859-1*fr?q?Fran=E7ois?=");
    g_assert_cmpstr(s, ==, "François");
    g_free(s);
    /* Raw 8-bit (Windows-1252), folded lines, broken encoded words */
    s = mail_decode_header("Caf\xe9\r\n\tcr\xe8me");
    g_assert_cmpstr(s, ==, "Café crème");
    g_free(s);
    s = mail_decode_header("=?UTF-8?X?bad?= =?UTF-8?B?");
    g_assert_cmpstr(s, ==, "=?UTF-8?X?bad?= =?UTF-8?B?");
    g_free(s);
    g_assert_null(mail_decode_header(NULL));
}

static void test_dates(void) {
    g_assert_cmpint(mail_parse_date("Thu, 29 Oct 2026 18:02:00 +0100"), ==, 1793293320);
    g_assert_cmpint(mail_parse_date("29 Oct 2026 17:02:00 GMT"), ==, 1793293320);
    g_assert_cmpint(mail_parse_date("Thu, 29 Oct 26 12:02 EST"), ==, 1793293320);
    g_assert_cmpint(mail_parse_date("yesterday"), ==, -1);
    g_assert_cmpint(mail_parse_date(NULL), ==, -1);
}

static void test_mime(void) {
    const gchar* text =
        "From: a@b\r\nTo: c@d\r\nSubject: hi\r\nContent-Type: multipart/mixed;\r\n boundary=\"XYZ\"\r\n\r\n"
        "preamble\r\n--XYZ\r\nContent-Type: text/plain; charset=\"utf-8\"\r\nContent-Transfer-Encoding: base64\r\n\r\n"
        "SGVsbG8gw6l0w6kh\r\n--XYZ\r\nContent-Type: application/octet-stream;\r\n name*0=\"long-\";\r\n name*1=\"name.bin\"\r\n"
        "Content-Disposition: attachment\r\n\r\nraw bytes\r\n--XYZ--\r\nepilogue";
    MailMessage* m = parse(text);
    g_assert_cmpstr(m->subject, ==, "hi");
    g_assert_cmpstr(m->root->content_type, ==, "multipart/mixed");
    g_assert_cmpuint(m->root->children->len, ==, 2);
    gchar* body = mail_part_text(mail_message_body(m, FALSE));
    g_assert_cmpstr(body, ==, "Hello été!");
    g_free(body);
    GPtrArray* att = mail_message_attachments(m);
    g_assert_cmpuint(att->len, ==, 1);
    const MailPart* p = g_ptr_array_index(att, 0);
    g_assert_cmpstr(p->filename, ==, "long-name.bin");
    g_assert_cmpuint(p->size, ==, 9);
    g_ptr_array_unref(att);
    g_assert_cmpstr(mail_message_header(m, "to"), ==, "c@d");
    mail_message_free(m);
    /* No headers at all: a text body */
    m = parse("just text\n");
    g_assert_null(m->subject);
    body = mail_part_text(mail_message_body(m, FALSE));
    g_assert_cmpstr(body, ==, "just text\n");
    g_free(body);
    mail_message_free(m);
    /* emlx: byte count line first */
    m = parse("26\nSubject: apple\n\nbody text\nplist garbage");
    g_assert_cmpstr(m->subject, ==, "apple");
    body = mail_part_text(mail_message_body(m, FALSE));
    g_assert_cmpstr(body, ==, "body text\n");
    g_free(body);
    mail_message_free(m);
    /* Truncated multipart: the last part runs to the end */
    m = parse("Content-Type: multipart/mixed; boundary=b\n\n--b\nContent-Type: text/plain\n\ncut here");
    body = mail_part_text(mail_message_body(m, FALSE));
    g_assert_cmpstr(body, ==, "cut here");
    g_free(body);
    mail_message_free(m);
    /* Parts larger than the limit are listed, not decoded */
    MailMessage* big = mail_message_parse("Content-Type: image/png\n\n0123456789", 35, 4);
    g_assert_null(big->root->data);
    g_assert_cmpuint(big->root->size, ==, 10);
    mail_message_free(big);
}

static void test_charsets(void) {
    gchar* s = mail_to_utf8("\x80 \x93x\x94", 5, "iso-8859-1");
    g_assert_cmpstr(s, ==, "€ “x”");
    g_free(s);
    s = mail_to_utf8("caf\xc3\xa9", 5, "UTF8");
    g_assert_cmpstr(s, ==, "café");
    g_free(s);
    /* Declared UTF-8 but not: Windows-1252 */
    s = mail_to_utf8("caf\xe9", 4, "utf-8");
    g_assert_cmpstr(s, ==, "café");
    g_free(s);
    s = mail_to_utf8("x", 1, "no-such-charset");
    g_assert_cmpstr(s, ==, "x");
    g_free(s);
}

static PreviewDoc* fixture(const gchar* name) {
    gchar* path = g_build_filename("tests", "fixtures", "mail", name, NULL);
    GError* err = NULL;
    FileKind kind = FILE_KIND_TEXT;
    PreviewDoc* doc = preview_format_build_file(path, name, 1 << 20, NULL, &kind, &err);
    g_assert_no_error(err);
    g_assert_cmpint(kind, ==, FILE_KIND_MAIL);
    g_free(path);
    return doc;
}

static void test_fixtures(void) {
    PreviewDoc* doc = fixture("alternative.eml");
    const gchar* t = doc->text->str;
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_TEXT);
    g_assert_true(g_str_has_prefix(t, "Réunion de lundi — agenda\n"));
    g_assert_nonnull(strstr(t, "Ann Müller <ann@example.org>"));
    g_assert_nonnull(strstr(t, "Bonjour à tous,"));
    g_assert_null(strstr(t, "<html>"));
    preview_doc_free(doc);

    doc = fixture("latin1.eml");
    g_assert_true(g_str_has_prefix(doc->text->str, "Café ?\n"));
    g_assert_nonnull(strstr(doc->text->str, "Le café est prêt, ça vous dit"));
    g_assert_nonnull(strstr(doc->text->str, "qui continue sur la suivante."));
    preview_doc_free(doc);

    doc = fixture("windows-1252.eml");
    g_assert_nonnull(strstr(doc->text->str, "Total: 12 € “paid”"));
    preview_doc_free(doc);

    doc = fixture("html-inline.eml");
    t = doc->text->str;
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_HTML);
    g_assert_nonnull(strstr(t, "<title>Weekly news</title>"));
    g_assert_nonnull(strstr(t, "<img src=\"data:image/png;base64,iVBORw0KGgo"));
    g_assert_nonnull(strstr(t, "https://tracker.example.com/pixel.gif")); /* listed, never fetched */
    g_assert_nonnull(strstr(t, "<li>résumé.pdf · application/pdf · 320 bytes</li>"));
    g_assert_null(strstr(t, "cid:"));
    preview_doc_free(doc);

    doc = fixture("forwarded.eml");
    t = doc->text->str;
    g_assert_nonnull(strstr(t, "Forwarded message"));
    g_assert_nonnull(strstr(t, "Invoice 42"));
    g_assert_nonnull(strstr(t, "Amount due: 30 €."));
    preview_doc_free(doc);

    doc = fixture("encrypted.eml");
    g_assert_nonnull(strstr(doc->text->str, "Encrypted message"));
    g_assert_nonnull(strstr(doc->text->str, "Secret plans"));
    g_assert_null(strstr(doc->text->str, "PGP MESSAGE"));
    preview_doc_free(doc);
}

static void test_detection(void) {
    const gchar* msg = "Received: from x\nFrom: a@b\nSubject: s\n\nbody";
    g_assert_cmpint(file_kind_detect("noext", (const guchar*)msg, strlen(msg)), ==, FILE_KIND_MAIL);
    g_assert_cmpint(file_kind_detect("1234.emlx", (const guchar*)"12\nFrom: a\nTo: b\n\n", 18), ==, FILE_KIND_MAIL);
    g_assert_cmpint(file_kind_from_name("x.eml"), ==, FILE_KIND_MAIL);
    /* Not mail: one header-like line, a YAML document, a git patch */
    const gchar* yaml = "name: x\nversion: 2\n";
    g_assert_cmpint(file_kind_detect("conf", (const guchar*)yaml, strlen(yaml)), !=, FILE_KIND_MAIL);
    const gchar* patch = "From 1234567 Mon Sep 17 00:00:00 2001\nFrom: a\nSubject: [PATCH] x\n\n";
    g_assert_cmpint(file_kind_detect("0001.patch", (const guchar*)patch, strlen(patch)), ==, FILE_KIND_DIFF);
}

/* --- mbox --- */

static PreviewDoc* mailbox_page(const gchar* name, guint page) {
    gchar* path = g_build_filename("tests", "fixtures", "mail", name, NULL);
    GStatBuf st;
    g_assert_cmpint(g_stat(path, &st), ==, 0);
    PreviewInput in = {.path = path, .name = name, .size = st.st_size, .kind = FILE_KIND_MAIL, .page = page};
    GError* err = NULL;
    PreviewDoc* doc = preview_format_build(&in, NULL, &err);
    g_assert_no_error(err);
    g_free(path);
    return doc;
}

static void test_mbox(void) {
    gchar* data = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents("tests/fixtures/mail/Inbox", &data, &len, NULL));
    g_assert_true(mail_box_looks_like(data, len));
    g_assert_cmpint(file_kind_detect("Inbox", (const guchar*)data, len), ==, FILE_KIND_MAIL);
    MailBox* box = mail_box_scan(data, len);
    g_assert_true(box->complete);
    g_assert_cmpuint(box->entries->len, ==, 3);
    MailBoxEntry* e = g_ptr_array_index(box->entries, 1);
    g_assert_cmpstr(e->subject, ==, "Re: Lunch? ✓");
    g_assert_true(e->attachments);
    /* >From unquoted in the message */
    MailMessage* m = mail_box_message(box, 0, data, len, 1 << 20);
    gchar* body = mail_part_text(mail_message_body(m, FALSE));
    g_assert_nonnull(strstr(body, "\nFrom the office, at noon."));
    g_free(body);
    mail_message_free(m);
    mail_box_free(box);
    g_free(data);

    /* mboxcl: an unquoted From line inside a Content-Length body */
    g_assert_true(g_file_get_contents("tests/fixtures/mail/mboxcl.mbox", &data, &len, NULL));
    box = mail_box_scan(data, len);
    g_assert_cmpuint(box->entries->len, ==, 2);
    e = g_ptr_array_index(box->entries, 0);
    g_assert_cmpint(e->date, ==, mail_parse_date("2 Nov 2026 08:00:00 +0000")); /* from the From line */
    m = mail_box_message(box, 0, data, len, 1 << 20);
    body = mail_part_text(mail_message_body(m, FALSE));
    g_assert_nonnull(strstr(body, "From here on, an unquoted line"));
    g_free(body);
    mail_message_free(m);
    mail_box_free(box);
    g_free(data);

    /* Truncated in a message: what is there is listed */
    g_assert_true(g_file_get_contents("tests/fixtures/mail/truncated.mbox", &data, &len, NULL));
    box = mail_box_scan(data, len);
    g_assert_cmpuint(box->entries->len, ==, 2);
    mail_box_free(box);
    g_free(data);
    g_assert_null(mail_box_scan("not a mailbox", 13));
}

static void test_mbox_large(void) {
    /* Larger than MAIL_BOX_FULL_SCAN: the first and the last messages */
    GString* s = g_string_new(NULL);
    guint i = 0;
    while (s->len < MAIL_BOX_FULL_SCAN + 1024 * 1024) {
        g_string_append_printf(s, "From a@b Mon Nov  2 08:00:00 2026\nSubject: message %u\n\n", i++);
        for (gint k = 0; k < 40; k++) g_string_append(s, "padding padding padding padding padding\n");
        g_string_append_c(s, '\n');
    }
    MailBox* box = mail_box_scan(s->str, s->len);
    g_assert_false(box->complete);
    g_assert_cmpuint(box->head_count, >, 100);
    MailBoxEntry* first = g_ptr_array_index(box->entries, 0);
    MailBoxEntry* last = g_ptr_array_index(box->entries, box->entries->len - 1);
    g_assert_cmpstr(first->subject, ==, "message 0");
    gchar* want = g_strdup_printf("message %u", i - 1);
    g_assert_cmpstr(last->subject, ==, want);
    g_free(want);
    for (guint k = 1; k < box->entries->len; k++) {
        MailBoxEntry* a = g_ptr_array_index(box->entries, k - 1);
        MailBoxEntry* b = g_ptr_array_index(box->entries, k);
        g_assert_cmpuint(a->offset + a->length, <=, b->offset);
    }
    mail_box_free(box);
    g_string_free(s, TRUE);
}

static void test_mbox_pages(void) {
    PreviewDoc* doc = mailbox_page("Inbox", 0);
    g_assert_cmpuint(doc->pages, ==, 3);
    g_assert_nonnull(strstr(doc->text->str, "Mailbox · 3 messages"));
    g_assert_nonnull(strstr(doc->text->str, "Re: Lunch? ✓"));
    preview_doc_free(doc);
    doc = mailbox_page("Inbox", 2);
    g_assert_cmpuint(doc->pages, ==, 3);
    g_assert_nonnull(strstr(doc->text->str, "Message 2 of 3"));
    g_assert_nonnull(strstr(doc->text->str, "Yes! Menu attached."));
    g_assert_nonnull(strstr(doc->text->str, "menu.txt · text/plain"));
    preview_doc_free(doc);
    /* Past the end: the list */
    doc = mailbox_page("Inbox", 9);
    g_assert_nonnull(strstr(doc->text->str, "Mailbox · 3 messages"));
    preview_doc_free(doc);
}

/* --- Maildir and MH --- */

static void test_mail_dirs(void) {
    g_assert_cmpint(mail_dir_kind("tests/fixtures/mail/Maildir"), ==, MAIL_DIR_MAILDIR);
    g_assert_cmpint(mail_dir_kind("tests/fixtures/mail/mh"), ==, MAIL_DIR_MH);
    g_assert_cmpint(mail_dir_kind("tests/fixtures"), ==, MAIL_DIR_NONE);
    g_assert_null(mail_dir_preview("tests/fixtures", "fixtures", 10, NULL));
    PreviewDoc* doc = mail_dir_preview("tests/fixtures/mail/Maildir", "Maildir", 10, NULL);
    const gchar* t = doc->text->str;
    g_assert_nonnull(strstr(t, "Maildir folder · 3 messages, 1 unread"));
    g_assert_nonnull(strstr(t, "Sent"));
    /* Oldest first, newest last */
    const gchar* a = strstr(t, "Read and replied");
    const gchar* b = strstr(t, "Flagged, seen");
    const gchar* c = strstr(t, "New one");
    g_assert_true(a != NULL && b != NULL && c != NULL && a < b && b < c);
    g_assert_nonnull(strstr(t, "Renée"));
    g_assert_nonnull(strstr(t, "read, flagged"));
    g_assert_nonnull(strstr(t, "replied"));
    g_assert_nonnull(strstr(t, "unread"));
    preview_doc_free(doc);
    /* Only the newest messages */
    doc = mail_dir_preview("tests/fixtures/mail/Maildir", "Maildir", 1, NULL);
    g_assert_nonnull(strstr(doc->text->str, "New one"));
    g_assert_null(strstr(doc->text->str, "Read and replied"));
    g_assert_nonnull(strstr(doc->text->str, "The newest 1 of 3 messages are shown."));
    preview_doc_free(doc);
    doc = mail_dir_preview("tests/fixtures/mail/mh", "mh", 10, NULL);
    t = doc->text->str;
    g_assert_nonnull(strstr(t, "MH folder · 2 messages, 1 unread"));
    g_assert_true(strstr(t, "First") < strstr(t, "Second"));
    preview_doc_free(doc);
    /* Messages in cur/ have no extension: detected by content */
    g_assert_cmpint(file_kind_of_path("tests/fixtures/mail/Maildir/new/1793200000.M3P1.host", "1793200000.M3P1.host"), ==,
                    FILE_KIND_MAIL);
}

/* --- Outlook .msg --- */

static PreviewDoc* msg_doc(GBytes* bytes) {
    GError* err = NULL;
    OleReader* ole = ole_reader_new_from_bytes(bytes, &err);
    g_assert_no_error(err);
    MailMessage* m = mail_msg_read(ole, 1 << 20, &err);
    g_assert_no_error(err);
    g_assert_nonnull(m);
    PreviewDoc* doc = mail_view_message(m, "Outlook message");
    mail_message_free(m);
    ole_reader_free(ole);
    return doc;
}

static void test_msg(void) {
    GBytes* b = msg_build(TRUE, NULL);
    GError* err = NULL;
    OleReader* ole = ole_reader_new_from_bytes(b, &err);
    g_assert_no_error(err);
    MailMessage* m = mail_msg_read(ole, 1 << 20, &err);
    g_assert_no_error(err);
    g_assert_cmpstr(m->subject, ==, "Quarterly figures – Q3");
    g_assert_cmpstr(m->from, ==, "Ann Müller <ann@example.org>");
    g_assert_cmpstr(m->to, ==, "Bob; Carol");
    g_assert_cmpstr(m->cc, ==, "Team");
    g_assert_cmpint(m->date, ==, 1793293320);
    g_assert_false(m->encrypted);
    mail_message_free(m);
    ole_reader_free(ole);

    PreviewDoc* doc = msg_doc(b);
    const gchar* t = doc->text->str;
    g_assert_nonnull(strstr(t, "The figures are attached."));
    g_assert_nonnull(strstr(t, "figures.csv"));
    g_assert_null(strstr(t, "\r"));
    preview_doc_free(doc);
    g_bytes_unref(b);

    /* No plain body: the RTF one, as HTML */
    b = msg_build(FALSE, "{\\rtf1\\ansi\\pard Hello from {\\b RTF}\\par}");
    doc = msg_doc(b);
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_HTML);
    g_assert_nonnull(strstr(doc->text->str, "Hello from"));
    preview_doc_free(doc);
    g_bytes_unref(b);

    /* Not a message: no properties */
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "Other", "x", 1);
    b = ole_builder_finish(ob);
    ole = ole_reader_new_from_bytes(b, NULL);
    g_assert_nonnull(ole);
    g_assert_null(mail_msg_read(ole, 1 << 20, &err));
    g_assert_nonnull(err);
    g_clear_error(&err);
    ole_reader_free(ole);
    g_bytes_unref(b);

    /* Encrypted S/MIME class */
    ob = ole_builder_new();
    msg_add_string(ob, "", 0x001A, "IPM.Note.SMIME");
    msg_add_string(ob, "", 0x0037, "Secret");
    b = ole_builder_finish(ob);
    ole = ole_reader_new_from_bytes(b, NULL);
    m = mail_msg_read(ole, 1 << 20, NULL);
    g_assert_true(m->encrypted);
    mail_message_free(m);
    ole_reader_free(ole);
    g_bytes_unref(b);
}

static void test_msg_rtf(void) {
    /* The compressed example of MS-OXRTFCP */
    static const guint8 lzfu[] = {0x2d, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x4c, 0x5a, 0x46, 0x75, 0xf1, 0xc5,
                                  0xc7, 0xa7, 0x03, 0x00, 0x0a, 0x00, 0x72, 0x63, 0x70, 0x67, 0x31, 0x32, 0x35, 0x42,
                                  0x32, 0x0a, 0xf3, 0x20, 0x68, 0x65, 0x6c, 0x09, 0x00, 0x20, 0x62, 0x77, 0x05, 0xb0,
                                  0x6c, 0x64, 0x7d, 0x0a, 0x80, 0x0f, 0xa0};
    GBytes* r = mail_msg_decompress_rtf(lzfu, sizeof lzfu);
    g_assert_nonnull(r);
    gsize n = 0;
    const gchar* d = g_bytes_get_data(r, &n);
    g_assert_cmpmem(d, n, "{\\rtf1\\ansi\\ansicpg1252\\pard hello world}\r\n", 43);
    g_bytes_unref(r);
    /* Truncated: what was decoded, never past the input */
    for (gsize cut = 0; cut < sizeof lzfu; cut++) {
        r = mail_msg_decompress_rtf(lzfu, cut);
        if (r != NULL) {
            g_assert_cmpuint(g_bytes_get_size(r), <=, 43);
            g_bytes_unref(r);
        }
    }
    /* Unknown method */
    guint8 bad[16] = {12, 0, 0, 0, 4, 0, 0, 0, 'X', 'X', 'X', 'X'};
    g_assert_null(mail_msg_decompress_rtf(bad, sizeof bad));
}

static void test_msg_damaged(void) {
    GBytes* b = msg_build(TRUE, NULL);
    gsize n = 0;
    const guint8* d = g_bytes_get_data(b, &n);
    /* Truncations and flipped bytes: no crash */
    for (gsize cut = 0; cut < n; cut += 97) {
        GBytes* part = g_bytes_new(d, cut);
        OleReader* ole = ole_reader_new_from_bytes(part, NULL);
        if (ole != NULL) {
            MailMessage* m = mail_msg_read(ole, 1 << 20, NULL);
            if (m != NULL) mail_message_free(m);
            ole_reader_free(ole);
        }
        g_bytes_unref(part);
    }
    guint8* copy = g_memdup2(d, n);
    for (gsize i = 512; i < n; i += 61) {
        guint8 old = copy[i];
        copy[i] ^= 0xA5;
        GBytes* part = g_bytes_new(copy, n);
        OleReader* ole = ole_reader_new_from_bytes(part, NULL);
        if (ole != NULL) {
            MailMessage* m = mail_msg_read(ole, 1 << 20, NULL);
            if (m != NULL) mail_message_free(m);
            ole_reader_free(ole);
        }
        g_bytes_unref(part);
        copy[i] = old;
    }
    g_free(copy);
    g_bytes_unref(b);

    /* The fixture, through the preview */
    gchar* path = g_build_filename("tests", "fixtures", "mail", "sample.msg", NULL);
    FileKind kind = FILE_KIND_TEXT;
    GError* err = NULL;
    PreviewDoc* doc = preview_format_build_file(path, "sample.msg", 1 << 20, NULL, &kind, &err);
    g_assert_no_error(err);
    g_assert_cmpint(kind, ==, FILE_KIND_OLE);
    g_free(path);
    g_assert_nonnull(strstr(doc->text->str, "Quarterly figures"));
    g_assert_nonnull(strstr(doc->text->str, "Outlook message"));
    preview_doc_free(doc);
}

/* --- TNEF winmail.dat --- */

static void test_tnef(void) {
    GBytes* b = tnef_build();
    gsize n = 0;
    const guint8* d = g_bytes_get_data(b, &n);
    g_assert_true(mail_tnef_looks_like(d, n));
    GError* err = NULL;
    MailMessage* m = mail_tnef_read(d, n, 1 << 20, &err);
    g_assert_no_error(err);
    g_assert_cmpstr(m->subject, ==, "Café menu");
    g_assert_cmpstr(m->from, ==, "Chef Léon <leon@example.org>");
    g_assert_cmpint(m->date, ==, 1793179800); /* 2026-10-28 09:30 */
    GPtrArray* att = mail_message_attachments(m);
    g_assert_cmpuint(att->len, ==, 2);
    const MailPart* p0 = g_ptr_array_index(att, 0);
    const MailPart* p1 = g_ptr_array_index(att, 1);
    g_assert_cmpstr(p0->filename, ==, "menu de la semaine.txt");
    g_assert_cmpstr(p0->content_type, ==, "text/plain");
    g_assert_cmpuint(p0->size, ==, 19);
    g_assert_cmpstr(p1->filename, ==, "photo.png");
    g_assert_cmpstr(p1->content_type, ==, "image/png");
    g_assert_cmpuint(p1->size, ==, 300);
    g_ptr_array_unref(att);
    PreviewDoc* doc = mail_view_message(m, "TNEF");
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_HTML);
    g_assert_nonnull(strstr(doc->text->str, "soupe à l'oignon"));
    g_assert_nonnull(strstr(doc->text->str, "menu de la semaine.txt"));
    preview_doc_free(doc);
    mail_message_free(m);

    /* Attachments over the part limit: listed with their size */
    m = mail_tnef_read(d, n, 100, NULL);
    att = mail_message_attachments(m);
    p1 = g_ptr_array_index(att, 1);
    g_assert_null(p1->data);
    g_assert_cmpuint(p1->size, ==, 300);
    g_ptr_array_unref(att);
    mail_message_free(m);

    /* Truncated anywhere, bytes flipped: what was read, no crash */
    for (gsize cut = 6; cut < n; cut += 7) {
        m = mail_tnef_read(d, cut, 1 << 20, NULL);
        g_assert_nonnull(m);
        mail_message_free(m);
    }
    guint8* copy = g_memdup2(d, n);
    for (gsize i = 6; i < n; i += 5) {
        guint8 old = copy[i];
        copy[i] ^= 0xFF;
        m = mail_tnef_read(copy, n, 1 << 20, NULL);
        g_assert_nonnull(m);
        mail_message_free(m);
        copy[i] = old;
    }
    g_free(copy);
    g_assert_null(mail_tnef_read((const guint8*)"From: x\n\n", 9, 100, &err));
    g_assert_nonnull(err);
    g_clear_error(&err);

    /* Detection: by content and by name; the fixture */
    g_assert_cmpint(file_kind_detect("attachment.bin", d, MIN(n, 64)), ==, FILE_KIND_MAIL);
    g_bytes_unref(b);
    g_assert_cmpint(file_kind_from_name("WINMAIL.DAT"), ==, FILE_KIND_MAIL);
    doc = fixture("winmail.dat");
    g_assert_nonnull(strstr(doc->text->str, "Outlook TNEF"));
    g_assert_nonnull(strstr(doc->text->str, "photo.png"));
    preview_doc_free(doc);
}

/* --- Outlook and Outlook Express stores --- */

static void put32(guint8* p, guint32 v) {
    for (gint i = 0; i < 4; i++) p[i] = (guint8)(v >> (8 * i));
}

static void test_mail_stores(void) {
    /* Fixtures: a Unicode PST, an Outlook Express folder */
    PreviewDoc* doc = fixture("archive.pst");
    const gchar* t = doc->text->str;
    g_assert_nonnull(strstr(t, "Outlook data file (PST) · Unicode"));
    g_assert_nonnull(strstr(t, "compressible (permutation)"));
    g_assert_nonnull(strstr(t, "(8192 bytes)"));
    g_assert_null(strstr(t, "truncated"));
    preview_doc_free(doc);
    doc = fixture("Inbox.dbx");
    g_assert_nonnull(strstr(doc->text->str, "Outlook Express mail folder (DBX)"));
    g_assert_nonnull(strstr(doc->text->str, "42"));
    preview_doc_free(doc);

    /* ANSI OST, truncated, no encryption */
    guint8 h[MAIL_STORE_HEADER_BYTES] = {0};
    memcpy(h, "!BDN", 4);
    memcpy(h + 8, "SO", 2);
    h[10] = 14;
    put32(h + 168, 1000000);
    doc = mail_store_preview(h, sizeof h, "old.ost", 5000);
    t = doc->text->str;
    g_assert_nonnull(strstr(t, "Outlook offline store (OST) · ANSI"));
    g_assert_nonnull(strstr(t, "Encryption"));
    g_assert_nonnull(strstr(t, "none"));
    g_assert_nonnull(strstr(t, "truncated"));
    preview_doc_free(doc);
    /* Short headers: what can be read, no crash */
    for (gsize n = 0; n < 600; n += 13) {
        doc = mail_store_preview(h, n, "x.pst", -1);
        if (n < 4) g_assert_null(doc);
        if (doc != NULL) preview_doc_free(doc);
    }
    /* A DBX count that cannot be a message count is not shown */
    guint8 d[0x100] = {0};
    memcpy(d, "\xCF\xAD\x12\xFE", 4);
    put32(d + 4, 0x6F74FDC5);
    put32(d + 0xC4, 0x7FFFFFFF);
    doc = mail_store_preview(d, sizeof d, "x.dbx", 4096);
    g_assert_null(strstr(doc->text->str, "2147483647"));
    preview_doc_free(doc);
    g_assert_null(mail_store_preview((const guint8*)"From x", 6, "x", 6));

    /* Detection by content, then by name */
    g_assert_cmpint(file_kind_detect("backup", h, 64), ==, FILE_KIND_MAIL);
    g_assert_cmpint(file_kind_detect("folder", d, 64), ==, FILE_KIND_MAIL);
    g_assert_cmpint(file_kind_from_name("Outlook.PST"), ==, FILE_KIND_MAIL);
    g_assert_cmpint(file_kind_from_name("Inbox.dbx"), ==, FILE_KIND_MAIL);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mail/encoded_words", test_encoded_words);
    g_test_add_func("/mail/dates", test_dates);
    g_test_add_func("/mail/mime", test_mime);
    g_test_add_func("/mail/charsets", test_charsets);
    g_test_add_func("/mail/fixtures", test_fixtures);
    g_test_add_func("/mail/detection", test_detection);
    g_test_add_func("/mail/mbox", test_mbox);
    g_test_add_func("/mail/mbox_large", test_mbox_large);
    g_test_add_func("/mail/mbox_pages", test_mbox_pages);
    g_test_add_func("/mail/mail_dirs", test_mail_dirs);
    g_test_add_func("/mail/msg", test_msg);
    g_test_add_func("/mail/msg_rtf", test_msg_rtf);
    g_test_add_func("/mail/msg_damaged", test_msg_damaged);
    g_test_add_func("/mail/tnef", test_tnef);
    g_test_add_func("/mail/stores", test_mail_stores);
    return g_test_run();
}
