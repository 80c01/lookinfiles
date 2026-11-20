/* Apache Parquet metadata in the Thrift compact protocol (see parquet_meta.h). */
#include "formats/parquet_meta.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

/* --- Thrift compact protocol --- */

enum {
    TC_STOP = 0, TC_TRUE = 1, TC_FALSE = 2, TC_BYTE = 3, TC_I16 = 4, TC_I32 = 5, TC_I64 = 6,
    TC_DOUBLE = 7, TC_BINARY = 8, TC_LIST = 9, TC_SET = 10, TC_MAP = 11, TC_STRUCT = 12
};

/* Nesting deeper than this is not a Parquet footer */
#define TC_MAX_DEPTH 32

typedef struct {
    const guchar* p;
    const guchar* end;
    gboolean bad;
    gint depth;
} TReader;

static guint64 tc_varint(TReader* r) {
    guint64 v = 0;
    for (guint shift = 0; shift < 64; shift += 7) {
        if (r->p >= r->end) {
            r->bad = TRUE;
            return 0;
        }
        guchar b = *r->p++;
        v |= (guint64)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return v;
    }
    r->bad = TRUE;
    return 0;
}

static gint64 tc_zigzag(TReader* r) {
    guint64 v = tc_varint(r);
    return (gint64)(v >> 1) ^ -(gint64)(v & 1);
}

static gint32 tc_i32(TReader* r) {
    return (gint32)tc_zigzag(r);
}

/* A binary / string field: pointer into the data, NULL if it does not fit */
static const guchar* tc_binary(TReader* r, gsize* len) {
    guint64 n = tc_varint(r);
    if (r->bad || n > (guint64)(r->end - r->p)) {
        r->bad = TRUE;
        *len = 0;
        return NULL;
    }
    const guchar* s = r->p;
    r->p += n;
    *len = (gsize)n;
    return s;
}

static gchar* tc_string(TReader* r) {
    gsize len = 0;
    const guchar* s = tc_binary(r, &len);
    if (s == NULL) return NULL;
    gchar* text = g_strndup((const gchar*)s, len);
    if (!g_utf8_validate(text, -1, NULL)) {
        gchar* valid = g_utf8_make_valid(text, -1);
        g_free(text);
        return valid;
    }
    return text;
}

/* Next field of a struct: FALSE at its end. *id follows the deltas. */
static gboolean tc_field(TReader* r, gint* id, gint* type) {
    if (r->bad || r->p >= r->end) {
        r->bad = TRUE;
        return FALSE;
    }
    guchar b = *r->p++;
    if (b == TC_STOP) return FALSE;
    *type = b & 0x0F;
    gint delta = b >> 4;
    *id = delta != 0 ? *id + delta : (gint)(gint16)tc_zigzag(r);
    return !r->bad;
}

/* List header: element count and type */
static guint64 tc_list(TReader* r, gint* elem) {
    if (r->p >= r->end) {
        r->bad = TRUE;
        return 0;
    }
    guchar b = *r->p++;
    *elem = b & 0x0F;
    guint64 n = b >> 4;
    if (n == 15) n = tc_varint(r);
    /* Each element takes a byte at least */
    if (n > (guint64)(r->end - r->p)) {
        r->bad = TRUE;
        return 0;
    }
    return n;
}

static void tc_skip(TReader* r, gint type);

static void tc_skip_struct(TReader* r) {
    if (++r->depth > TC_MAX_DEPTH) {
        r->bad = TRUE;
        return;
    }
    gint id = 0, type = 0;
    while (tc_field(r, &id, &type)) tc_skip(r, type);
    r->depth--;
}

