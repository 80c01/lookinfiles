/* PCAP / PCAPNG network packet capture parser and preview builder. */
#include "pcap_info.h"

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PACKETS_SAVED 100
#define PCAP_MAGIC_USEC_BE 0xA1B2C3D4
#define PCAP_MAGIC_USEC_LE 0xD4C3B2A1
#define PCAP_MAGIC_NSEC_BE 0xA1B23C4D
#define PCAP_MAGIC_NSEC_LE 0x4D3CB2A1
#define PCAPNG_BLOCK_SHB   0x0A0D0D0A
#define PCAPNG_BYTE_ORDER  0x1A2B3C4D

static guint16 r16(const guchar* p, gboolean le) {
    return le ? (guint16)(p[0] | (p[1] << 8)) : (guint16)((p[0] << 8) | p[1]);
}

static guint32 r32(const guchar* p, gboolean le) {
    return le ? ((guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24))
              : (((guint32)p[0] << 24) | ((guint32)p[1] << 16) | ((guint32)p[2] << 8) | (guint32)p[3]);
}

static const gchar* linktype_name(guint32 lt) {
    switch (lt) {
    case 0:   return "Null/Loopback";
    case 1:   return "Ethernet";
    case 9:   return "PPP";
    case 12:  return "Raw IP";
    case 101: return "Raw IP";
    case 105: return "IEEE 802.11 (WiFi)";
    case 113: return "Linux Cooked (SLL)";
    default:  return "Unknown Link Layer";
    }
}

