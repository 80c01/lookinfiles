/* Unit tests for src/cert_view.c (X.509 decoding, PEM blocks, display). */
#include "cert_view.h"

#include <string.h>

static gchar* fixture(const gchar* name, gsize* len) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", name, NULL);
    gchar* data = NULL;
    g_assert_true(g_file_get_contents(path, &data, len, NULL));
    g_free(path);
    return data;
}

static void test_parse_der(void) {
    gsize len;
    g_autofree gchar* der = fixture("server.der", &len);
    GError* error = NULL;
    g_autoptr(X509Info) info = x509_parse((const guchar*)der, len, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(info->subject, ==, "CN=lookin.example, O=Lookin Test, C=FR");
    g_assert_cmpstr(info->subject_cn, ==, "lookin.example");
    g_assert_cmpstr(info->issuer, ==, info->subject);
    g_assert_cmpstr(info->key, ==, "EC P-256");
    g_assert_cmpstr(info->signature, ==, "ECDSA with SHA-256");
    g_assert_cmpuint(info->version, ==, 3);
    g_assert_cmpint(info->is_ca, ==, 0);
    g_assert_cmpuint(info->alt_names->len, ==, 3);
    g_assert_cmpstr(g_ptr_array_index(info->alt_names, 0), ==, "DNS:lookin.example");
    g_assert_cmpstr(g_ptr_array_index(info->alt_names, 2), ==, "IP:192.0.2.10");
    g_assert_cmpstr(info->ext_key_usage, ==, "TLS Web Server Authentication");
    g_assert_cmpint(info->not_after - info->not_before, ==, 3650 * 86400);
    g_assert_cmpuint(strlen(info->sha256), ==, 32 * 3 - 1);
    /* Truncated: not a certificate */
    g_assert_null(x509_parse((const guchar*)der, len / 2, NULL));
}

static void test_pem_and_der_display(void) {
    gsize pem_len, der_len;
    g_autofree gchar* pem = fixture("server.crt", &pem_len);
    g_autofree gchar* der = fixture("server.der", &der_len);
    g_autoptr(X509Info) info = x509_parse((const guchar*)der, der_len, NULL);

    g_autoptr(PreviewDoc) a = preview_doc_new();
    g_assert_true(cert_view_append(a, (const guchar*)pem, pem_len, info->not_before + 86400, NULL));
    g_autoptr(PreviewDoc) b = preview_doc_new();
    g_assert_true(cert_view_append(b, (const guchar*)der, der_len, info->not_before + 86400, NULL));
    g_assert_cmpstr(a->text->str, ==, b->text->str);
    g_assert_nonnull(strstr(a->text->str, "Subject        CN=lookin.example"));
    g_assert_nonnull(strstr(a->text->str, "(self-signed)"));
    g_assert_nonnull(strstr(a->text->str, "valid, expires in 3649 days"));
    g_assert_nonnull(strstr(a->text->str, "Alt names      DNS:lookin.example\n               DNS:www.lookin.example"));

    g_autoptr(PreviewDoc) expired = preview_doc_new();
    cert_view_append(expired, (const guchar*)pem, pem_len, info->not_after + 3 * 86400, NULL);
    g_assert_nonnull(strstr(expired->text->str, "expired 3 days ago"));
    g_autoptr(PreviewDoc) soon = preview_doc_new();
    cert_view_append(soon, (const guchar*)pem, pem_len, info->not_after - 10 * 86400 - 60, NULL);
    g_assert_nonnull(strstr(soon->text->str, "valid, expires in 10 days"));
}

static void test_chain_and_keys(void) {
    gsize pem_len;
    g_autofree gchar* pem = fixture("server.crt", &pem_len);
    GString* text = g_string_new(NULL);
    g_string_append(text, pem);
    g_string_append(text, pem);
    /* A private key is named, never shown (dummy body) */
    g_string_append(text, "-----BEGIN EC PRIVATE KEY-----\nAAAA\n-----END EC PRIVATE KEY-----\n");
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(cert_view_append(doc, (const guchar*)text->str, text->len, 0, NULL));
    g_assert_true(g_str_has_prefix(doc->text->str, "2 certificates\n"));
    g_assert_nonnull(strstr(doc->text->str, "Certificate 2 of 2"));
    g_assert_nonnull(strstr(doc->text->str, "EC private key"));
    g_assert_nonnull(strstr(doc->text->str, "The key itself is not shown."));
    g_assert_null(strstr(doc->text->str, "AAAA"));
    g_string_free(text, TRUE);
}

static void test_not_a_certificate(void) {
    const gchar* text = "hello\n";
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    GError* error = NULL;
    g_assert_false(cert_view_append(doc, (const guchar*)text, strlen(text), 0, &error));
    g_assert_nonnull(error);
    g_error_free(error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/cert_view/parse_der", test_parse_der);
    g_test_add_func("/cert_view/pem_and_der_display", test_pem_and_der_display);
    g_test_add_func("/cert_view/chain_and_keys", test_chain_and_keys);
    g_test_add_func("/cert_view/not_a_certificate", test_not_a_certificate);
    return g_test_run();
}