static void tc_skip(TReader* r, gint type) {
    gsize len;
    gint elem = 0;
    switch (type) {
    case TC_TRUE: case TC_FALSE: break;          /* the value is in the type */
    case TC_BYTE:
        if (r->p < r->end) r->p++; else r->bad = TRUE;
        break;
    case TC_I16: case TC_I32: case TC_I64: tc_varint(r); break;
    case TC_DOUBLE:
        if (r->end - r->p >= 8) r->p += 8; else r->bad = TRUE;
        break;
    case TC_BINARY: tc_binary(r, &len); break;
    case TC_LIST: case TC_SET: {
        guint64 n = tc_list(r, &elem);
        for (guint64 i = 0; i < n && !r->bad; i++) {
            if (elem == TC_TRUE || elem == TC_FALSE) tc_skip(r, TC_BYTE); /* one byte each in lists */
            else tc_skip(r, elem);
        }
        break;
    }
    case TC_MAP: {
        guint64 n = tc_varint(r);
        if (n == 0) break;
        if (r->p >= r->end || n > (guint64)(r->end - r->p)) {
            r->bad = TRUE;
            break;
        }
        guchar kv = *r->p++;
        for (guint64 i = 0; i < n && !r->bad; i++) {
            tc_skip(r, kv >> 4);
            tc_skip(r, kv & 0x0F);
        }
        break;
    }
    case TC_STRUCT: tc_skip_struct(r); break;
    default: r->bad = TRUE; break;
    }
}

/* A boolean field: in its type, or a byte (in lists) */
static gboolean tc_bool(gint type) {
    return type == TC_TRUE;
}

/* --- Schema --- */

static void parse_time_unit(TReader* r, ParquetNode* n) {
    gint id = 0, type = 0;
    while (tc_field(r, &id, &type)) {
        if (type == TC_STRUCT && id >= 1 && id <= 3) n->unit = (ParquetTimeUnit)(id - 1);
        tc_skip(r, type);
    }
}

/* DecimalType, TimeType, TimestampType, IntType */
static void parse_logical_params(TReader* r, ParquetNode* n) {
    gint id = 0, type = 0;
    while (tc_field(r, &id, &type)) {
        if (n->logical == PARQUET_LOGICAL_DECIMAL && type == TC_I32 && id <= 2) {
            gint v = tc_i32(r);
            if (id == 1) n->scale = v; else n->precision = v;
        } else if ((n->logical == PARQUET_LOGICAL_TIME || n->logical == PARQUET_LOGICAL_TIMESTAMP) &&
                   id == 1 && (type == TC_TRUE || type == TC_FALSE)) {
            n->utc = tc_bool(type);
        } else if ((n->logical == PARQUET_LOGICAL_TIME || n->logical == PARQUET_LOGICAL_TIMESTAMP) &&
                   id == 2 && type == TC_STRUCT) {
            parse_time_unit(r, n);
        } else if (n->logical == PARQUET_LOGICAL_INTEGER && id == 1 && type == TC_BYTE && r->p < r->end) {
            n->int_bits = (gint8)*r->p++;
        } else if (n->logical == PARQUET_LOGICAL_INTEGER && id == 2 && (type == TC_TRUE || type == TC_FALSE)) {
            n->int_signed = tc_bool(type);
        } else {
            tc_skip(r, type);
        }
    }
}

static void parse_logical(TReader* r, ParquetNode* n) {
    gint id = 0, type = 0;
    while (tc_field(r, &id, &type)) {
        if (type == TC_STRUCT && n->logical == PARQUET_LOGICAL_NONE) {
            n->logical = (ParquetLogical)id;
            parse_logical_params(r, n);
        } else {
            tc_skip(r, type);
        }
    }
}

