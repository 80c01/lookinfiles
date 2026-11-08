#ifndef PCAP_INFO_H
#define PCAP_INFO_H

#include <glib.h>
#include "preview/preview_doc.h"

/* PCAP / PCAPNG network packet capture parser and preview builder.
 * Extracts capture format, link layer type, snaplen, packet counts,
 * timestamp range, total wire/capture bytes, and decodes packet headers
 * (Ethernet, IPv4, IPv6, ARP, TCP, UDP, ICMP).
 * Layer 1 foundation module (pure GLib, no widgets, unit-tested). */

typedef struct {
    guint num;
    gdouble time_offset; /* seconds relative to first packet */
    gint64 ts_sec;
    guint32 ts_usec;
    guint32 cap_len;
    guint32 orig_len;
    gchar* src;
    gchar* dst;
    const gchar* protocol;
    gchar* summary;
} PcapPacket;

typedef struct {
    const gchar* format;    /* "libpcap", "pcapng" */
    const gchar* link_name; /* "Ethernet", "Linux Cooked", "Raw IP", etc. */
    guint32 linktype;
    guint32 snaplen;
    guint32 major_version;
    guint32 minor_version;
    guint total_packets;
    guint64 total_cap_bytes;
    guint64 total_orig_bytes;
    gint64 start_time;      /* epoch seconds */
    gint64 end_time;
    GArray* packets;        /* PcapPacket */
} PcapInfo;

PcapInfo* pcap_info_parse(const guchar* data, gsize len, GError** error);
void pcap_info_free(PcapInfo* info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PcapInfo, pcap_info_free)

/* Appends structured PCAP preview to doc */
gboolean pcap_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error);

#endif /* PCAP_INFO_H */
