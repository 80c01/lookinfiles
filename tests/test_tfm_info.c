/* Unit tests for src/formats/tfm_info.c (TeX font metric preview). */
#include "formats/tfm_info.h"

#include <string.h>

static void append_be16(GByteArray* b, guint16 val) {
    guint8 bytes[2] = { (guint8)((val >> 8) & 0xFF), (guint8)(val & 0xFF) };
    g_byte_array_append(b, bytes, 2);
}

static void append_be32(GByteArray* b, guint32 val) {
    guint8 bytes[4] = {
        (guint8)((val >> 24) & 0xFF),
        (guint8)((val >> 16) & 0xFF),
        (guint8)((val >> 8) & 0xFF),
        (guint8)(val & 0xFF)
    };
    g_byte_array_append(b, bytes, 4);
}

static GByteArray* build_synthetic_tfm(void) {
    GByteArray* b = g_byte_array_new();

    guint16 lh = 18;
    guint16 bc = 65;
    guint16 ec = 66;
    guint16 nw = 2;
    guint16 nh = 1;
    guint16 nd = 1;
    guint16 ni = 0;
    guint16 nl = 0;
    guint16 nk = 0;
    guint16 ne = 0;
    guint16 np = 7;
    guint16 lf = 6 + lh + (ec - bc + 1) + nw + nh + nd + ni + nl + nk + ne + np;

    /* 12 header dimension words */
    append_be16(b, lf);
    append_be16(b, lh);
    append_be16(b, bc);
    append_be16(b, ec);
    append_be16(b, nw);
    append_be16(b, nh);
    append_be16(b, nd);
    append_be16(b, ni);
    append_be16(b, nl);
    append_be16(b, nk);
    append_be16(b, ne);
    append_be16(b, np);

    /* Header block (lh = 18 words = 72 bytes) */
    append_be32(b, 0x12345678);       /* checksum */
    append_be32(b, (guint32)(10 << 20)); /* design size = 10.0 pt */

    /* Word 2 (byte 32): coding scheme */
    guint8 scheme_buf[40] = { 0 };
    scheme_buf[0] = 8;
    memcpy(scheme_buf + 1, "TeX text", 8);
    g_byte_array_append(b, scheme_buf, 40); /* 10 words */

    /* Word 12 (byte 72): font family */
    guint8 family_buf[20] = { 0 };
    family_buf[0] = 3;
    memcpy(family_buf + 1, "CMR", 3);
    g_byte_array_append(b, family_buf, 20); /* 5 words */

    append_be32(b, 0); /* word 17 (7-bit safe flag) */

    /* Char info table: 2 words */
    append_be32(b, 0);
    append_be32(b, 0);

    /* Width table: 2 words */
    append_be32(b, 0);
    append_be32(b, (guint32)(1 << 19)); /* 0.5 */

    /* Height table: 1 word */
    append_be32(b, (guint32)(1 << 19));

    /* Depth table: 1 word */
    append_be32(b, 0);

    /* Font parameters: 7 words */
    append_be32(b, 0);                  /* slant = 0 */
    append_be32(b, (guint32)(1 << 19)); /* space = 0.5 */
    append_be32(b, (guint32)(1 << 18)); /* stretch = 0.25 */
    append_be32(b, (guint32)(1 << 17)); /* shrink = 0.125 */
    append_be32(b, (guint32)(450000));  /* x_height */
    append_be32(b, (guint32)(1 << 20)); /* quad = 1.0 */
    append_be32(b, (guint32)(1 << 18)); /* extra_space = 0.25 */

    return b;
}

static void test_tfm_parsing(void) {
    GByteArray* b = build_synthetic_tfm();
    GError* error = NULL;
    TfmInfo* info = tfm_info_parse(b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);

    g_assert_cmpstr(info->format, ==, "TeX Font Metric (.tfm)");
    g_assert_cmpstr(info->coding_scheme, ==, "TeX text");
    g_assert_cmpstr(info->font_family, ==, "CMR");
    g_assert_cmphex(info->checksum, ==, 0x12345678);
    g_assert_cmpfloat_with_epsilon(info->design_size, 10.0, 0.001);
    g_assert_cmpint(info->bc, ==, 65);
    g_assert_cmpint(info->ec, ==, 66);
    g_assert_cmpint(info->n_chars, ==, 2);

    g_assert_true(info->has_params);
    g_assert_cmpfloat_with_epsilon(info->space, 0.5, 0.001);
    g_assert_cmpfloat_with_epsilon(info->quad, 1.0, 0.001);

    tfm_info_free(info);
    g_byte_array_free(b, TRUE);
}

