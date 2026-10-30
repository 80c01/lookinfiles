/* Test helper: builds small TNEF streams (winmail.dat). Header-only, for
 * tests/. */
#ifndef TEST_TNEF_BUILDER_H
#define TEST_TNEF_BUILDER_H

#include <glib.h>
#include <string.h>

static inline void tb_put16(GByteArray* a, guint v) {
    guint8 b[2] = {(guint8)v, (guint8)(v >> 8)};
    g_byte_array_append(a, b, 2);
}

static inline void tb_put32(GByteArray* a, guint32 v) {
    guint8 b[4] = {(guint8)v, (guint8)(v >> 8), (guint8)(v >> 16), (guint8)(v >> 24)};
    g_byte_array_append(a, b, 4);
}

static inline void tb_pad(GByteArray* a) {
    while (a->len % 4 != 0) g_byte_array_append(a, (const guint8*)"", 1);
}

static inline GByteArray* tnef_new(void) {
    GByteArray* a = g_byte_array_new();
    tb_put32(a, 0x223E9F78);
    tb_put16(a, 0x0001);
    return a;
}

/* One attribute: level, id | type << 16, data, checksum */
static inline void tnef_attr(GByteArray* a, guint8 level, guint32 attr, const void* data, gsize len) {
    g_byte_array_append(a, &level, 1);
    tb_put32(a, attr);
    tb_put32(a, (guint32)len);
    g_byte_array_append(a, data, (guint)len);
    guint sum = 0;
    for (gsize i = 0; i < len; i++) sum += ((const guint8*)data)[i];
    tb_put16(a, sum & 0xFFFF);
}

static inline void tnef_attr_string(GByteArray* a, guint8 level, guint32 attr, const gchar* s) {
    tnef_attr(a, level, attr, s, strlen(s) + 1);
}

/* MAPI property blocks: count, then type, id, value(s) */
static inline void mp_unicode(GByteArray* p, guint id, const gchar* utf8) {
    glong n = 0;
    gunichar2* w = g_utf8_to_utf16(utf8, -1, NULL, &n, NULL);
    tb_put16(p, 0x001F);
    tb_put16(p, id);
    tb_put32(p, 1);
    tb_put32(p, (guint32)(n + 1) * 2);
    for (glong i = 0; i <= n; i++) tb_put16(p, i < n ? w[i] : 0);
    tb_pad(p);
    g_free(w);
}

static inline void mp_binary(GByteArray* p, guint id, const void* data, gsize len) {
    tb_put16(p, 0x0102);
    tb_put16(p, id);
    tb_put32(p, 1);
    tb_put32(p, (guint32)len);
    g_byte_array_append(p, data, (guint)len);
    tb_pad(p);
}

static inline void mp_long(GByteArray* p, guint id, guint32 v) {
    tb_put16(p, 0x0003);
    tb_put16(p, id);
    tb_put32(p, v);
}

/* A named property (string name), skipped by readers looking for ids */
static inline void mp_named_long(GByteArray* p, const gchar* ascii_name, guint32 v) {
    tb_put16(p, 0x0003);
    tb_put16(p, 0x8001);
    for (gint i = 0; i < 16; i++) g_byte_array_append(p, (const guint8*)"\x11", 1);
    tb_put32(p, 1);
    gsize n = strlen(ascii_name);
    tb_put32(p, (guint32)(n + 1) * 2);
    for (gsize i = 0; i <= n; i++) tb_put16(p, i < n ? (guint8)ascii_name[i] : 0);
    tb_pad(p);
    tb_put32(p, v);
}

static inline GByteArray* mp_block(guint32 count) {
    GByteArray* p = g_byte_array_new();
    tb_put32(p, count);
    return p;
}

/* A winmail.dat: subject and sender as attributes, the HTML body and the
 * sender's address as MAPI properties, two attachments */
static inline GBytes* tnef_build(void) {
    GByteArray* a = tnef_new();
    guint32 cp = 1252;
    tnef_attr(a, 1, 0x00069007, &cp, 4);                         /* attOemCodepage */
    tnef_attr_string(a, 1, 0x00078008, "IPM.Microsoft Mail.Note"); /* attMessageClass */
    tnef_attr_string(a, 1, 0x00018004, "Caf\xe9 menu");            /* attSubject */
    guint8 date[14] = {0};
    guint16 dtr[7] = {2026, 10, 28, 9, 30, 0, 3};
    for (gint i = 0; i < 7; i++) {
        date[2 * i] = (guint8)dtr[i];
        date[2 * i + 1] = (guint8)(dtr[i] >> 8);
    }
    tnef_attr(a, 1, 0x00038005, date, 14); /* attDateSent */

    GByteArray* props = mp_block(4);
    mp_named_long(props, "Flags", 7);
    mp_unicode(props, 0x0C1A, "Chef Léon");
    mp_unicode(props, 0x5D01, "leon@example.org");
    const gchar* html = "<html><body><p>Today: <b>soupe à l'oignon</b></p></body></html>";
    mp_binary(props, 0x1013, html, strlen(html));
    tnef_attr(a, 1, 0x00069003, props->data, props->len); /* attMAPIProps */
    g_byte_array_unref(props);

    /* Attachment 1: 8.3 title, long name and MIME type in its properties */
    tnef_attr(a, 2, 0x00069002, "\x01\x00\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00", 14); /* attAttachRenddata */
    tnef_attr_string(a, 2, 0x00018010, "MENU~1.TXT");
    const gchar* menu = "Soupe\nGratin\nTarte\n";
    tnef_attr(a, 2, 0x0006800F, menu, strlen(menu));
    props = mp_block(3);
    mp_long(props, 0x3705, 1);
    mp_unicode(props, 0x3707, "menu de la semaine.txt");
    mp_unicode(props, 0x370E, "text/plain");
    tnef_attr(a, 2, 0x00069005, props->data, props->len);
    g_byte_array_unref(props);

    /* Attachment 2: title and data only */
    tnef_attr(a, 2, 0x00069002, "\x01\x00\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00", 14);
    tnef_attr_string(a, 2, 0x00018010, "photo.png");
    guint8 png[300];
    memset(png, 0x42, sizeof png);
    memcpy(png, "\x89PNG\r\n\x1a\n", 8);
    tnef_attr(a, 2, 0x0006800F, png, sizeof png);
    return g_byte_array_free_to_bytes(a);
}

#endif /* TEST_TNEF_BUILDER_H */