static void parse_node(TReader* r, ParquetNode* n) {
    n->type = -1;
    n->repetition = PARQUET_REQUIRED;
    n->converted = -1;
    n->int_signed = TRUE;
    gint id = 0, type = 0;
    while (tc_field(r, &id, &type)) {
        if (id == 4 && type == TC_BINARY) { g_free(n->name); n->name = tc_string(r); continue; }
        if (id == 10 && type == TC_STRUCT) { parse_logical(r, n); continue; }
        if (type != TC_I32) { tc_skip(r, type); continue; }
        gint v = tc_i32(r);
        switch (id) {
        case 1: n->type = v; break;
        case 2: n->type_length = v; break;
        case 3: n->repetition = v; break;
        case 5: n->num_children = v; break;
        case 6: n->converted = v; break;
        case 7: n->scale = v; break;
        case 8: n->precision = v; break;
        default: break;
        }
    }
    if (n->name == NULL) n->name = g_strdup("");
    /* Old files: the converted type stands for the logical type */
    if (n->logical == PARQUET_LOGICAL_NONE) {
        switch (n->converted) {
        case PARQUET_CONVERTED_UTF8: n->logical = PARQUET_LOGICAL_STRING; break;
        case PARQUET_CONVERTED_MAP: n->logical = PARQUET_LOGICAL_MAP; break;
        case PARQUET_CONVERTED_LIST: n->logical = PARQUET_LOGICAL_LIST; break;
        case PARQUET_CONVERTED_ENUM: n->logical = PARQUET_LOGICAL_ENUM; break;
        case PARQUET_CONVERTED_DECIMAL: n->logical = PARQUET_LOGICAL_DECIMAL; break;
        case PARQUET_CONVERTED_DATE: n->logical = PARQUET_LOGICAL_DATE; break;
        case PARQUET_CONVERTED_TIME_MILLIS: case PARQUET_CONVERTED_TIME_MICROS:
            n->logical = PARQUET_LOGICAL_TIME;
            n->unit = n->converted == PARQUET_CONVERTED_TIME_MILLIS ? PARQUET_MILLIS : PARQUET_MICROS;
            n->utc = TRUE;
            break;
        case PARQUET_CONVERTED_TIMESTAMP_MILLIS: case PARQUET_CONVERTED_TIMESTAMP_MICROS:
            n->logical = PARQUET_LOGICAL_TIMESTAMP;
            n->unit = n->converted == PARQUET_CONVERTED_TIMESTAMP_MILLIS ? PARQUET_MILLIS : PARQUET_MICROS;
            n->utc = TRUE;
            break;
        case PARQUET_CONVERTED_JSON: n->logical = PARQUET_LOGICAL_JSON; break;
        case PARQUET_CONVERTED_BSON: n->logical = PARQUET_LOGICAL_BSON; break;
        default:
            if (n->converted >= PARQUET_CONVERTED_UINT_8 && n->converted <= PARQUET_CONVERTED_INT_64) {
                gint k = n->converted - PARQUET_CONVERTED_UINT_8;
                n->logical = PARQUET_LOGICAL_INTEGER;
                n->int_signed = k >= 4;
                n->int_bits = 8 << (k % 4);
            }
            break;
        }
    }
}

typedef struct {
    ParquetMeta* meta;
    GArray* leaves;
    GPtrArray* names;    /* path of the current node */
    GPtrArray* labels;   /* its names shown (NULL: a wrapper level) */
    guint next;          /* next node to read */
} SchemaWalk;

/* Levels from the root to a node */
typedef struct {
    gint def;
    gint rep;
    gint rep_def[PARQUET_MAX_REP];
    gboolean rep_map[PARQUET_MAX_REP];
} Levels;

static gboolean is_list_group(const ParquetNode* n) {
    return n->type < 0 && (n->logical == PARQUET_LOGICAL_LIST || n->logical == PARQUET_LOGICAL_MAP);
}

static gchar* join_names(GPtrArray* names) {
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < names->len; i++) {
        const gchar* name = g_ptr_array_index(names, i);
        if (name == NULL) continue;
        if (s->len > 0) g_string_append_c(s, '.');
        g_string_append(s, name);
    }
    return g_string_free(s, FALSE);
}

/* Visit node walk->next and its children. hidden: the node is a wrapper
 * level of a list or map (its name is not shown); parent: the logical type
 * of its parent. */
