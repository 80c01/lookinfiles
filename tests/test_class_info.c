/* Unit tests for src/formats/class_info.c (Java class file preview). */
#include "formats/class_info.h"

#include <string.h>

/* Minimal Java .class builder in memory */
typedef struct {
    GByteArray* bytes;
} ClassBuilder;

static ClassBuilder* cb_new(guint16 major, guint16 minor) {
    ClassBuilder* b = g_new0(ClassBuilder, 1);
    b->bytes = g_byte_array_new();
    guint8 hdr[] = {
        0xCA, 0xFE, 0xBA, 0xBE,
        (guint8)(minor >> 8), (guint8)minor,
        (guint8)(major >> 8), (guint8)major
    };
    g_byte_array_append(b->bytes, hdr, sizeof(hdr));
    return b;
}

static void cb_free(ClassBuilder* b) {
    if (b == NULL) return;
    g_byte_array_free(b->bytes, TRUE);
    g_free(b);
}

/* Builds a synthetic class:
 * public class com.example.TestClass extends java.lang.Object implements java.io.Serializable
 * Fields: public static final int MAX_COUNT
 * Methods: public void <init>()
 *          public int getCount()
 */
static GByteArray* build_synthetic_class(void) {
    ClassBuilder* b = cb_new(61, 0); /* Java 17 */

    /* Constant pool entries:
     * 1: Utf8 "com/example/TestClass"
     * 2: Class #1
     * 3: Utf8 "java/lang/Object"
     * 4: Class #3
     * 5: Utf8 "java/io/Serializable"
     * 6: Class #5
     * 7: Utf8 "MAX_COUNT"
     * 8: Utf8 "I"
     * 9: Utf8 "<init>"
     * 10: Utf8 "()V"
     * 11: Utf8 "getCount"
     * 12: Utf8 "()I"
     * 13: Utf8 "SourceFile"
     * 14: Utf8 "TestClass.java"
     * cp_count = 15
     */
    guint16 cp_count = 15;
    guint8 cp_count_bytes[] = { (guint8)(cp_count >> 8), (guint8)cp_count };
    g_byte_array_append(b->bytes, cp_count_bytes, 2);

    const char* utf8_entries[] = {
        "com/example/TestClass", /* 1 */
        NULL,                    /* 2: Class */
        "java/lang/Object",      /* 3 */
        NULL,                    /* 4: Class */
        "java/io/Serializable",  /* 5 */
        NULL,                    /* 6: Class */
        "MAX_COUNT",             /* 7 */
        "I",                     /* 8 */
        "<init>",                /* 9 */
        "()V",                   /* 10 */
        "getCount",              /* 11 */
        "()I",                   /* 12 */
        "SourceFile",            /* 13 */
        "TestClass.java"         /* 14 */
    };

    for (int i = 1; i < cp_count; i++) {
        if (i == 2) {
            guint8 class_entry[] = { 7, 0, 1 };
            g_byte_array_append(b->bytes, class_entry, sizeof(class_entry));
        } else if (i == 4) {
            guint8 class_entry[] = { 7, 0, 3 };
            g_byte_array_append(b->bytes, class_entry, sizeof(class_entry));
        } else if (i == 6) {
            guint8 class_entry[] = { 7, 0, 5 };
            g_byte_array_append(b->bytes, class_entry, sizeof(class_entry));
        } else {
            const char* s = utf8_entries[i - 1];
            guint16 slen = (guint16)strlen(s);
            guint8 tag = 1;
            g_byte_array_append(b->bytes, &tag, 1);
            guint8 len_bytes[] = { (guint8)(slen >> 8), (guint8)slen };
            g_byte_array_append(b->bytes, len_bytes, 2);
            g_byte_array_append(b->bytes, (const guint8*)s, slen);
        }
    }

    /* Access flags: public (0x0001) | super (0x0020) = 0x0021 */
    guint8 decl[] = {
        0x00, 0x21, /* flags */
        0x00, 0x02, /* this_class (#2) */
        0x00, 0x04, /* super_class (#4) */
        0x00, 0x01, /* interfaces_count = 1 */
        0x00, 0x06  /* interface #6 */
    };
    g_byte_array_append(b->bytes, decl, sizeof(decl));

    /* Fields: 1 field */
    guint8 field_data[] = {
        0x00, 0x01, /* fields_count = 1 */
        0x00, 0x19, /* public (1) | static (8) | final (16) */
        0x00, 0x07, /* name_index (#7 "MAX_COUNT") */
        0x00, 0x08, /* descriptor_index (#8 "I") */
        0x00, 0x00  /* attributes_count = 0 */
    };
    g_byte_array_append(b->bytes, field_data, sizeof(field_data));

    /* Methods: 2 methods */
    guint8 method_data[] = {
        0x00, 0x02, /* methods_count = 2 */
        /* Method 1: <init> */
        0x00, 0x01, /* public */
        0x00, 0x09, /* name_index (#9 "<init>") */
        0x00, 0x0A, /* desc_index (#10 "()V") */
        0x00, 0x00, /* attrs = 0 */
        /* Method 2: getCount */
        0x00, 0x01, /* public */
        0x00, 0x0B, /* name_index (#11 "getCount") */
        0x00, 0x0C, /* desc_index (#12 "()I") */
        0x00, 0x00  /* attrs = 0 */
    };
    g_byte_array_append(b->bytes, method_data, sizeof(method_data));

    /* Attributes: SourceFile attribute */
    guint8 attr_data[] = {
        0x00, 0x01, /* attributes_count = 1 */
        0x00, 0x0D, /* attribute_name_index (#13 "SourceFile") */
        0x00, 0x00, 0x00, 0x02, /* attribute_length = 2 */
        0x00, 0x0E  /* sourcefile_index (#14 "TestClass.java") */
    };
    g_byte_array_append(b->bytes, attr_data, sizeof(attr_data));

    GByteArray* res = b->bytes;
    g_free(b);
    return res;
}