static void decode_packet(const guchar* pkt, guint cap_len, guint32 linktype,
                          gchar** out_src, gchar** out_dst, const gchar** out_proto, gchar** out_info) {
    *out_src = NULL;
    *out_dst = NULL;
    *out_proto = "Raw";
    *out_info = NULL;

    const guchar* payload = pkt;
    guint plen = cap_len;
    guint16 ethertype = 0;

    if (linktype == 1 && cap_len >= 14) { /* Ethernet */
        ethertype = (guint16)((pkt[12] << 8) | pkt[13]);
        payload = pkt + 14;
        plen = cap_len - 14;
        if (ethertype == 0x8100 && plen >= 4) { /* 802.1Q VLAN */
            ethertype = (guint16)((payload[2] << 8) | payload[3]);
            payload += 4;
            plen -= 4;
        }
    } else if (linktype == 113 && cap_len >= 16) { /* Linux SLL */
        ethertype = (guint16)((pkt[14] << 8) | pkt[15]);
        payload = pkt + 16;
        plen = cap_len - 16;
    } else if (linktype == 12 || linktype == 101) { /* Raw IP */
        if (cap_len >= 20 && (pkt[0] >> 4) == 4) ethertype = 0x0800;
        else if (cap_len >= 40 && (pkt[0] >> 4) == 6) ethertype = 0x86DD;
    }

    if (ethertype == 0x0800 && plen >= 20) { /* IPv4 */
        guint ihl = (payload[0] & 0x0F) * 4;
        guint8 ip_proto = payload[9];
        gchar* src_ip = g_strdup_printf("%u.%u.%u.%u", payload[12], payload[13], payload[14], payload[15]);
        gchar* dst_ip = g_strdup_printf("%u.%u.%u.%u", payload[16], payload[17], payload[18], payload[19]);

        if (plen >= ihl && ihl >= 20) {
            const guchar* l4 = payload + ihl;
            guint l4_len = plen - ihl;

            if (ip_proto == 6 && l4_len >= 14) { /* TCP */
                guint16 sport = (guint16)((l4[0] << 8) | l4[1]);
                guint16 dport = (guint16)((l4[2] << 8) | l4[3]);
                guint8 flags = l4[13];
                *out_src = g_strdup_printf("%s:%u", src_ip, sport);
                *out_dst = g_strdup_printf("%s:%u", dst_ip, dport);
                *out_proto = "TCP";

                GString* fs = g_string_new("[");
                if (flags & 0x02) g_string_append(fs, "SYN, ");
                if (flags & 0x10) g_string_append(fs, "ACK, ");
                if (flags & 0x01) g_string_append(fs, "FIN, ");
                if (flags & 0x04) g_string_append(fs, "RST, ");
                if (flags & 0x08) g_string_append(fs, "PSH, ");
                if (fs->len > 1) g_string_truncate(fs, fs->len - 2);
                g_string_append(fs, "]");
                *out_info = g_string_free(fs, FALSE);
            } else if (ip_proto == 17 && l4_len >= 8) { /* UDP */
                guint16 sport = (guint16)((l4[0] << 8) | l4[1]);
                guint16 dport = (guint16)((l4[2] << 8) | l4[3]);
                guint16 ulen = (guint16)((l4[4] << 8) | l4[5]);
                *out_src = g_strdup_printf("%s:%u", src_ip, sport);
                *out_dst = g_strdup_printf("%s:%u", dst_ip, dport);
                if (sport == 53 || dport == 53 || sport == 5353 || dport == 5353) {
                    *out_proto = "DNS";
                } else if (sport == 67 || dport == 67 || sport == 68 || dport == 68) {
                    *out_proto = "DHCP";
                } else if (sport == 123 || dport == 123) {
                    *out_proto = "NTP";
                } else {
                    *out_proto = "UDP";
                }
                *out_info = g_strdup_printf("Length=%u", ulen);
            } else if (ip_proto == 1 && l4_len >= 2) { /* ICMP */
                *out_src = g_strdup(src_ip);
                *out_dst = g_strdup(dst_ip);
                *out_proto = "ICMP";
                guint8 type = l4[0];
                if (type == 8) *out_info = g_strdup("Echo (ping) request");
                else if (type == 0) *out_info = g_strdup("Echo (ping) reply");
                else if (type == 3) *out_info = g_strdup("Destination unreachable");
                else *out_info = g_strdup_printf("Type %u", type);
            }
        }
        if (*out_src == NULL) {
            *out_src = g_strdup(src_ip);
            *out_dst = g_strdup(dst_ip);
            *out_proto = "IPv4";
        }
        g_free(src_ip);
        g_free(dst_ip);
    } else if (ethertype == 0x86DD && plen >= 40) { /* IPv6 */
        *out_proto = "IPv6";
        *out_src = g_strdup_printf("%02x%02x:%02x%02x::%02x%02x",
                                   payload[8], payload[9], payload[10], payload[11], payload[22], payload[23]);
        *out_dst = g_strdup_printf("%02x%02x:%02x%02x::%02x%02x",
                                   payload[24], payload[25], payload[26], payload[27], payload[38], payload[39]);
        guint8 next_hdr = payload[6];
        if (next_hdr == 6) *out_proto = "TCP6";
        else if (next_hdr == 17) *out_proto = "UDP6";
        else if (next_hdr == 58) *out_proto = "ICMPv6";
    } else if (ethertype == 0x0806 && plen >= 28) { /* ARP */
        *out_proto = "ARP";
        guint16 op = (guint16)((payload[6] << 8) | payload[7]);
        *out_src = g_strdup_printf("%u.%u.%u.%u", payload[14], payload[15], payload[16], payload[17]);
        *out_dst = g_strdup_printf("%u.%u.%u.%u", payload[24], payload[25], payload[26], payload[27]);
        if (op == 1) *out_info = g_strdup_printf("Who has %s? Tell %s", *out_dst, *out_src);
        else if (op == 2) *out_info = g_strdup_printf("%s is at sender MAC", *out_src);
    }

    if (*out_src == NULL && cap_len >= 12 && linktype == 1) {
        *out_src = g_strdup_printf("%02X:%02X:%02X:%02X:%02X:%02X",
                                   pkt[6], pkt[7], pkt[8], pkt[9], pkt[10], pkt[11]);
        *out_dst = g_strdup_printf("%02X:%02X:%02X:%02X:%02X:%02X",
                                   pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5]);
        *out_proto = "Ethernet";
    }
}

void pcap_info_free(PcapInfo* info) {
    if (info == NULL) return;
    if (info->packets != NULL) {
        for (guint i = 0; i < info->packets->len; i++) {
            PcapPacket* p = &g_array_index(info->packets, PcapPacket, i);
            g_free(p->src);
            g_free(p->dst);
            g_free(p->summary);
        }
        g_array_free(info->packets, TRUE);
    }
    g_free(info);
}