static gboolean walk_node(SchemaWalk* w, Levels lv, gboolean hidden, ParquetLogical parent, gint depth) {
    if (w->next >= w->meta->n_nodes || depth > TC_MAX_DEPTH) return FALSE;
    guint index = w->next++;
    ParquetNode* n = &w->meta->nodes[index];
    if (index > 0) {
        if (n->repetition == PARQUET_OPTIONAL) lv.def++;
        if (n->repetition == PARQUET_REPEATED) {
            lv.def++;
            if (lv.rep < PARQUET_MAX_REP) {
                lv.rep_def[lv.rep] = lv.def;
                lv.rep_map[lv.rep] = parent == PARQUET_LOGICAL_MAP;
            }
            lv.rep++;
        }
        g_ptr_array_add(w->names, n->name);
        g_ptr_array_add(w->labels, hidden ? NULL : n->name);
    }
    gboolean ok = TRUE;
    if (n->type < 0 || (index == 0 && n->num_children > 0)) {
        /* The repeated level of a LIST or MAP, and the single element
         * under a list's repeated level, are wrappers */
        gboolean child_hidden = is_list_group(n) ||
                                (hidden && n->repetition == PARQUET_REPEATED && parent == PARQUET_LOGICAL_LIST &&
                                 n->num_children == 1);
        for (gint i = 0; i < n->num_children && ok; i++) {
            ok = walk_node(w, lv, child_hidden, n->type < 0 ? n->logical : PARQUET_LOGICAL_NONE, depth + 1);
        }
    } else if (index > 0) {
        ParquetLeaf leaf = { .node = index, .max_def = lv.def, .max_rep = lv.rep };
        memcpy(leaf.rep_def, lv.rep_def, sizeof(leaf.rep_def));
        memcpy(leaf.rep_map, lv.rep_map, sizeof(leaf.rep_map));
        leaf.path = join_names(w->names);
        leaf.label = join_names(w->labels);
        if (*leaf.label == '\0') {
            g_free(leaf.label);
            leaf.label = g_strdup(leaf.path);
        }
        g_array_append_val(w->leaves, leaf);
    }
    if (index > 0) {
        g_ptr_array_set_size(w->names, w->names->len - 1);
        g_ptr_array_set_size(w->labels, w->labels->len - 1);
    }
    return ok;
}

static gboolean build_leaves(ParquetMeta* meta) {
    SchemaWalk w = { .meta = meta, .leaves = g_array_new(FALSE, TRUE, sizeof(ParquetLeaf)),
                     .names = g_ptr_array_new(), .labels = g_ptr_array_new() };
    Levels lv = { 0 };
    gboolean ok = meta->n_nodes > 0 && walk_node(&w, lv, FALSE, PARQUET_LOGICAL_NONE, 0);
    g_ptr_array_free(w.names, TRUE);
    g_ptr_array_free(w.labels, TRUE);
    meta->n_leaves = w.leaves->len;
    meta->leaves = (ParquetLeaf*)(void*)g_array_free(w.leaves, FALSE);
    return ok && meta->n_leaves > 0;
}

/* --- Row groups --- */

static void parse_chunk_meta(TReader* r, ParquetChunk* c) {
    gint id = 0, type = 0;
    while (tc_field(r, &id, &type)) {
        if (id == 2 && type == TC_LIST) {
            gint elem = 0;
            guint64 n = tc_list(r, &elem);
            for (guint64 i = 0; i < n && !r->bad; i++) {
                if (elem != TC_I32) { tc_skip(r, elem); continue; }
                gint e = tc_i32(r);
                if (e >= 0 && e < PARQUET_ENCODING_COUNT) c->encodings |= 1u << e;
            }
            continue;
        }
        if (type == TC_I32 && (id == 1 || id == 4)) {
            gint v = tc_i32(r);
            if (id == 1) c->type = v; else c->codec = v;
            continue;
        }
        if (type != TC_I64) { tc_skip(r, type); continue; }
        gint64 v = tc_zigzag(r);
        switch (id) {
        case 5: c->num_values = v; break;
        case 6: c->uncompressed_size = v; break;
        case 7: c->compressed_size = v; break;
        case 9: c->data_page_offset = v; break;
        case 11: c->dictionary_page_offset = v; break;
        default: break;
        }
    }
}

