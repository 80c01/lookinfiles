/* Python compiled bytecode (.pyc, .pyo) parser and preview builder. */
#include "pyc_info.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PYC_DEPTH 40
#define MAX_FUNCS_SHOWN 80
#define MAX_NAMES_SHOWN 60

static const gchar* magic_to_version(guint16 magic) {
    switch (magic) {
    case 62211: return "Python 2.7";
    case 3000:  return "Python 3.0";
    case 3140:
    case 3150:  return "Python 3.1 / 3.2";
    case 3180:  return "Python 3.3";
    case 3310:  return "Python 3.4";
    case 3350:
    case 3351:  return "Python 3.5";
    case 3360:
    case 3370:
    case 3379:  return "Python 3.6";
    case 3390:
    case 3394:  return "Python 3.7";
    case 3410:
    case 3413:  return "Python 3.8";
    case 3414:  return "Python 3.9";
    case 3439:  return "Python 3.10";
    case 3495:  return "Python 3.11";
    case 3531:  return "Python 3.12";
    case 3570:
    case 3571:  return "Python 3.13";
    case 3600:  return "Python 3.14";
    default:
        if (magic > 3571) return "Python 3.13+";
        if (magic >= 3390) return "Python 3.7+";
        if (magic >= 3000) return "Python 3.x";
        return "Python";
    }
}

typedef enum {
    M_OTHER,
    M_INT,
    M_STR,
    M_TUPLE,
    M_CODE
} MType;

typedef struct MValue MValue;

struct MValue {
    MType type;
    union {
        gint64 num;
        gchar* str;
        GPtrArray* tuple; /* MValue* */
        struct {
            gchar* name;
            gchar* qualname;
            gchar* filename;
            gint32 firstlineno;
            gint32 argcount;
            gint32 flags;
            MValue* consts;
            MValue* names;
            MValue* varnames;
        } code;
    } v;
};

static void mvalue_free_single(gpointer p) {
    MValue* v = (MValue*)p;
    if (v == NULL) return;
    if (v->type == M_STR) {
        g_free(v->v.str);
    } else if (v->type == M_TUPLE) {
        if (v->v.tuple != NULL) g_ptr_array_free(v->v.tuple, TRUE);
    } else if (v->type == M_CODE) {
        g_free(v->v.code.name);
        g_free(v->v.code.qualname);
        g_free(v->v.code.filename);
    }
    g_free(v);
}

typedef struct {
    const guint8* data;
    gsize len;
    gsize pos;
    guint16 magic;
    GPtrArray* pool;      /* owns all MValue* */
    GPtrArray* ref_table; /* borrowed MValue* */
} PycReader;

