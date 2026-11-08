/* Unit tests for src/formats/pyc_info.c (Python bytecode preview). */
#include "formats/pyc_info.h"

#include <string.h>

static void append_int32(GByteArray* b, gint32 val) {
    guint8 bytes[4] = {
        (guint8)(val & 0xFF),
        (guint8)((val >> 8) & 0xFF),
        (guint8)((val >> 16) & 0xFF),
        (guint8)((val >> 24) & 0xFF)
    };
    g_byte_array_append(b, bytes, 4);
}

static void append_short_ascii(GByteArray* b, const gchar* s) {
    guint8 tag = 'z';
    guint8 len = (guint8)strlen(s);
    g_byte_array_append(b, &tag, 1);
    g_byte_array_append(b, &len, 1);
    g_byte_array_append(b, (const guint8*)s, len);
}

static GByteArray* build_synthetic_pyc(void) {
    GByteArray* b = g_byte_array_new();

    /* Header: magic 3495 (Python 3.11), \r\n, flags=0, mtime=1700000000, size=123 */
    guint8 magic_crlf[] = { 0xA7, 0x0D, '\r', '\n' };
    g_byte_array_append(b, magic_crlf, 4);
    append_int32(b, 0);          /* flags (timestamp-based) */
    append_int32(b, 1700000000); /* mtime */
    append_int32(b, 123);        /* size */

    /* Inner code object for function 'add' */
    GByteArray* inner = g_byte_array_new();
    guint8 c_tag = 'c' | 0x80;
    g_byte_array_append(inner, &c_tag, 1);
    append_int32(inner, 2); /* argcount */
    append_int32(inner, 0); /* posonly */
    append_int32(inner, 0); /* kwonly */
    append_int32(inner, 2); /* stacksize */
    append_int32(inner, 0); /* flags */

    guint8 code_str[] = { 's', 1, 0, 0, 0, 0 };
    g_byte_array_append(inner, code_str, sizeof(code_str));

    guint8 empty_tuple[] = { ')', 0 };
    g_byte_array_append(inner, empty_tuple, 2); /* consts */
    g_byte_array_append(inner, empty_tuple, 2); /* names */

    guint8 locals_tuple[] = { ')', 2 };
    g_byte_array_append(inner, locals_tuple, 2); /* localsplusnames */
    append_short_ascii(inner, "a");
    append_short_ascii(inner, "b");

    guint8 empty_str[] = { 's', 0, 0, 0, 0 };
    g_byte_array_append(inner, empty_str, 5); /* localspluskinds */

    append_short_ascii(inner, "math_app.py"); /* filename */
    append_short_ascii(inner, "add");         /* name */
    append_short_ascii(inner, "Calculator.add"); /* qualname */
    append_int32(inner, 10);                  /* firstlineno */
    g_byte_array_append(inner, empty_str, 5); /* linetable */
    g_byte_array_append(inner, empty_str, 5); /* exceptiontable */

    /* Root module code object */
    guint8 root_tag = 'c' | 0x80;
    g_byte_array_append(b, &root_tag, 1);
    append_int32(b, 0); /* argcount */
    append_int32(b, 0); /* posonly */
    append_int32(b, 0); /* kwonly */
    append_int32(b, 2); /* stacksize */
    append_int32(b, 0); /* flags */

    g_byte_array_append(b, code_str, sizeof(code_str));

    /* consts tuple containing inner code */
    guint8 consts_tuple[] = { ')', 1 };
    g_byte_array_append(b, consts_tuple, 2);
    g_byte_array_append(b, inner->data, inner->len);
    g_byte_array_free(inner, TRUE);

    /* names tuple: ('os', 'sys') */
    guint8 names_tuple[] = { ')', 2 };
    g_byte_array_append(b, names_tuple, 2);
    append_short_ascii(b, "os");
    append_short_ascii(b, "sys");

    g_byte_array_append(b, empty_tuple, 2);   /* localsplusnames */
    g_byte_array_append(b, empty_str, 5);     /* localspluskinds */
    append_short_ascii(b, "math_app.py");     /* filename */
    append_short_ascii(b, "<module>");        /* name */
    append_short_ascii(b, "<module>");        /* qualname */
    append_int32(b, 1);                       /* firstlineno */
    g_byte_array_append(b, empty_str, 5);     /* linetable */
    g_byte_array_append(b, empty_str, 5);     /* exceptiontable */

    return b;
}

static void test_pyc_synthetic(void) {
    GByteArray* b = build_synthetic_pyc();
    GError* error = NULL;
    PycInfo* info = pyc_info_parse(b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);

    g_assert_cmpint(info->magic, ==, 3495);
    g_assert_cmpstr(info->python_version, ==, "Python 3.11");
    g_assert_false(info->hash_based);
    g_assert_cmpint(info->source_mtime, ==, 1700000000);
    g_assert_cmpint(info->source_size, ==, 123);
    g_assert_cmpstr(info->filename, ==, "math_app.py");
    g_assert_cmpstr(info->main_name, ==, "<module>");
    g_assert_cmpint(info->main_firstlineno, ==, 1);

    g_assert_cmpint(info->functions->len, ==, 1);
    PycFunction* fn = (PycFunction*)g_ptr_array_index(info->functions, 0);
    g_assert_cmpstr(fn->name, ==, "add");
    g_assert_cmpstr(fn->qualname, ==, "Calculator.add");
    g_assert_cmpint(fn->firstlineno, ==, 10);
    g_assert_cmpint(fn->argcount, ==, 2);

    g_assert_cmpint(info->names->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(info->names, 0), ==, "os");
    g_assert_cmpstr(g_ptr_array_index(info->names, 1), ==, "sys");

    pyc_info_free(info);
    g_byte_array_free(b, TRUE);
}

static void test_pyc_doc_append(void) {
    GByteArray* b = build_synthetic_pyc();
    PreviewDoc* doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = pyc_info_append(doc, b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    const gchar* text = doc->text->str;
    g_assert_true(strstr(text, "Python 3.11") != NULL);
    g_assert_true(strstr(text, "math_app.py") != NULL);
    g_assert_true(strstr(text, "123 bytes") != NULL || strstr(text, "123") != NULL);
    g_assert_true(strstr(text, "Calculator.add") != NULL);
    g_assert_true(strstr(text, "os, sys") != NULL || strstr(text, "os") != NULL);

    preview_doc_free(doc);
    g_byte_array_free(b, TRUE);
}

static void test_pyc_invalid(void) {
    GError* error = NULL;
    g_assert_null(pyc_info_parse(NULL, 0, &error));
    g_clear_error(&error);

    guint8 bad_magic[] = { 'P', 'Y', 'C', '0', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    g_assert_null(pyc_info_parse(bad_magic, sizeof(bad_magic), &error));
    g_clear_error(&error);

    guint8 truncated[] = { 0xA7, 0x0D, '\r', '\n' };
    g_assert_null(pyc_info_parse(truncated, sizeof(truncated), &error));
    g_clear_error(&error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/pyc_info/synthetic", test_pyc_synthetic);
    g_test_add_func("/pyc_info/doc_append", test_pyc_doc_append);
    g_test_add_func("/pyc_info/invalid", test_pyc_invalid);
    return g_test_run();
}