static void parse_chunk(TReader* r, ParquetChunk* c) {
    gboolean has_meta = FALSE;
    gint id = 0, type = 0;
    while (tc_field(r, &id, &type)) {
        if (id == 1 && type == TC_BINARY) {
            gsize len = 0;
            tc_binary(r, &len);
            if (len > 0) c->external = TRUE;
        } else if (id == 3 && type == TC_STRUCT) {
            parse_chunk_meta(r, c);
            has_meta = TRUE;
        } else {
            tc_skip(r, type);
        }
    }
    if (!has_meta) c->external = TRUE;
}

static void parse_row_group(TReader* r, ParquetRowGroup* g) {
    gint id = 0, type = 0;
    while (tc_field(r, &id, &type)) {
        if (id == 1 && type == TC_LIST) {
            gint elem = 0;
            guint64 n = tc_list(r, &elem);
            if (elem != TC_STRUCT) { r->bad = TRUE; return; }
            g_free(g->chunks);
            g->chunks = g_new0(ParquetChunk, n);
            g->n_chunks = (guint)n;
            for (guint64 i = 0; i < n && !r->bad; i++) {
                parse_chunk(r, &g->chunks[i]);
                g->compressed_size += g->chunks[i].compressed_size;
            }
        } else if (type == TC_I64 && (id == 2 || id == 3)) {
            gint64 v = tc_zigzag(r);
            if (id == 2) g->total_byte_size = v; else g->num_rows = v;
        } else {
            tc_skip(r, type);
        }
    }
}

/* --- FileMetaData --- */

static void parse_key_values(TReader* r, ParquetMeta* m) {
    gint elem = 0;
    guint64 n = tc_list(r, &elem);
    if (elem != TC_STRUCT) { r->bad = TRUE; return; }
    m->keys = g_new0(gchar*, n + 1);
    m->values = g_new0(gchar*, n + 1);
    for (guint64 i = 0; i < n && !r->bad; i++) {
        gint id = 0, type = 0;
        gchar* key = NULL;
        gchar* value = NULL;
        while (tc_field(r, &id, &type)) {
            if (type == TC_BINARY && id == 1) { g_free(key); key = tc_string(r); }
            else if (type == TC_BINARY && id == 2) { g_free(value); value = tc_string(r); }
            else tc_skip(r, type);
        }
        m->keys[m->n_keys] = key != NULL ? key : g_strdup("");
        m->values[m->n_keys++] = value;
    }
}

ParquetMeta* parquet_meta_parse(const guchar* data, gsize len, GError** error) {
    TReader r = { .p = data, .end = data + len };
    ParquetMeta* m = g_new0(ParquetMeta, 1);
    gboolean have_schema = FALSE;
    gint id = 0, type = 0;
    while (data != NULL && tc_field(&r, &id, &type)) {
        if (id == 1 && type == TC_I32) {
            m->version = tc_i32(&r);
        } else if (id == 2 && type == TC_LIST && !have_schema) {
            gint elem = 0;
            guint64 n = tc_list(&r, &elem);
            if (elem != TC_STRUCT) { r.bad = TRUE; break; }
            m->nodes = g_new0(ParquetNode, n);
            for (guint64 i = 0; i < n && !r.bad; i++) {
                parse_node(&r, &m->nodes[i]);
                m->n_nodes++;
            }
            have_schema = TRUE;
        } else if (id == 3 && type == TC_I64) {
            m->num_rows = tc_zigzag(&r);
        } else if (id == 4 && type == TC_LIST && m->row_groups == NULL) {
            gint elem = 0;
            guint64 n = tc_list(&r, &elem);
            if (elem != TC_STRUCT) { r.bad = TRUE; break; }
            m->row_groups = g_new0(ParquetRowGroup, n);
            for (guint64 i = 0; i < n && !r.bad; i++) {
                parse_row_group(&r, &m->row_groups[i]);
                m->n_row_groups++;
            }
        } else if (id == 5 && type == TC_LIST && m->keys == NULL) {
            parse_key_values(&r, m);
        } else if (id == 6 && type == TC_BINARY) {
            g_free(m->created_by);
            m->created_by = tc_string(&r);
        } else {
            tc_skip(&r, type);
        }
    }
    if (data == NULL || r.bad || !have_schema || !build_leaves(m)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("invalid Parquet footer"));
        parquet_meta_free(m);
        return NULL;
    }
    /* Chunks are matched to leaves by position: a row group that does not
     * have one per leaf is unusable */
    for (guint i = 0; i < m->n_row_groups; i++) {
        if (m->row_groups[i].n_chunks != m->n_leaves) {
            g_free(m->row_groups[i].chunks);
            m->row_groups[i].chunks = NULL;
            m->row_groups[i].n_chunks = 0;
        }
    }
    return m;
}