static gint32 r_int32(PycReader* r) {
    if (r->pos + 4 > r->len) return 0;
    guint32 b0 = r->data[r->pos];
    guint32 b1 = r->data[r->pos + 1];
    guint32 b2 = r->data[r->pos + 2];
    guint32 b3 = r->data[r->pos + 3];
    r->pos += 4;
    return (gint32)(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
}

static MValue* alloc_val(PycReader* r, MType type) {
    MValue* v = g_new0(MValue, 1);
    v->type = type;
    g_ptr_array_add(r->pool, v);
    return v;
}

static MValue* r_object(PycReader* r, guint depth);

static MValue* r_tuple(PycReader* r, guint count, guint depth) {
    MValue* v = alloc_val(r, M_TUPLE);
    v->v.tuple = g_ptr_array_new();
    for (guint i = 0; i < count; i++) {
        MValue* item = r_object(r, depth + 1);
        g_ptr_array_add(v->v.tuple, item);
    }
    return v;
}

static gchar* val_to_str(MValue* v) {
    if (v != NULL && v->type == M_STR && v->v.str != NULL) {
        return g_strdup(v->v.str);
    }
    return NULL;
}

static MValue* r_code(PycReader* r, guint depth) {
    MValue* v = alloc_val(r, M_CODE);

    if (r->magic >= 3495) { /* Python 3.11+ */
        v->v.code.argcount = r_int32(r);
        (void)r_int32(r); /* posonly */
        (void)r_int32(r); /* kwonly */
        (void)r_int32(r); /* stacksize */
        v->v.code.flags = r_int32(r);
        (void)r_object(r, depth + 1); /* code */
        v->v.code.consts = r_object(r, depth + 1);
        v->v.code.names = r_object(r, depth + 1);
        v->v.code.varnames = r_object(r, depth + 1); /* localsplusnames */
        (void)r_object(r, depth + 1);                /* localspluskinds */
        MValue* fn = r_object(r, depth + 1);
        v->v.code.filename = val_to_str(fn);
        MValue* nm = r_object(r, depth + 1);
        v->v.code.name = val_to_str(nm);
        MValue* qn = r_object(r, depth + 1);
        v->v.code.qualname = val_to_str(qn);
        v->v.code.firstlineno = r_int32(r);
        (void)r_object(r, depth + 1); /* linetable */
        (void)r_object(r, depth + 1); /* exceptiontable */
    } else if (r->magic >= 3410) {    /* Python 3.8 - 3.10 */
        v->v.code.argcount = r_int32(r);
        (void)r_int32(r); /* posonly */
        (void)r_int32(r); /* kwonly */
        (void)r_int32(r); /* nlocals */
        (void)r_int32(r); /* stacksize */
        v->v.code.flags = r_int32(r);
        (void)r_object(r, depth + 1); /* code */
        v->v.code.consts = r_object(r, depth + 1);
        v->v.code.names = r_object(r, depth + 1);
        v->v.code.varnames = r_object(r, depth + 1);
        (void)r_object(r, depth + 1); /* freevars */
        (void)r_object(r, depth + 1); /* cellvars */
        MValue* fn = r_object(r, depth + 1);
        v->v.code.filename = val_to_str(fn);
        MValue* nm = r_object(r, depth + 1);
        v->v.code.name = val_to_str(nm);
        v->v.code.firstlineno = r_int32(r);
        (void)r_object(r, depth + 1); /* lnotab */
    } else {                          /* < 3.8 */
        v->v.code.argcount = r_int32(r);
        if (r->magic >= 3000) (void)r_int32(r); /* kwonly */
        (void)r_int32(r);                       /* nlocals */
        (void)r_int32(r);                       /* stacksize */
        v->v.code.flags = r_int32(r);
        (void)r_object(r, depth + 1); /* code */
        v->v.code.consts = r_object(r, depth + 1);
        v->v.code.names = r_object(r, depth + 1);
        v->v.code.varnames = r_object(r, depth + 1);
        (void)r_object(r, depth + 1); /* freevars */
        (void)r_object(r, depth + 1); /* cellvars */
        MValue* fn = r_object(r, depth + 1);
        v->v.code.filename = val_to_str(fn);
        MValue* nm = r_object(r, depth + 1);
        v->v.code.name = val_to_str(nm);
        v->v.code.firstlineno = r_int32(r);
        (void)r_object(r, depth + 1); /* lnotab */
    }

    return v;
}

static MValue* r_object(PycReader* r, guint depth) {
    if (r->pos >= r->len || depth > MAX_PYC_DEPTH) return NULL;
    guint8 raw_tag = r->data[r->pos++];
    gboolean is_ref = (raw_tag & 0x80) != 0;
    guint8 tag = raw_tag & 0x7F;

    guint ref_idx = G_MAXUINT;
    if (is_ref) {
        ref_idx = r->ref_table->len;
        g_ptr_array_add(r->ref_table, NULL);
    }

    MValue* res = NULL;

    switch (tag) {
    case 'N': /* None */
    case 'F': /* False */
    case 'T': /* True */
    case 'S': /* StopIteration */
    case '.': /* Ellipsis */
    case '0': /* NULL */
        res = alloc_val(r, M_OTHER);
        break;

    case 'i': { /* int32 */
        gint32 val = r_int32(r);
        res = alloc_val(r, M_INT);
        res->v.num = val;
        break;
    }

    case 'I': { /* int64 */
        if (r->pos + 8 <= r->len) r->pos += 8;
        res = alloc_val(r, M_OTHER);
        break;
    }

    case 'f': { /* float */
        if (r->pos < r->len) {
            guint flen = r->data[r->pos++];
            if (r->pos + flen <= r->len) r->pos += flen;
        }
        res = alloc_val(r, M_OTHER);
        break;
    }

    case 'g': /* binary float (double) */
        if (r->pos + 8 <= r->len) r->pos += 8;
        res = alloc_val(r, M_OTHER);
        break;

    case 'x': { /* complex */
        if (r->pos < r->len) {
            guint flen = r->data[r->pos++];
            if (r->pos + flen <= r->len) r->pos += flen;
        }
        if (r->pos < r->len) {
            guint flen = r->data[r->pos++];
            if (r->pos + flen <= r->len) r->pos += flen;
        }
        res = alloc_val(r, M_OTHER);
        break;
    }

    case 'y': /* binary complex */
        if (r->pos + 16 <= r->len) r->pos += 16;
        res = alloc_val(r, M_OTHER);
        break;

    case 'l': { /* long */
        gint32 n = r_int32(r);
        gsize bytes = (gsize)abs(n) * 2;
        if (r->pos + bytes <= r->len) r->pos += bytes;
        res = alloc_val(r, M_OTHER);
        break;
    }

    case 's': /* string / bytes */
    case 'u': /* unicode */
    case 'a': /* ascii */
    case 'A': /* interned ascii */
    case 't': { /* interned string */
        guint32 slen = (guint32)r_int32(r);
        if (r->pos + slen <= r->len) {
            gchar* raw = g_strndup((const gchar*)r->data + r->pos, slen);
            r->pos += slen;
            res = alloc_val(r, M_STR);
            res->v.str = g_utf8_make_valid(raw, (gssize)slen);
            g_free(raw);
        } else {
            res = alloc_val(r, M_OTHER);
        }
        break;
    }

    case 'z':   /* short ascii */
    case 'Z': { /* short ascii interned */
        if (r->pos < r->len) {
            guint slen = r->data[r->pos++];
            if (r->pos + slen <= r->len) {
                gchar* raw = g_strndup((const gchar*)r->data + r->pos, slen);
                r->pos += slen;
                res = alloc_val(r, M_STR);
                res->v.str = g_utf8_make_valid(raw, (gssize)slen);
                g_free(raw);
            } else {
                res = alloc_val(r, M_OTHER);
            }
        }
        break;
    }

    case 'r': { /* ref */
        guint32 ridx = (guint32)r_int32(r);
        if (ridx < r->ref_table->len) {
            res = (MValue*)g_ptr_array_index(r->ref_table, ridx);
        }
        break;
    }

    case 'R': { /* string ref */
        (void)r_int32(r);
        res = alloc_val(r, M_OTHER);
        break;
    }

    case '(': /* tuple */
    case '[': /* list */
    case '<': /* set */
    case '>': { /* frozenset */
        guint32 count = (guint32)r_int32(r);
        if (is_ref && ref_idx < r->ref_table->len) {
            res = r_tuple(r, count, depth);
            g_ptr_array_index(r->ref_table, ref_idx) = res;
        } else {
            res = r_tuple(r, count, depth);
        }
        break;
    }

    case ')': { /* small tuple */
        if (r->pos < r->len) {
            guint count = r->data[r->pos++];
            if (is_ref && ref_idx < r->ref_table->len) {
                res = r_tuple(r, count, depth);
                g_ptr_array_index(r->ref_table, ref_idx) = res;
            } else {
                res = r_tuple(r, count, depth);
            }
        }
        break;
    }

    case '{': { /* dict */
        res = alloc_val(r, M_OTHER);
        if (is_ref && ref_idx < r->ref_table->len) {
            g_ptr_array_index(r->ref_table, ref_idx) = res;
        }
        while (r->pos < r->len) {
            MValue* k = r_object(r, depth + 1);
            if (k == NULL || r->pos >= r->len) break;
            (void)r_object(r, depth + 1);
        }
        break;
    }

    case 'c': { /* code object */
        if (is_ref && ref_idx < r->ref_table->len) {
            res = r_code(r, depth);
            g_ptr_array_index(r->ref_table, ref_idx) = res;
        } else {
            res = r_code(r, depth);
        }
        break;
    }

    default:
        res = alloc_val(r, M_OTHER);
        break;
    }

    if (is_ref && ref_idx < r->ref_table->len && g_ptr_array_index(r->ref_table, ref_idx) == NULL) {
        g_ptr_array_index(r->ref_table, ref_idx) = res;
    }

    return res;
}

static void pyc_collect_code(MValue* code_val, GPtrArray* funcs) {
    if (code_val == NULL || code_val->type != M_CODE || code_val->v.code.consts == NULL) return;
    MValue* consts = code_val->v.code.consts;
    if (consts->type != M_TUPLE || consts->v.tuple == NULL) return;

    for (guint i = 0; i < consts->v.tuple->len && funcs->len < MAX_FUNCS_SHOWN; i++) {
        MValue* item = (MValue*)g_ptr_array_index(consts->v.tuple, i);
        if (item != NULL && item->type == M_CODE) {
            PycFunction* fn = g_new0(PycFunction, 1);
            fn->name = g_strdup(item->v.code.name != NULL ? item->v.code.name : "<unnamed>");
            fn->qualname = g_strdup(item->v.code.qualname);
            fn->firstlineno = (guint32)(item->v.code.firstlineno > 0 ? item->v.code.firstlineno : 0);
            fn->argcount = (guint32)(item->v.code.argcount > 0 ? item->v.code.argcount : 0);
            fn->flags = (guint32)item->v.code.flags;
            g_ptr_array_add(funcs, fn);

            /* Recurse for nested functions / methods */
            pyc_collect_code(item, funcs);
        }
    }
}

static void pyc_collect_strings(MValue* tuple_val, GPtrArray* out, guint max_count) {
    if (tuple_val == NULL || tuple_val->type != M_TUPLE || tuple_val->v.tuple == NULL) return;
    for (guint i = 0; i < tuple_val->v.tuple->len && out->len < max_count; i++) {
        MValue* item = (MValue*)g_ptr_array_index(tuple_val->v.tuple, i);
        if (item != NULL && item->type == M_STR && item->v.str != NULL && item->v.str[0] != '\0') {
            gboolean found = FALSE;
            for (guint j = 0; j < out->len; j++) {
                if (g_strcmp0(g_ptr_array_index(out, j), item->v.str) == 0) {
                    found = TRUE;
                    break;
                }
            }
            if (!found) g_ptr_array_add(out, g_strdup(item->v.str));
        }
    }
}

void pyc_info_free(PycInfo* info) {
    if (info == NULL) return;
    g_free(info->hash_hex);
    g_free(info->filename);
    g_free(info->main_name);
    if (info->functions != NULL) {
        for (guint i = 0; i < info->functions->len; i++) {
            PycFunction* fn = (PycFunction*)g_ptr_array_index(info->functions, i);
            g_free(fn->name);
            g_free(fn->qualname);
            g_free(fn);
        }
        g_ptr_array_free(info->functions, TRUE);
    }
    if (info->names != NULL) g_ptr_array_free(info->names, TRUE);
    if (info->varnames != NULL) g_ptr_array_free(info->varnames, TRUE);
    g_free(info);
}

PycInfo* pyc_info_parse(const guchar* data, gsize len, GError** error) {
    if (data == NULL || len < 16) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("file too small for Python bytecode"));
        return NULL;
    }
    if (data[2] != '\r' || data[3] != '\n') {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("invalid .pyc magic delimiter"));
        return NULL;
    }

    guint16 magic = (guint16)(data[0] | (data[1] << 8));
    guint pos = 16;
    guint32 flags = 0;
    gboolean hash_based = FALSE;
    gboolean check_hash = FALSE;
    gint64 source_mtime = 0;
    guint32 source_size = 0;
    gchar* hash_hex = NULL;

    if (magic >= 3390) { /* Python 3.7+ (PEP 552) */
        flags = (guint32)(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
        hash_based = (flags & 0x01) != 0;
        check_hash = (flags & 0x02) != 0;
        if (hash_based) {
            hash_hex = g_strdup_printf("%02x%02x%02x%02x%02x%02x%02x%02x",
                                       data[8], data[9], data[10], data[11],
                                       data[12], data[13], data[14], data[15]);
        } else {
            source_mtime = (gint64)(data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24));
            source_size = (guint32)(data[12] | (data[13] << 8) | (data[14] << 16) | (data[15] << 24));
        }
        pos = 16;
    } else if (magic >= 3180) { /* Python 3.3 - 3.6 */
        source_mtime = (gint64)(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
        source_size = (guint32)(data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24));
        pos = 12;
    } else { /* < 3.3 */
        source_mtime = (gint64)(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
        pos = 8;
    }

    PycReader reader = {
        .data = data,
        .len = len,
        .pos = pos,
        .magic = magic,
        .pool = g_ptr_array_new_with_free_func(mvalue_free_single),
        .ref_table = g_ptr_array_new()
    };

    MValue* root = r_object(&reader, 0);

    PycInfo* info = g_new0(PycInfo, 1);
    info->magic = magic;
    info->python_version = magic_to_version(magic);
    info->flags = flags;
    info->hash_based = hash_based;
    info->check_hash = check_hash;
    info->source_mtime = source_mtime;
    info->source_size = source_size;
    info->hash_hex = hash_hex;
    info->functions = g_ptr_array_new();
    info->names = g_ptr_array_new_with_free_func(g_free);
    info->varnames = g_ptr_array_new_with_free_func(g_free);

    if (root != NULL && root->type == M_CODE) {
        info->filename = g_strdup(root->v.code.filename);
        info->main_name = g_strdup(root->v.code.name != NULL ? root->v.code.name : "<module>");
        info->main_firstlineno = (guint32)(root->v.code.firstlineno > 0 ? root->v.code.firstlineno : 1);
        info->main_argcount = (guint32)(root->v.code.argcount > 0 ? root->v.code.argcount : 0);
        info->main_flags = (guint32)root->v.code.flags;

        pyc_collect_code(root, info->functions);
        pyc_collect_strings(root->v.code.names, info->names, MAX_NAMES_SHOWN);
        pyc_collect_strings(root->v.code.varnames, info->varnames, MAX_NAMES_SHOWN);
    }

    g_ptr_array_free(reader.ref_table, TRUE);
    g_ptr_array_free(reader.pool, TRUE);

    return info;
}

