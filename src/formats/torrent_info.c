/* BitTorrent metainfo (.torrent) bencode parser and preview builder. */
#include "formats/torrent_info.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BENCODE_DEPTH 24
#define MAX_FILES_SHOWN 500

typedef struct {
    const guint8* data;
    gsize len;
    gsize pos;
} BencodeReader;

typedef enum {
    BENCODE_INT,
    BENCODE_STR,
    BENCODE_LIST,
    BENCODE_DICT
} BencodeType;

typedef struct BencodeValue BencodeValue;

struct BencodeValue {
    BencodeType type;
    union {
        gint64 num;
        struct {
            gchar* str;
            gsize len;
        } str;
        GPtrArray* list;
        GHashTable* dict;
    } v;
};

static void bencode_free(BencodeValue* val) {
    if (val == NULL) return;
    switch (val->type) {
    case BENCODE_STR:
        g_free(val->v.str.str);
        break;
    case BENCODE_LIST:
        if (val->v.list != NULL) {
            for (guint i = 0; i < val->v.list->len; i++) {
                bencode_free((BencodeValue*)g_ptr_array_index(val->v.list, i));
            }
            g_ptr_array_free(val->v.list, TRUE);
        }
        break;
    case BENCODE_DICT:
        if (val->v.dict != NULL) {
            GHashTableIter iter;
            gpointer k, v;
            g_hash_table_iter_init(&iter, val->v.dict);
            while (g_hash_table_iter_next(&iter, &k, &v)) {
                g_free(k);
                bencode_free((BencodeValue*)v);
            }
            g_hash_table_destroy(val->v.dict);
        }
        break;
    default:
        break;
    }
    g_free(val);
}

static BencodeValue* parse_value(BencodeReader* r, guint depth);

static BencodeValue* parse_int(BencodeReader* r) {
    if (r->pos >= r->len || r->data[r->pos] != 'i') return NULL;
    r->pos++; /* skip 'i' */
    gsize start = r->pos;
    if (r->pos < r->len && r->data[r->pos] == '-') r->pos++;
    while (r->pos < r->len && g_ascii_isdigit(r->data[r->pos])) r->pos++;
    if (r->pos >= r->len || r->data[r->pos] != 'e' || r->pos == start) return NULL;
    gchar* buf = g_strndup((const gchar*)r->data + start, r->pos - start);
    gint64 num = g_ascii_strtoll(buf, NULL, 10);
    g_free(buf);
    r->pos++; /* skip 'e' */
    BencodeValue* val = g_new0(BencodeValue, 1);
    val->type = BENCODE_INT;
    val->v.num = num;
    return val;
}

static BencodeValue* parse_str(BencodeReader* r) {
    gsize start = r->pos;
    while (r->pos < r->len && g_ascii_isdigit(r->data[r->pos])) r->pos++;
    if (r->pos >= r->len || r->data[r->pos] != ':' || r->pos == start) {
        r->pos = start;
        return NULL;
    }
    gchar* len_str = g_strndup((const gchar*)r->data + start, r->pos - start);
    gint64 slen = g_ascii_strtoll(len_str, NULL, 10);
    g_free(len_str);
    if (slen < 0) return NULL;
    r->pos++; /* skip ':' */
    if (r->pos + (gsize)slen > r->len) return NULL;
    BencodeValue* val = g_new0(BencodeValue, 1);
    val->type = BENCODE_STR;
    val->v.str.str = g_strndup((const gchar*)r->data + r->pos, (gsize)slen);
    val->v.str.len = (gsize)slen;
    r->pos += (gsize)slen;
    return val;
}