void parquet_meta_free(ParquetMeta* m) {
    if (m == NULL) return;
    for (guint i = 0; i < m->n_nodes; i++) g_free(m->nodes[i].name);
    g_free(m->nodes);
    for (guint i = 0; i < m->n_leaves; i++) {
        g_free(m->leaves[i].path);
        g_free(m->leaves[i].label);
    }
    g_free(m->leaves);
    for (guint i = 0; i < m->n_row_groups; i++) g_free(m->row_groups[i].chunks);
    g_free(m->row_groups);
    for (guint i = 0; i < m->n_keys; i++) {
        g_free(m->keys[i]);
        g_free(m->values[i]);
    }
    g_free(m->keys);
    g_free(m->values);
    g_free(m->created_by);
    g_free(m);
}

/* --- Page headers --- */

static void parse_data_header(TReader* r, ParquetPageHeader* h, gboolean v2) {
    gint id = 0, type = 0;
    while (tc_field(r, &id, &type)) {
        if (v2 && id == 7 && (type == TC_TRUE || type == TC_FALSE)) {
            h->is_compressed = tc_bool(type);
            continue;
        }
        if (type != TC_I32) { tc_skip(r, type); continue; }
        gint v = tc_i32(r);
        if (!v2) {
            switch (id) {
            case 1: h->num_values = v; break;
            case 2: h->encoding = v; break;
            case 3: h->def_encoding = v; break;
            case 4: h->rep_encoding = v; break;
            default: break;
            }
        } else {
            switch (id) {
            case 1: h->num_values = v; break;
            case 2: h->num_nulls = v; break;
            case 3: h->num_rows = v; break;
            case 4: h->encoding = v; break;
            case 5: h->def_length = v; break;
            case 6: h->rep_length = v; break;
            default: break;
            }
        }
    }
}

gsize parquet_page_header_parse(const guchar* data, gsize len, ParquetPageHeader* h) {
    memset(h, 0, sizeof(*h));
    h->type = -1;
    h->is_compressed = TRUE;
    h->def_encoding = h->rep_encoding = PARQUET_RLE;
    TReader r = { .p = data, .end = data + len };
    gint id = 0, type = 0;
    while (data != NULL && tc_field(&r, &id, &type)) {
        if (type == TC_I32 && id >= 1 && id <= 3) {
            gint v = tc_i32(&r);
            if (id == 1) h->type = v;
            else if (id == 2) h->uncompressed_size = v;
            else h->compressed_size = v;
        } else if (type == TC_STRUCT && (id == 5 || id == 8)) {
            parse_data_header(&r, h, id == 8);
        } else if (type == TC_STRUCT && id == 7) {
            gint did = 0, dtype = 0;
            while (tc_field(&r, &did, &dtype)) {
                if (dtype == TC_I32 && did <= 2) {
                    gint v = tc_i32(&r);
                    if (did == 1) h->num_values = v; else h->encoding = v;
                } else {
                    tc_skip(&r, dtype);
                }
            }
        } else {
            tc_skip(&r, type);
        }
    }
    if (data == NULL || r.bad || h->type < 0 || h->compressed_size < 0 || h->uncompressed_size < 0 ||
        h->num_values < 0 || h->def_length < 0 || h->rep_length < 0) {
        return 0;
    }
    return (gsize)(r.p - data);
}

/* --- Names --- */