gboolean pyc_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error) {
    g_autoptr(PycInfo) info = pyc_info_parse(data, len, error);
    if (info == NULL) return FALSE;

    doc->monospace = TRUE;
    doc->wrap = FALSE;

    gchar* ver_str = g_strdup_printf(_("%1$s (magic %2$u)"), info->python_version, info->magic);
    preview_doc_field(doc, _("Version"), ver_str);
    g_free(ver_str);

    if (info->filename != NULL) {
        preview_doc_field(doc, _("Source file"), info->filename);
    }

    if (info->hash_based) {
        gchar* h_str = g_strdup_printf("%s (%s)", info->hash_hex != NULL ? info->hash_hex : "",
                                       info->check_hash ? "checked" : "unchecked");
        preview_doc_field(doc, _("Hash (PEP 552)"), h_str);
        g_free(h_str);
    } else {
        if (info->source_size > 0) {
            gchar* sz_str = g_format_size(info->source_size);
            preview_doc_field(doc, _("Source size"), sz_str);
            g_free(sz_str);
        }
        if (info->source_mtime > 0) {
            GDateTime* dt = g_date_time_new_from_unix_local(info->source_mtime);
            if (dt != NULL) {
                gchar* d_str = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
                preview_doc_field(doc, _("Source modified"), d_str);
                g_free(d_str);
                g_date_time_unref(dt);
            }
        }
    }

    if (info->main_name != NULL) {
        gchar* main_str = g_strdup_printf(_("%1$s (starts line %2$u)"), info->main_name, info->main_firstlineno);
        preview_doc_field(doc, _("Entry point"), main_str);
        g_free(main_str);
    }

    if (info->functions != NULL && info->functions->len > 0) {
        preview_doc_append_len(doc, "\n", 1);
        preview_doc_section(doc, _("Functions and Classes"));

        g_autoptr(PreviewTable) table = preview_table_new(3);
        const gchar* header[] = { _("Name"), _("Line"), _("Args") };
        preview_table_set_header(table, header);
        preview_table_set_right_aligned(table, 1);
        preview_table_set_right_aligned(table, 2);
        preview_table_set_max_cell_width(table, 60);

        for (guint i = 0; i < info->functions->len; i++) {
            const PycFunction* fn = (PycFunction*)g_ptr_array_index(info->functions, i);
            gchar* line_str = g_strdup_printf("%u", fn->firstlineno);
            gchar* arg_str = g_strdup_printf("%u", fn->argcount);
            const gchar* row[] = { fn->qualname != NULL ? fn->qualname : fn->name, line_str, arg_str };
            preview_table_add_row(table, row, 3);
            g_free(line_str);
            g_free(arg_str);
        }
        preview_doc_add_table(doc, table);
    }

    if (info->names != NULL && info->names->len > 0) {
        preview_doc_append_len(doc, "\n", 1);
        preview_doc_section(doc, _("Referenced Symbols"));
        GString* line = g_string_new("  ");
        for (guint i = 0; i < info->names->len; i++) {
            const gchar* sym = (const gchar*)g_ptr_array_index(info->names, i);
            if (line->len > 2) g_string_append(line, ", ");
            if (line->len + strlen(sym) > 80) {
                preview_doc_append(doc, PREVIEW_STYLE_PLAIN, line->str);
                preview_doc_append_len(doc, "\n", 1);
                g_string_assign(line, "  ");
            }
            g_string_append(line, sym);
        }
        if (line->len > 2) {
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, line->str);
            preview_doc_append_len(doc, "\n", 1);
        }
        g_string_free(line, TRUE);
    }

    return TRUE;
}