static BencodeValue* parse_list(BencodeReader* r, guint depth) {
    if (r->pos >= r->len || r->data[r->pos] != 'l') return NULL;
    r->pos++; /* skip 'l' */
    GPtrArray* list = g_ptr_array_new();
    while (r->pos < r->len && r->data[r->pos] != 'e') {
        BencodeValue* item = parse_value(r, depth + 1);
        if (item == NULL) {
            for (guint i = 0; i < list->len; i++) bencode_free((BencodeValue*)g_ptr_array_index(list, i));
            g_ptr_array_free(list, TRUE);
            return NULL;
        }
        g_ptr_array_add(list, item);
    }
    if (r->pos >= r->len || r->data[r->pos] != 'e') {
        for (guint i = 0; i < list->len; i++) bencode_free((BencodeValue*)g_ptr_array_index(list, i));
        g_ptr_array_free(list, TRUE);
        return NULL;
    }
    r->pos++; /* skip 'e' */
    BencodeValue* val = g_new0(BencodeValue, 1);
    val->type = BENCODE_LIST;
    val->v.list = list;
    return val;
}

static BencodeValue* parse_dict(BencodeReader* r, guint depth) {
    if (r->pos >= r->len || r->data[r->pos] != 'd') return NULL;
    r->pos++; /* skip 'd' */
    GHashTable* dict = g_hash_table_new(g_str_hash, g_str_equal);
    while (r->pos < r->len && r->data[r->pos] != 'e') {
        BencodeValue* key = parse_str(r);
        if (key == NULL) {
            GHashTableIter iter; gpointer k, v;
            g_hash_table_iter_init(&iter, dict);
            while (g_hash_table_iter_next(&iter, &k, &v)) { g_free(k); bencode_free((BencodeValue*)v); }
            g_hash_table_destroy(dict);
            return NULL;
        }
        BencodeValue* val = parse_value(r, depth + 1);
        if (val == NULL) {
            g_free(key->v.str.str); g_free(key);
            GHashTableIter iter; gpointer k, v;
            g_hash_table_iter_init(&iter, dict);
            while (g_hash_table_iter_next(&iter, &k, &v)) { g_free(k); bencode_free((BencodeValue*)v); }
            g_hash_table_destroy(dict);
            return NULL;
        }
        g_hash_table_insert(dict, key->v.str.str, val);
        g_free(key);
    }
    if (r->pos >= r->len || r->data[r->pos] != 'e') {
        GHashTableIter iter; gpointer k, v;
        g_hash_table_iter_init(&iter, dict);
        while (g_hash_table_iter_next(&iter, &k, &v)) { g_free(k); bencode_free((BencodeValue*)v); }
        g_hash_table_destroy(dict);
        return NULL;
    }
    r->pos++; /* skip 'e' */
    BencodeValue* val = g_new0(BencodeValue, 1);
    val->type = BENCODE_DICT;
    val->v.dict = dict;
    return val;
}

static BencodeValue* parse_value(BencodeReader* r, guint depth) {
    if (r->pos >= r->len || depth > MAX_BENCODE_DEPTH) return NULL;
    guint8 c = r->data[r->pos];
    if (c == 'i') return parse_int(r);
    if (c == 'l') return parse_list(r, depth);
    if (c == 'd') return parse_dict(r, depth);
    if (g_ascii_isdigit(c)) return parse_str(r);
    return NULL;
}

static const gchar* dict_get_str(GHashTable* dict, const gchar* key) {
    BencodeValue* v = g_hash_table_lookup(dict, key);
    return (v != NULL && v->type == BENCODE_STR) ? v->v.str.str : NULL;
}

static gint64 dict_get_int(GHashTable* dict, const gchar* key, gint64 def) {
    BencodeValue* v = g_hash_table_lookup(dict, key);
    return (v != NULL && v->type == BENCODE_INT) ? v->v.num : def;
}

static GHashTable* dict_get_dict(GHashTable* dict, const gchar* key) {
    BencodeValue* v = g_hash_table_lookup(dict, key);
    return (v != NULL && v->type == BENCODE_DICT) ? v->v.dict : NULL;
}

