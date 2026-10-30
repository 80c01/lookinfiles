/* Unit tests for src/mail_message.c and src/mail_view.c (and the mail
 * previews of src/preview_formats.c): headers, MIME, charsets and the
 * fixtures of tests/fixtures/mail. */
#include "file_types.h"
#include "mail_message.h"
#include "mail_view.h"
#include "preview_formats.h"

#include <string.h>

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
    g_assert_cmpstr(file_icon("a.eml", FALSE), ==, "📧");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/mail/encoded_words", test_encoded_words);
    g_test_add_func("/mail/dates", test_dates);
    g_test_add_func("/mail/mime", test_mime);
    g_test_add_func("/mail/charsets", test_charsets);
    g_test_add_func("/mail/fixtures", test_fixtures);
    g_test_add_func("/mail/detection", test_detection);
    return g_test_run();
}
