/* Unit tests for src/pcap_info.c (PCAP / PCAPNG packet capture preview). */
#include "pcap_info.h"

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

static GByteArray* build_synthetic_pcap(void) {
    GByteArray* b = g_byte_array_new();

    /* Global Header (24 bytes) */
    append_be32(b, 0xA1B2C3D4); /* magic (BE) */
    append_be16(b, 2);          /* version major */
    append_be16(b, 4);          /* version minor */
    append_be32(b, 0);          /* thiszone */
    append_be32(b, 0);          /* sigfigs */
    append_be32(b, 65535);      /* snaplen */
    append_be32(b, 1);          /* network = Ethernet */

    /* Packet 1: TCP SYN from 192.168.1.10:12345 to 1.1.1.1:443 (54 bytes) */
    append_be32(b, 1700000000); /* ts_sec */
    append_be32(b, 1000);       /* ts_usec */
    append_be32(b, 54);         /* cap_len */
    append_be32(b, 54);         /* orig_len */

    guint8 pkt1[54] = {
        /* Ethernet: dst, src, EtherType 0x0800 */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB,
        0x08, 0x00,
        /* IPv4: ver/ihl, tos, len, id, frag, ttl, proto(6), chk, src, dst */
        0x45, 0x00, 0x00, 0x28, 0x12, 0x34, 0x40, 0x00, 64, 6, 0x00, 0x00,
        192, 168, 1, 10,
        1, 1, 1, 1,
        /* TCP: sport 12345, dport 443, seq 1, ack 0, offset/flags (SYN 0x02), win */
        0x30, 0x39, 0x01, 0xBB, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x50, 0x02, 0xFA, 0xF0, 0x00, 0x00, 0x00, 0x00
    };
    g_byte_array_append(b, pkt1, sizeof(pkt1));

    /* Packet 2: UDP DNS from 192.168.1.10:54321 to 8.8.8.8:53 (42 bytes) */
    append_be32(b, 1700000000); /* ts_sec */
    append_be32(b, 25000);      /* ts_usec */
    append_be32(b, 42);         /* cap_len */
    append_be32(b, 42);         /* orig_len */

    guint8 pkt2[42] = {
        /* Ethernet */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB,
        0x08, 0x00,
        /* IPv4: proto 17 (UDP) */
        0x45, 0x00, 0x00, 0x1C, 0x56, 0x78, 0x40, 0x00, 64, 17, 0x00, 0x00,
        192, 168, 1, 10,
        8, 8, 8, 8,
        /* UDP: sport 54321, dport 53, len 8 */
        0xD4, 0x31, 0x00, 0x35, 0x00, 0x08, 0x00, 0x00
    };
    g_byte_array_append(b, pkt2, sizeof(pkt2));

    return b;
}

static void test_pcap_parsing(void) {
    GByteArray* b = build_synthetic_pcap();
    GError* error = NULL;
    PcapInfo* info = pcap_info_parse(b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);

    g_assert_cmpstr(info->format, ==, "libpcap (microsecond)");
    g_assert_cmpstr(info->link_name, ==, "Ethernet");
    g_assert_cmpint(info->linktype, ==, 1);
    g_assert_cmpint(info->snaplen, ==, 65535);
    g_assert_cmpint(info->total_packets, ==, 2);
    g_assert_cmpint(info->total_cap_bytes, ==, 96);
    g_assert_cmpint(info->total_orig_bytes, ==, 96);

    g_assert_cmpint(info->packets->len, ==, 2);

    const PcapPacket* p1 = &g_array_index(info->packets, PcapPacket, 0);
    g_assert_cmpint(p1->num, ==, 1);
    g_assert_cmpstr(p1->protocol, ==, "TCP");
    g_assert_cmpstr(p1->src, ==, "192.168.1.10:12345");
    g_assert_cmpstr(p1->dst, ==, "1.1.1.1:443");
    g_assert_true(strstr(p1->summary, "SYN") != NULL);

    const PcapPacket* p2 = &g_array_index(info->packets, PcapPacket, 1);
    g_assert_cmpint(p2->num, ==, 2);
    g_assert_cmpstr(p2->protocol, ==, "DNS");
    g_assert_cmpstr(p2->src, ==, "192.168.1.10:54321");
    g_assert_cmpstr(p2->dst, ==, "8.8.8.8:53");

    pcap_info_free(info);
    g_byte_array_free(b, TRUE);
}

static void test_pcap_doc_append(void) {
    GByteArray* b = build_synthetic_pcap();
    PreviewDoc* doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = pcap_info_append(doc, b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    const gchar* text = doc->text->str;
    g_assert_true(strstr(text, "libpcap") != NULL);
    g_assert_true(strstr(text, "Ethernet") != NULL);
    g_assert_true(strstr(text, "2 packets") != NULL);
    g_assert_true(strstr(text, "192.168.1.10:12345") != NULL);
    g_assert_true(strstr(text, "1.1.1.1:443") != NULL);
    g_assert_true(strstr(text, "DNS") != NULL);

    preview_doc_free(doc);
    g_byte_array_free(b, TRUE);
}

static void test_pcapng_parsing(void) {
    GByteArray* b = g_byte_array_new();

    /* Section Header Block (28 bytes) */
    append_be32(b, 0x0A0D0D0A); /* block type */
    append_be32(b, 28);         /* block len */
    append_be32(b, 0x1A2B3C4D); /* byte order magic */
    append_be16(b, 1);          /* major 1 */
    append_be16(b, 0);          /* minor 0 */
    append_be32(b, 0xFFFFFFFF); /* section len high */
    append_be32(b, 0xFFFFFFFF); /* section len low */
    append_be32(b, 28);         /* block len */

    /* Interface Description Block (20 bytes) */
    append_be32(b, 1);          /* block type = IDB */
    append_be32(b, 20);         /* block len */
    append_be16(b, 1);          /* linktype = Ethernet */
    append_be16(b, 0);          /* reserved */
    append_be32(b, 65535);      /* snaplen */
    append_be32(b, 20);         /* block len */

    GError* error = NULL;
    PcapInfo* info = pcap_info_parse(b->data, b->len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);

    g_assert_cmpstr(info->format, ==, "pcapng");
    g_assert_cmpstr(info->link_name, ==, "Ethernet");
    g_assert_cmpint(info->linktype, ==, 1);

    pcap_info_free(info);
    g_byte_array_free(b, TRUE);
}

static void test_pcap_invalid(void) {
    GError* error = NULL;
    g_assert_null(pcap_info_parse(NULL, 0, &error));
    g_clear_error(&error);

    guint8 bad[] = { 0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    g_assert_null(pcap_info_parse(bad, sizeof(bad), &error));
    g_clear_error(&error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/pcap_info/parsing", test_pcap_parsing);
    g_test_add_func("/pcap_info/doc_append", test_pcap_doc_append);
    g_test_add_func("/pcap_info/pcapng", test_pcapng_parsing);
    g_test_add_func("/pcap_info/invalid", test_pcap_invalid);
    return g_test_run();
}
