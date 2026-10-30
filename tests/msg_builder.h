/* Test helper: builds small Outlook .msg files (compound files with MAPI
 * property streams) with tests/ole_builder.h. Header-only, for tests/. */
#ifndef TEST_MSG_BUILDER_H
#define TEST_MSG_BUILDER_H

#include "ole_builder.h"

/* A Unicode string property stream "<prefix>__substg1.0_<id>001F" */
static inline void msg_add_string(OleBuilder* ob, const gchar* prefix, guint id, const gchar* utf8) {
    glong n = 0;
    gunichar2* w = g_utf8_to_utf16(utf8, -1, NULL, &n, NULL);
    guint8* bytes = g_malloc((gsize)n * 2 + 2);
    for (glong i = 0; i < n; i++) ob_put16(bytes + i * 2, w[i]);
    ob_put16(bytes + n * 2, 0);
    gchar* path = g_strdup_printf("%s__substg1.0_%04X001F", prefix, id);
    ole_builder_add(ob, path, bytes, (gsize)n * 2);
    g_free(path);
    g_free(bytes);
    g_free(w);
}

static inline void msg_add_binary(OleBuilder* ob, const gchar* prefix, guint id, const void* data, gsize len) {
    gchar* path = g_strdup_printf("%s__substg1.0_%04X0102", prefix, id);
    ole_builder_add(ob, path, data, len);
    g_free(path);
}

/* The fixed property table with PR_CLIENT_SUBMIT_TIME (Unix time) */
static inline void msg_add_time(OleBuilder* ob, gint64 unix_time) {
    guint8 props[32 + 16] = {0};
    guint64 ft = ((guint64)unix_time + 11644473600ULL) * 10000000ULL;
    ob_put32(props + 32, (0x0039u << 16) | 0x0040u);
    ob_put32(props + 40, (guint32)ft);
    ob_put32(props + 44, (guint32)(ft >> 32));
    ole_builder_add(ob, "__properties_version1.0", props, sizeof props);
}

/* RTF compressed with the "MELA" (stored) method */
static inline GBytes* msg_stored_rtf(const gchar* rtf) {
    gsize n = strlen(rtf);
    GByteArray* a = g_byte_array_new();
    guint8 h[16] = {0};
    ob_put32(h, (guint32)n + 12);
    ob_put32(h + 4, (guint32)n);
    ob_put32(h + 8, 0x414C454D);
    g_byte_array_append(a, h, 16);
    g_byte_array_append(a, (const guint8*)rtf, (guint)n);
    return g_byte_array_free_to_bytes(a);
}

/* A message: subject, sender, recipients, date, plain body (or none), one
 * attachment */
static inline GBytes* msg_build(gboolean plain_body, const gchar* rtf_body) {
    OleBuilder* ob = ole_builder_new();
    msg_add_string(ob, "", 0x001A, "IPM.Note");
    msg_add_string(ob, "", 0x0037, "Quarterly figures – Q3");
    msg_add_string(ob, "", 0x0C1A, "Ann Müller");
    msg_add_string(ob, "", 0x5D01, "ann@example.org");
    msg_add_string(ob, "", 0x0E04, "Bob; Carol");
    msg_add_string(ob, "", 0x0E03, "Team");
    msg_add_time(ob, 1793293320); /* 2026-10-29 17:02 UTC */
    if (plain_body) msg_add_string(ob, "", 0x1000, "Hello Bob,\r\n\r\nThe figures are attached.\r\n\r\n> old quote\r\n");
    if (rtf_body != NULL) {
        GBytes* r = msg_stored_rtf(rtf_body);
        msg_add_binary(ob, "", 0x1009, g_bytes_get_data(r, NULL), g_bytes_get_size(r));
        g_bytes_unref(r);
    }
    const gchar* att = "__attach_version1.0_#00000000/";
    msg_add_string(ob, att, 0x3707, "figures.csv");
    msg_add_string(ob, att, 0x370E, "text/csv");
    const gchar* csv = "quarter,files\nQ3,1876\n";
    msg_add_binary(ob, att, 0x3701, csv, strlen(csv));
    return ole_builder_finish(ob);
}

#endif /* TEST_MSG_BUILDER_H */