static GPtrArray* dict_get_list(GHashTable* dict, const gchar* key) {
    BencodeValue* v = g_hash_table_lookup(dict, key);
    return (v != NULL && v->type == BENCODE_LIST) ? v->v.list : NULL;
}

void torrent_info_free(TorrentInfo* info) {
    if (info == NULL) return;
    g_free(info->name);
    g_free(info->created_by);
    g_free(info->comment);
    if (info->trackers != NULL) g_ptr_array_free(info->trackers, TRUE);
    if (info->files != NULL) {
        for (guint i = 0; i < info->files->len; i++) {
            TorrentFile* f = &g_array_index(info->files, TorrentFile, i);
            g_free(f->path);
        }
        g_array_free(info->files, TRUE);
    }
    g_free(info);
}

TorrentInfo* torrent_info_parse(const guchar* data, gsize len, GError** error) {
    if (data == NULL || len < 10) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("file too small for torrent"));
        return NULL;
    }
    BencodeReader r = { .data = data, .len = len, .pos = 0 };
    BencodeValue* root = parse_value(&r, 0);
    if (root == NULL || root->type != BENCODE_DICT) {
        bencode_free(root);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("invalid bencode root dictionary"));
        return NULL;
    }

    GHashTable* d = root->v.dict;
    GHashTable* info_dict = dict_get_dict(d, "info");
    if (info_dict == NULL) {
        bencode_free(root);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("torrent missing 'info' dictionary"));
        return NULL;
    }

    TorrentInfo* info = g_new0(TorrentInfo, 1);
    info->trackers = g_ptr_array_new_with_free_func(g_free);
    info->files = g_array_new(FALSE, TRUE, sizeof(TorrentFile));
    info->creation_date = dict_get_int(d, "creation date", -1);

    const gchar* ann = dict_get_str(d, "announce");
    if (ann != NULL) g_ptr_array_add(info->trackers, g_strdup(ann));

    GPtrArray* ann_list = dict_get_list(d, "announce-list");
    if (ann_list != NULL) {
        for (guint i = 0; i < ann_list->len; i++) {
            BencodeValue* tier = g_ptr_array_index(ann_list, i);
            if (tier->type == BENCODE_LIST) {
                for (guint j = 0; j < tier->v.list->len; j++) {
                    BencodeValue* turl = g_ptr_array_index(tier->v.list, j);
                    if (turl->type == BENCODE_STR && g_strcmp0(turl->v.str.str, ann) != 0) {
                        g_ptr_array_add(info->trackers, g_strdup(turl->v.str.str));
                    }
                }
            }
        }
    }

    const gchar* cb = dict_get_str(d, "created by");
    if (cb != NULL) info->created_by = g_strdup(cb);

    const gchar* cm = dict_get_str(d, "comment");
    if (cm != NULL) info->comment = g_strdup(cm);

    const gchar* iname = dict_get_str(info_dict, "name");
    info->name = iname != NULL ? g_strdup(iname) : g_strdup("unnamed");

    info->piece_length = (guint64)dict_get_int(info_dict, "piece length", 0);
    BencodeValue* pieces_val = g_hash_table_lookup(info_dict, "pieces");
    if (pieces_val != NULL && pieces_val->type == BENCODE_STR) {
        info->n_pieces = (guint)(pieces_val->v.str.len / 20);
    }

    /* Single file or multi-file mode */
    GPtrArray* files_list = dict_get_list(info_dict, "files");
    if (files_list != NULL) {
        for (guint i = 0; i < files_list->len; i++) {
            BencodeValue* fe = g_ptr_array_index(files_list, i);
            if (fe->type == BENCODE_DICT) {
                gint64 flen = dict_get_int(fe->v.dict, "length", 0);
                if (flen < 0) flen = 0;
                info->total_size += (guint64)flen;
                GPtrArray* path_parts = dict_get_list(fe->v.dict, "path");
                if (path_parts != NULL && info->files->len < MAX_FILES_SHOWN) {
                    GString* path_str = g_string_new(NULL);
                    for (guint p = 0; p < path_parts->len; p++) {
                        BencodeValue* part = g_ptr_array_index(path_parts, p);
                        if (part->type == BENCODE_STR) {
                            if (p > 0) g_string_append_c(path_str, '/');
                            g_string_append(path_str, part->v.str.str);
                        }
                    }
                    TorrentFile tf = { .path = g_string_free(path_str, FALSE), .size = (guint64)flen };
                    g_array_append_val(info->files, tf);
                }
            }
        }
    } else {
        gint64 flen = dict_get_int(info_dict, "length", 0);
        if (flen < 0) flen = 0;
        info->total_size = (guint64)flen;
        TorrentFile tf = { .path = g_strdup(info->name), .size = info->total_size };
        g_array_append_val(info->files, tf);
    }

    bencode_free(root);
    return info;
}