const gchar* parquet_codec_name(gint codec) {
    static const gchar* const NAMES[] = { "UNCOMPRESSED", "SNAPPY", "GZIP", "LZO", "BROTLI", "LZ4", "ZSTD", "LZ4_RAW" };
    return codec >= 0 && codec < (gint)G_N_ELEMENTS(NAMES) ? NAMES[codec] : "?";
}

const gchar* parquet_encoding_name(gint e) {
    static const gchar* const NAMES[] = { "PLAIN", "GROUP_VAR_INT", "PLAIN_DICTIONARY", "RLE", "BIT_PACKED",
                                          "DELTA_BINARY_PACKED", "DELTA_LENGTH_BYTE_ARRAY", "DELTA_BYTE_ARRAY",
                                          "RLE_DICTIONARY", "BYTE_STREAM_SPLIT" };
    return e >= 0 && e < (gint)G_N_ELEMENTS(NAMES) ? NAMES[e] : "?";
}

static gchar* node_type(const ParquetNode* n) {
    static const gchar* const UNITS[] = { "ms", "us", "ns" };
    const gchar* unit = UNITS[n->unit >= 0 && n->unit <= 2 ? n->unit : 0];
    switch (n->logical) {
    case PARQUET_LOGICAL_STRING: return g_strdup("string");
    case PARQUET_LOGICAL_ENUM: return g_strdup("enum");
    case PARQUET_LOGICAL_JSON: return g_strdup("json");
    case PARQUET_LOGICAL_BSON: return g_strdup("bson");
    case PARQUET_LOGICAL_UUID: return g_strdup("uuid");
    case PARQUET_LOGICAL_FLOAT16: return g_strdup("float16");
    case PARQUET_LOGICAL_DATE: return g_strdup("date");
    case PARQUET_LOGICAL_VARIANT: return g_strdup("variant");
    case PARQUET_LOGICAL_GEOMETRY: return g_strdup("geometry");
    case PARQUET_LOGICAL_GEOGRAPHY: return g_strdup("geography");
    case PARQUET_LOGICAL_DECIMAL: return g_strdup_printf("decimal(%d, %d)", n->precision, n->scale);
    case PARQUET_LOGICAL_TIME: return g_strdup_printf("time[%s]", unit);
    case PARQUET_LOGICAL_TIMESTAMP:
        return n->utc ? g_strdup_printf("timestamp[%s, UTC]", unit) : g_strdup_printf("timestamp[%s]", unit);
    case PARQUET_LOGICAL_INTEGER:
        return g_strdup_printf("%sint%d", n->int_signed ? "" : "u", n->int_bits > 0 ? n->int_bits : 32);
    default: break;
    }
    switch (n->type) {
    case PARQUET_BOOLEAN: return g_strdup("bool");
    case PARQUET_INT32: return g_strdup("int32");
    case PARQUET_INT64: return g_strdup("int64");
    case PARQUET_INT96: return g_strdup("int96");
    case PARQUET_FLOAT: return g_strdup("float");
    case PARQUET_DOUBLE: return g_strdup("double");
    case PARQUET_BYTE_ARRAY: return g_strdup("binary");
    case PARQUET_FIXED_LEN_BYTE_ARRAY: return g_strdup_printf("fixed[%d]", n->type_length);
    default: return g_strdup("?");
    }
}

gchar* parquet_leaf_type(const ParquetMeta* meta, const ParquetLeaf* leaf) {
    const ParquetNode* n = &meta->nodes[leaf->node];
    gchar* type = n->type == PARQUET_INT96 ? g_strdup("timestamp[ns] (int96)") : node_type(n);
    if (leaf->max_rep > PARQUET_MAX_REP) {
        gchar* wrapped = g_strdup_printf("list<…<%s>>", type);
        g_free(type);
        type = wrapped;
    }
    for (gint k = MIN(leaf->max_rep, PARQUET_MAX_REP) - 1; k >= 0; k--) {
        gchar* wrapped = g_strdup_printf(leaf->rep_map[k] ? "map<%s>" : "list<%s>", type);
        g_free(type);
        type = wrapped;
    }
    return type;
}