static void test_tfm_doc_append(void) {
    GByteArray* b = build_synthetic_tfm();
    PreviewDoc* doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = tfm_info_append(doc, b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    const gchar* text = doc->text->str;
    g_assert_true(strstr(text, "TeX Font Metric") != NULL);
    g_assert_true(strstr(text, "CMR") != NULL);
    g_assert_true(strstr(text, "TeX text") != NULL);
    g_assert_true(strstr(text, "10.00 pt") != NULL);
    g_assert_true(strstr(text, "12345678") != NULL);
    g_assert_true(strstr(text, "2 glyphs") != NULL);
    g_assert_true(strstr(text, "Normal space") != NULL);
    g_assert_true(strstr(text, "Quad width") != NULL);

    preview_doc_free(doc);
    g_byte_array_free(b, TRUE);
}

static void test_vf_parsing(void) {
    GByteArray* b = g_byte_array_new();
    guint8 pre[] = { 0xF7, 202, 7 };
    g_byte_array_append(b, pre, 3);
    g_byte_array_append(b, (const guint8*)"test_vf", 7);
    append_be32(b, 0xAABBCCDD);
    append_be32(b, (guint32)(12 << 20)); /* 12.0 pt */

    GError* error = NULL;
    TfmInfo* info = tfm_info_parse(b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);

    g_assert_cmpstr(info->format, ==, "TeX Virtual Font (.vf)");
    g_assert_cmpstr(info->comment, ==, "test_vf");
    g_assert_cmphex(info->checksum, ==, 0xAABBCCDD);
    g_assert_cmpfloat_with_epsilon(info->design_size, 12.0, 0.001);

    tfm_info_free(info);
    g_byte_array_free(b, TRUE);
}

static void test_pfb_parsing(void) {
    const gchar ps_text[] =
        "%!FontType1-1.0\n"
        "/FontName /MyPostScriptFont def\n"
        "/FamilyName (MyFontFamily) def\n"
        "/FullName (My Font Full Name) def\n";
    guint32 tlen = (guint32)strlen(ps_text);

    GByteArray* b = g_byte_array_new();
    guint8 hdr[6] = {
        0x80, 0x01,
        (guint8)(tlen & 0xFF),
        (guint8)((tlen >> 8) & 0xFF),
        (guint8)((tlen >> 16) & 0xFF),
        (guint8)((tlen >> 24) & 0xFF)
    };
    g_byte_array_append(b, hdr, 6);
    g_byte_array_append(b, (const guint8*)ps_text, tlen);

    GError* error = NULL;
    TfmInfo* info = tfm_info_parse(b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);

    g_assert_cmpstr(info->format, ==, "PostScript Type 1 (.pfb)");
    g_assert_cmpstr(info->font_family, ==, "MyFontFamily");
    g_assert_cmpstr(info->full_name, ==, "My Font Full Name");

    tfm_info_free(info);
    g_byte_array_free(b, TRUE);
}

static void test_tfm_invalid(void) {
    GError* error = NULL;
    g_assert_null(tfm_info_parse(NULL, 0, &error));
    g_clear_error(&error);

    guint8 tiny[] = { 0, 1, 2 };
    g_assert_null(tfm_info_parse(tiny, sizeof(tiny), &error));
    g_clear_error(&error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/tfm_info/parsing", test_tfm_parsing);
    g_test_add_func("/tfm_info/doc_append", test_tfm_doc_append);
    g_test_add_func("/tfm_info/vf_parsing", test_vf_parsing);
    g_test_add_func("/tfm_info/pfb_parsing", test_pfb_parsing);
    g_test_add_func("/tfm_info/invalid", test_tfm_invalid);
    return g_test_run();
}