gboolean torrent_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error) {
    g_autoptr(TorrentInfo) info = torrent_info_parse(data, len, error);
    if (info == NULL) return FALSE;

    doc->monospace = TRUE;
    doc->wrap = FALSE;

    preview_doc_field(doc, _("Name"), info->name);

    gchar* total_str = g_format_size(info->total_size);
    preview_doc_field(doc, _("Total size"), total_str);
    g_free(total_str);

    if (info->files != NULL) {
        gchar* count_str = g_strdup_printf("%u %s", info->files->len,
                                           info->files->len == 1 ? "file" : "files");
        preview_doc_field(doc, _("Files"), count_str);
        g_free(count_str);
    }

    if (info->piece_length > 0) {
        gchar* piece_sz = g_format_size(info->piece_length);
        gchar* piece_str = g_strdup_printf(ngettext("%1$s (%2$u piece)", "%1$s (%2$u pieces)", info->n_pieces), piece_sz, info->n_pieces);
        preview_doc_field(doc, _("Piece size"), piece_str);
        g_free(piece_str);
        g_free(piece_sz);
    }

    if (info->creation_date > 0) {
        GDateTime* dt = g_date_time_new_from_unix_local(info->creation_date);
        if (dt != NULL) {
            gchar* d_str = g_date_time_format(dt, "%Y-%m-%d %H:%M");
            preview_doc_field(doc, _("Created"), d_str);
            g_free(d_str);
            g_date_time_unref(dt);
        }
    }

    if (info->created_by != NULL) {
        preview_doc_field(doc, _("Created by"), info->created_by);
    }
    if (info->comment != NULL) {
        preview_doc_field(doc, _("Comment"), info->comment);
    }

    if (info->trackers != NULL && info->trackers->len > 0) {
        preview_doc_field(doc, _("Primary tracker"), (const gchar*)g_ptr_array_index(info->trackers, 0));
    }

    /* Table of files */
    if (info->files != NULL && info->files->len > 0) {
        preview_doc_append_len(doc, "\n", 1);
        g_autoptr(PreviewTable) table = preview_table_new(2);
        const gchar* header[] = { _("File Path"), _("Size") };
        preview_table_set_header(table, header);
        preview_table_set_right_aligned(table, 1);
        preview_table_set_max_cell_width(table, 80);

        for (guint i = 0; i < info->files->len; i++) {
            const TorrentFile* f = &g_array_index(info->files, TorrentFile, i);
            gchar* fsz = g_format_size(f->size);
            const gchar* row[] = { f->path, fsz };
            preview_table_add_row(table, row, 2);
            g_free(fsz);
        }
        preview_doc_add_table(doc, table);
    }

    if (info->trackers != NULL && info->trackers->len > 1) {
        preview_doc_section(doc, _("Trackers"));
        for (guint i = 0; i < info->trackers->len; i++) {
            const gchar* tr = g_ptr_array_index(info->trackers, i);
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, "  • ");
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, tr);
            preview_doc_append_len(doc, "\n", 1);
        }
    }

    return TRUE;
}