static void test_class_valid(void) {
    GByteArray* bytes = build_synthetic_class();
    GError* error = NULL;
    ClassInfo* info = class_info_parse(bytes->data, bytes->len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);

    g_assert_cmpint(info->major, ==, 61);
    g_assert_cmpstr(info->this_class, ==, "com.example.TestClass");
    g_assert_null(info->super_class); /* java.lang.Object is omitted */
    g_assert_cmpint(info->interfaces->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(info->interfaces, 0), ==, "java.io.Serializable");
    g_assert_cmpstr(info->source_file, ==, "TestClass.java");

    g_assert_cmpint(info->fields->len, ==, 1);
    g_assert_true(strstr(g_ptr_array_index(info->fields, 0), "MAX_COUNT") != NULL);
    g_assert_true(strstr(g_ptr_array_index(info->fields, 0), "int") != NULL);

    g_assert_cmpint(info->methods->len, ==, 2);
    g_assert_true(strstr(g_ptr_array_index(info->methods, 0), "new ()") != NULL);
    g_assert_true(strstr(g_ptr_array_index(info->methods, 1), "int getCount()") != NULL);

    class_info_free(info);
    g_byte_array_free(bytes, TRUE);
}

static void test_class_doc_append(void) {
    GByteArray* bytes = build_synthetic_class();
    PreviewDoc* doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = class_info_append(doc, bytes->data, bytes->len, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    const gchar* text = doc->text->str;
    g_assert_true(strstr(text, "com.example.TestClass") != NULL);
    g_assert_true(strstr(text, "Java 17") != NULL);
    g_assert_true(strstr(text, "java.io.Serializable") != NULL);
    g_assert_true(strstr(text, "TestClass.java") != NULL);
    g_assert_true(strstr(text, "MAX_COUNT") != NULL);
    g_assert_true(strstr(text, "getCount") != NULL);

    preview_doc_free(doc);
    g_byte_array_free(bytes, TRUE);
}

static void test_class_invalid(void) {
    GError* error = NULL;
    g_assert_null(class_info_parse(NULL, 0, &error));
    g_clear_error(&error);

    guint8 bad_magic[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3D };
    g_assert_null(class_info_parse(bad_magic, sizeof(bad_magic), &error));
    g_clear_error(&error);

    guint8 truncated[] = { 0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00 };
    g_assert_null(class_info_parse(truncated, sizeof(truncated), &error));
    g_clear_error(&error);

    guint8 truncated_cp[] = { 0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00, 0x00, 0x3D, 0x00, 0x05, 0x01 };
    g_assert_null(class_info_parse(truncated_cp, sizeof(truncated_cp), &error));
    g_clear_error(&error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/class_info/valid", test_class_valid);
    g_test_add_func("/class_info/doc_append", test_class_doc_append);
    g_test_add_func("/class_info/invalid", test_class_invalid);
    return g_test_run();
}
