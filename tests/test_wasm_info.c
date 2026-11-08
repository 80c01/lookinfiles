/* Unit tests for src/formats/wasm_info.c (WebAssembly preview). */
#include "formats/wasm_info.h"

#include <string.h>

/* Helper to append LEB128 unsigned */
static void append_leb128(GByteArray* b, guint32 val) {
    do {
        guint8 byte = val & 0x7F;
        val >>= 7;
        if (val != 0) byte |= 0x80;
        g_byte_array_append(b, &byte, 1);
    } while (val != 0);
}

static void append_wasm_string(GByteArray* b, const gchar* s) {
    guint32 len = (guint32)strlen(s);
    append_leb128(b, len);
    g_byte_array_append(b, (const guint8*)s, len);
}

static GByteArray* build_synthetic_wasm(void) {
    GByteArray* b = g_byte_array_new();
    /* Header: \0asm, version 1 */
    guint8 hdr[] = { 0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00 };
    g_byte_array_append(b, hdr, sizeof(hdr));

    /* Section 1: Type section (1 func type: (i32, i32) -> i32) */
    GByteArray* sec1 = g_byte_array_new();
    append_leb128(sec1, 1); /* 1 type */
    guint8 func_type[] = { 0x60, 0x02, 0x7F, 0x7F, 0x01, 0x7F };
    g_byte_array_append(sec1, func_type, sizeof(func_type));

    guint8 sec1_id = 1;
    g_byte_array_append(b, &sec1_id, 1);
    append_leb128(b, sec1->len);
    g_byte_array_append(b, sec1->data, sec1->len);
    g_byte_array_free(sec1, TRUE);

    /* Section 2: Import section (1 import: env.print (func 0)) */
    GByteArray* sec2 = g_byte_array_new();
    append_leb128(sec2, 1); /* 1 import */
    append_wasm_string(sec2, "env");
    append_wasm_string(sec2, "print");
    guint8 imp_desc[] = { 0x00, 0x00 }; /* func, type 0 */
    g_byte_array_append(sec2, imp_desc, sizeof(imp_desc));

    guint8 sec2_id = 2;
    g_byte_array_append(b, &sec2_id, 1);
    append_leb128(b, sec2->len);
    g_byte_array_append(b, sec2->data, sec2->len);
    g_byte_array_free(sec2, TRUE);

    /* Section 3: Function section (1 defined function of type 0) */
    GByteArray* sec3 = g_byte_array_new();
    append_leb128(sec3, 1); /* 1 func */
    append_leb128(sec3, 0); /* type 0 */

    guint8 sec3_id = 3;
    g_byte_array_append(b, &sec3_id, 1);
    append_leb128(b, sec3->len);
    g_byte_array_append(b, sec3->data, sec3->len);
    g_byte_array_free(sec3, TRUE);

    /* Section 5: Memory section (1 memory: initial 2 pages, max 10 pages) */
    GByteArray* sec5 = g_byte_array_new();
    append_leb128(sec5, 1); /* 1 mem */
    guint8 mem_limits[] = { 0x01, 0x02, 0x0A }; /* flags=1, min=2, max=10 */
    g_byte_array_append(sec5, mem_limits, sizeof(mem_limits));

    guint8 sec5_id = 5;
    g_byte_array_append(b, &sec5_id, 1);
    append_leb128(b, sec5->len);
    g_byte_array_append(b, sec5->data, sec5->len);
    g_byte_array_free(sec5, TRUE);

    /* Section 7: Export section (2 exports: memory, add) */
    GByteArray* sec7 = g_byte_array_new();
    append_leb128(sec7, 2); /* 2 exports */
    append_wasm_string(sec7, "memory");
    guint8 exp_mem[] = { 0x02, 0x00 }; /* mem #0 */
    g_byte_array_append(sec7, exp_mem, sizeof(exp_mem));

    append_wasm_string(sec7, "add");
    guint8 exp_func[] = { 0x00, 0x01 }; /* func #1 */
    g_byte_array_append(sec7, exp_func, sizeof(exp_func));

    guint8 sec7_id = 7;
    g_byte_array_append(b, &sec7_id, 1);
    append_leb128(b, sec7->len);
    g_byte_array_append(b, sec7->data, sec7->len);
    g_byte_array_free(sec7, TRUE);

    return b;
}

static void test_wasm_valid(void) {
    GByteArray* b = build_synthetic_wasm();
    GError* error = NULL;
    WasmInfo* info = wasm_info_parse(b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);

    g_assert_cmpint(info->version, ==, 1);
    g_assert_cmpint(info->n_types, ==, 1);
    g_assert_cmpint(info->n_imported_funcs, ==, 1);
    g_assert_cmpint(info->n_defined_funcs, ==, 1);
    g_assert_cmpint(info->n_mems, ==, 1);
    g_assert_cmpint(info->mem_initial_pages, ==, 2);
    g_assert_cmpint(info->mem_max_pages, ==, 10);
    g_assert_true(info->has_mem_max);

    g_assert_cmpint(info->imports->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(info->imports, 0), ==, "env.print (func)");

    g_assert_cmpint(info->exports->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(info->exports, 0), ==, "memory (memory)");
    g_assert_cmpstr(g_ptr_array_index(info->exports, 1), ==, "add (func #1)");

    wasm_info_free(info);
    g_byte_array_free(b, TRUE);
}

static void test_wasm_doc_append(void) {
    GByteArray* b = build_synthetic_wasm();
    PreviewDoc* doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = wasm_info_append(doc, b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    const gchar* text = doc->text->str;
    g_assert_true(strstr(text, "Version 1") != NULL);
    g_assert_true(strstr(text, "1 defined, 1 imported") != NULL);
    g_assert_true(strstr(text, "2 pages (128 KB)") != NULL);
    g_assert_true(strstr(text, "env.print") != NULL);
    g_assert_true(strstr(text, "add (func #1)") != NULL);

    preview_doc_free(doc);
    g_byte_array_free(b, TRUE);
}

static void test_wasm_invalid(void) {
    GError* error = NULL;
    g_assert_null(wasm_info_parse(NULL, 0, &error));
    g_clear_error(&error);

    guint8 bad_magic[] = { 'W', 'A', 'S', 'M', 1, 0, 0, 0 };
    g_assert_null(wasm_info_parse(bad_magic, sizeof(bad_magic), &error));
    g_clear_error(&error);

    guint8 truncated[] = { 0, 'a', 's', 'm' };
    g_assert_null(wasm_info_parse(truncated, sizeof(truncated), &error));
    g_clear_error(&error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/wasm_info/valid", test_wasm_valid);
    g_test_add_func("/wasm_info/doc_append", test_wasm_doc_append);
    g_test_add_func("/wasm_info/invalid", test_wasm_invalid);
    return g_test_run();
}
