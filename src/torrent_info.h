#ifndef TORRENT_INFO_H
#define TORRENT_INFO_H

#include <glib.h>
#include "preview_doc.h"

/* BitTorrent metainfo (.torrent) bencode parser and preview builder.
 * Extracts torrent name, file list, total size, piece size, trackers,
 * creation date, and comments.
 * Pure GLib, Layer 1 foundation module (no widgets, unit-tested). */

typedef struct {
    gchar* path;
    guint64 size;
} TorrentFile;

typedef struct {
    gchar* name;
    guint64 total_size;
    guint64 piece_length;
    guint n_pieces;
    gint64 creation_date; /* Unix time, or -1 */
    gchar* created_by;
    gchar* comment;
    GPtrArray* trackers;  /* gchar* */
    GArray* files;        /* TorrentFile */
} TorrentInfo;

TorrentInfo* torrent_info_parse(const guchar* data, gsize len, GError** error);
void torrent_info_free(TorrentInfo* info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(TorrentInfo, torrent_info_free)

/* Appends structured BitTorrent preview to doc */
gboolean torrent_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error);

#endif /* TORRENT_INFO_H */