static PcapInfo* parse_classic_pcap(const guchar* data, gsize len, GError** error) {
    if (len < 24) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "file too small for PCAP header");
        return NULL;
    }

    guint32 raw_magic = ((guint32)data[0] << 24) | ((guint32)data[1] << 16) | ((guint32)data[2] << 8) | data[3];
    gboolean is_le = FALSE;
    gboolean is_nsec = FALSE;

    if (raw_magic == PCAP_MAGIC_USEC_BE) {
        is_le = FALSE; is_nsec = FALSE;
    } else if (raw_magic == 0xD4C3B2A1 || ((guint32)data[0] == 0xD4 && (guint32)data[1] == 0xC3)) {
        is_le = TRUE; is_nsec = FALSE;
    } else if (raw_magic == PCAP_MAGIC_NSEC_BE) {
        is_le = FALSE; is_nsec = TRUE;
    } else if (raw_magic == 0x4D3CB2A1 || ((guint32)data[0] == 0x4D && (guint32)data[1] == 0x3C)) {
        is_le = TRUE; is_nsec = TRUE;
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "unrecognized PCAP magic number");
        return NULL;
    }

    PcapInfo* info = g_new0(PcapInfo, 1);
    info->format = is_nsec ? "libpcap (nanosecond)" : "libpcap (microsecond)";
    info->major_version = r16(data + 4, is_le);
    info->minor_version = r16(data + 6, is_le);
    info->snaplen = r32(data + 16, is_le);
    info->linktype = r32(data + 20, is_le);
    info->link_name = linktype_name(info->linktype);
    info->packets = g_array_new(FALSE, TRUE, sizeof(PcapPacket));

    gsize pos = 24;
    gdouble first_ts = -1.0;

    while (pos + 16 <= len) {
        guint32 ts_sec = r32(data + pos, is_le);
        guint32 ts_usec = r32(data + pos + 4, is_le);
        guint32 cap_len = r32(data + pos + 8, is_le);
        guint32 orig_len = r32(data + pos + 12, is_le);
        pos += 16;

        if (pos + cap_len > len) break;

        info->total_packets++;
        info->total_cap_bytes += cap_len;
        info->total_orig_bytes += orig_len;

        gdouble cur_ts = (gdouble)ts_sec + (is_nsec ? (gdouble)ts_usec / 1e9 : (gdouble)ts_usec / 1e6);
        if (first_ts < 0.0) {
            first_ts = cur_ts;
            info->start_time = (gint64)ts_sec;
        }
        info->end_time = (gint64)ts_sec;

        if (info->packets->len < MAX_PACKETS_SAVED) {
            PcapPacket pkt = { 0 };
            pkt.num = info->total_packets;
            pkt.ts_sec = (gint64)ts_sec;
            pkt.ts_usec = ts_usec;
            pkt.time_offset = cur_ts - first_ts;
            pkt.cap_len = cap_len;
            pkt.orig_len = orig_len;

            decode_packet(data + pos, cap_len, info->linktype,
                          &pkt.src, &pkt.dst, &pkt.protocol, &pkt.summary);

            g_array_append_val(info->packets, pkt);
        }

        pos += cap_len;
    }

    return info;
}

static PcapInfo* parse_pcapng(const guchar* data, gsize len, GError** error) {
    if (len < 32 || r32(data, FALSE) != PCAPNG_BLOCK_SHB) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not a valid pcapng file");
        return NULL;
    }

    gboolean is_le = (r32(data + 8, FALSE) != PCAPNG_BYTE_ORDER);
    PcapInfo* info = g_new0(PcapInfo, 1);
    info->format = "pcapng";
    info->linktype = 1; /* default Ethernet */
    info->link_name = linktype_name(info->linktype);
    info->snaplen = 65535;
    info->packets = g_array_new(FALSE, TRUE, sizeof(PcapPacket));

    gsize pos = 0;
    gdouble first_ts = -1.0;

    while (pos + 12 <= len) {
        guint32 block_type = r32(data + pos, is_le);
        guint32 block_len = r32(data + pos + 4, is_le);
        if (block_len < 12 || pos + block_len > len) break;

        if (block_type == 1 && block_len >= 20) { /* Interface Description Block */
            info->linktype = r16(data + pos + 8, is_le);
            info->link_name = linktype_name(info->linktype);
            info->snaplen = r32(data + pos + 12, is_le);
        } else if (block_type == 6 && block_len >= 32) { /* Enhanced Packet Block */
            guint32 ts_high = r32(data + pos + 12, is_le);
            guint32 ts_low = r32(data + pos + 16, is_le);
            guint32 cap_len = r32(data + pos + 20, is_le);
            guint32 orig_len = r32(data + pos + 24, is_le);

            guint64 ts64 = ((guint64)ts_high << 32) | ts_low;
            gdouble cur_ts = (gdouble)ts64 / 1e6; /* standard microsecond resolution */
            gint64 ts_sec = (gint64)(ts64 / 1000000ULL);

            info->total_packets++;
            info->total_cap_bytes += cap_len;
            info->total_orig_bytes += orig_len;

            if (first_ts < 0.0) {
                first_ts = cur_ts;
                info->start_time = ts_sec;
            }
            info->end_time = ts_sec;

            if (info->packets->len < MAX_PACKETS_SAVED && pos + 28 + cap_len <= pos + block_len) {
                PcapPacket pkt = { 0 };
                pkt.num = info->total_packets;
                pkt.ts_sec = ts_sec;
                pkt.ts_usec = (guint32)(ts64 % 1000000ULL);
                pkt.time_offset = cur_ts - first_ts;
                pkt.cap_len = cap_len;
                pkt.orig_len = orig_len;

                decode_packet(data + pos + 28, cap_len, info->linktype,
                              &pkt.src, &pkt.dst, &pkt.protocol, &pkt.summary);

                g_array_append_val(info->packets, pkt);
            }
        }

        pos += block_len;
    }

    return info;
}

PcapInfo* pcap_info_parse(const guchar* data, gsize len, GError** error) {
    if (data == NULL || len < 24) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "file too small for packet capture");
        return NULL;
    }

    if (data[0] == 0x0A && data[1] == 0x0D && data[2] == 0x0D && data[3] == 0x0A) {
        return parse_pcapng(data, len, error);
    }
    return parse_classic_pcap(data, len, error);
}

gboolean pcap_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error) {
    g_autoptr(PcapInfo) info = pcap_info_parse(data, len, error);
    if (info == NULL) return FALSE;

    doc->monospace = TRUE;
    doc->wrap = FALSE;

    preview_doc_field(doc, "Format", info->format);

    gchar* link_str = g_strdup_printf("%s (linktype %u)", info->link_name, info->linktype);
    preview_doc_field(doc, "Link layer", link_str);
    g_free(link_str);

    if (info->snaplen > 0) {
        gchar* snap_sz = g_format_size(info->snaplen);
        preview_doc_field(doc, "Snap length", snap_sz);
        g_free(snap_sz);
    }

    gchar* cap_sz = g_format_size(info->total_cap_bytes);
    gchar* orig_sz = g_format_size(info->total_orig_bytes);
    gchar* pkts_str = g_strdup_printf("%u %s (captured %s, wire %s)",
                                      info->total_packets,
                                      info->total_packets == 1 ? "packet" : "packets",
                                      cap_sz, orig_sz);
    preview_doc_field(doc, "Captured", pkts_str);
    g_free(pkts_str);
    g_free(orig_sz);
    g_free(cap_sz);

    if (info->start_time > 0 && info->end_time >= info->start_time) {
        GDateTime* dt1 = g_date_time_new_from_unix_local(info->start_time);
        GDateTime* dt2 = g_date_time_new_from_unix_local(info->end_time);
        if (dt1 != NULL && dt2 != NULL) {
            gchar* s1 = g_date_time_format(dt1, "%Y-%m-%d %H:%M:%S");
            gchar* s2 = g_date_time_format(dt2, "%H:%M:%S");
            gint64 dur = info->end_time - info->start_time;
            gchar* tr_str = g_strdup_printf("%s .. %s (%ld sec)", s1, s2, (long)dur);
            preview_doc_field(doc, "Time range", tr_str);
            g_free(tr_str);
            g_free(s2);
            g_free(s1);
        }
        if (dt1 != NULL) g_date_time_unref(dt1);
        if (dt2 != NULL) g_date_time_unref(dt2);
    }

    if (info->packets != NULL && info->packets->len > 0) {
        preview_doc_append_len(doc, "\n", 1);

        g_autoptr(PreviewTable) table = preview_table_new(7);
        const gchar* header[] = { "No.", "Time (s)", "Source", "Destination", "Proto", "Len", "Info" };
        preview_table_set_header(table, header);
        preview_table_set_right_aligned(table, 0);
        preview_table_set_right_aligned(table, 1);
        preview_table_set_right_aligned(table, 5);
        preview_table_set_max_cell_width(table, 50);

        for (guint i = 0; i < info->packets->len; i++) {
            const PcapPacket* p = &g_array_index(info->packets, PcapPacket, i);
            gchar* num_str = g_strdup_printf("%u", p->num);
            gchar* time_str = g_strdup_printf("%.6f", p->time_offset);
            gchar* len_str = g_strdup_printf("%u", p->cap_len);

            const gchar* row[] = {
                num_str,
                time_str,
                p->src != NULL ? p->src : "",
                p->dst != NULL ? p->dst : "",
                p->protocol != NULL ? p->protocol : "",
                len_str,
                p->summary != NULL ? p->summary : ""
            };
            preview_table_add_row(table, row, 7);

            g_free(len_str);
            g_free(time_str);
            g_free(num_str);
        }
        preview_doc_add_table(doc, table);

        if (info->total_packets > info->packets->len) {
            gchar* note = g_strdup_printf("\nShowing first %u of %u captured packets.",
                                          info->packets->len, info->total_packets);
            preview_doc_note(doc, note);
            g_free(note);
        }
    }

    return TRUE;
}
