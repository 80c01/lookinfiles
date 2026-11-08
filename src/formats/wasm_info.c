/* WebAssembly (.wasm) binary parser and preview builder. */
#include "formats/wasm_info.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

#define WASM_MAGIC "\0asm"
#define MAX_WASM_IMPORTS 100
#define MAX_WASM_EXPORTS 150

typedef struct {
    const guint8* data;
    gsize len;
    gsize pos;
} ByteReader;

static inline gboolean br_read_u8(ByteReader* r, guint8* val) {
    if (r->pos >= r->len) return FALSE;
    *val = r->data[r->pos++];
    return TRUE;
}

static inline gboolean br_read_u32_le(ByteReader* r, guint32* val) {
    if (r->pos + 4 > r->len) return FALSE;
    *val = (guint32)r->data[r->pos] |
           ((guint32)r->data[r->pos + 1] << 8) |
           ((guint32)r->data[r->pos + 2] << 16) |
           ((guint32)r->data[r->pos + 3] << 24);
    r->pos += 4;
    return TRUE;
}

static inline gboolean br_read_leb128(ByteReader* r, guint64* val) {
    guint64 result = 0;
    guint shift = 0;
    while (r->pos < r->len) {
        guint8 byte = r->data[r->pos++];
        result |= (guint64)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            *val = result;
            return TRUE;
        }
        shift += 7;
        if (shift >= 64) return FALSE;
    }
    return FALSE;
}

static inline gboolean br_read_leb128_u32(ByteReader* r, guint32* val) {
    guint64 v = 0;
    if (!br_read_leb128(r, &v) || v > G_MAXUINT32) return FALSE;
    *val = (guint32)v;
    return TRUE;
}

static inline gboolean br_skip(ByteReader* r, gsize count) {
    if (r->pos + count > r->len) return FALSE;
    r->pos += count;
    return TRUE;
}

static gchar* br_read_wasm_string(ByteReader* r) {
    guint32 slen = 0;
    if (!br_read_leb128_u32(r, &slen) || r->pos + slen > r->len) return NULL;
    gchar* s = g_strndup((const gchar*)r->data + r->pos, slen);
    r->pos += slen;
    return s;
}

void wasm_info_free(WasmInfo* info) {
    if (info == NULL) return;
    if (info->imports != NULL) g_ptr_array_free(info->imports, TRUE);
    if (info->exports != NULL) g_ptr_array_free(info->exports, TRUE);
    g_free(info);
}

static void parse_import_section(ByteReader* r, gsize end_pos, WasmInfo* info) {
    guint32 count = 0;
    if (!br_read_leb128_u32(r, &count)) return;
    for (guint32 i = 0; i < count && r->pos < end_pos; i++) {
        gchar* mod = br_read_wasm_string(r);
        gchar* name = br_read_wasm_string(r);
        guint8 kind = 0;
        if (mod == NULL || name == NULL || !br_read_u8(r, &kind)) {
            g_free(mod);
            g_free(name);
            break;
        }
        const gchar* kind_name = "unknown";
        switch (kind) {
        case 0: { /* func */
            guint32 typeidx = 0;
            br_read_leb128_u32(r, &typeidx);
            info->n_imported_funcs++;
            kind_name = "func";
            break;
        }
        case 1: { /* table */
            guint8 elemtype = 0; guint8 flags = 0; guint32 initial = 0;
            br_read_u8(r, &elemtype);
            br_read_u8(r, &flags);
            br_read_leb128_u32(r, &initial);
            if (flags & 1) { guint32 max = 0; br_read_leb128_u32(r, &max); }
            info->n_imported_tables++;
            kind_name = "table";
            break;
        }
        case 2: { /* mem */
            guint8 flags = 0; guint32 initial = 0;
            br_read_u8(r, &flags);
            br_read_leb128_u32(r, &initial);
            if (flags & 1) { guint32 max = 0; br_read_leb128_u32(r, &max); }
            info->n_imported_mems++;
            kind_name = "memory";
            break;
        }
        case 3: { /* global */
            guint8 valtype = 0, mut = 0;
            br_read_u8(r, &valtype);
            br_read_u8(r, &mut);
            info->n_imported_globals++;
            kind_name = "global";
            break;
        }
        }
        if (info->imports->len < MAX_WASM_IMPORTS) {
            gchar* entry = g_strdup_printf("%s.%s (%s)", mod, name, kind_name);
            g_ptr_array_add(info->imports, entry);
        }
        g_free(mod);
        g_free(name);
    }
}

static void parse_export_section(ByteReader* r, gsize end_pos, WasmInfo* info) {
    guint32 count = 0;
    if (!br_read_leb128_u32(r, &count)) return;
    info->n_exports = count;
    for (guint32 i = 0; i < count && r->pos < end_pos; i++) {
        gchar* name = br_read_wasm_string(r);
        guint8 kind = 0;
        guint32 idx = 0;
        if (name == NULL || !br_read_u8(r, &kind) || !br_read_leb128_u32(r, &idx)) {
            g_free(name);
            break;
        }
        const gchar* kind_desc = "unknown";
        switch (kind) {
        case 0: kind_desc = "func"; break;
        case 1: kind_desc = "table"; break;
        case 2: kind_desc = "memory"; break;
        case 3: kind_desc = "global"; break;
        }
        if (info->exports->len < MAX_WASM_EXPORTS) {
            gchar* entry = (kind == 0) ? g_strdup_printf(_("%1$s (func #%2$u)"), name, idx)
                                       : g_strdup_printf("%s (%s)", name, kind_desc);
            g_ptr_array_add(info->exports, entry);
        }
        g_free(name);
    }
}

static void parse_memory_section(ByteReader* r, WasmInfo* info) {
    guint32 count = 0;
    if (!br_read_leb128_u32(r, &count)) return;
    info->n_mems = count;
    for (guint32 i = 0; i < count; i++) {
        guint8 flags = 0; guint32 min_pages = 0;
        if (!br_read_u8(r, &flags) || !br_read_leb128_u32(r, &min_pages)) break;
        if (i == 0) {
            info->mem_initial_pages = min_pages;
        }
        if (flags & 1) {
            guint32 max_pages = 0;
            if (br_read_leb128_u32(r, &max_pages) && i == 0) {
                info->mem_max_pages = max_pages;
                info->has_mem_max = TRUE;
            }
        }
    }
}

WasmInfo* wasm_info_parse(const guchar* data, gsize len, GError** error) {
    if (data == NULL || len < 8) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("file too small for WebAssembly binary"));
        return NULL;
    }
    if (memcmp(data, WASM_MAGIC, 4) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not a WebAssembly binary (bad magic)"));
        return NULL;
    }
    ByteReader r = { .data = data, .len = len, .pos = 4 };
    guint32 version = 0;
    if (!br_read_u32_le(&r, &version)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("truncated version header"));
        return NULL;
    }

    WasmInfo* info = g_new0(WasmInfo, 1);
    info->version = version;
    info->imports = g_ptr_array_new_with_free_func(g_free);
    info->exports = g_ptr_array_new_with_free_func(g_free);

    while (r.pos < r.len) {
        guint8 sec_id = 0;
        guint32 sec_len = 0;
        if (!br_read_u8(&r, &sec_id) || !br_read_leb128_u32(&r, &sec_len)) break;
        gsize sec_end = r.pos + sec_len;
        if (sec_end > r.len) sec_end = r.len;

        switch (sec_id) {
        case 1: { /* Type */
            guint32 count = 0;
            if (br_read_leb128_u32(&r, &count)) info->n_types = count;
            break;
        }
        case 2: /* Import */
            parse_import_section(&r, sec_end, info);
            break;
        case 3: { /* Function */
            guint32 count = 0;
            if (br_read_leb128_u32(&r, &count)) info->n_defined_funcs = count;
            break;
        }
        case 4: { /* Table */
            guint32 count = 0;
            if (br_read_leb128_u32(&r, &count)) info->n_tables = count;
            break;
        }
        case 5: /* Memory */
            parse_memory_section(&r, info);
            break;
        case 6: { /* Global */
            guint32 count = 0;
            if (br_read_leb128_u32(&r, &count)) info->n_globals = count;
            break;
        }
        case 7: /* Export */
            parse_export_section(&r, sec_end, info);
            break;
        case 11: { /* Data */
            guint32 count = 0;
            if (br_read_leb128_u32(&r, &count)) info->n_data_segments = count;
            break;
        }
        default:
            break;
        }
        r.pos = sec_end;
    }

    return info;
}

gboolean wasm_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error) {
    g_autoptr(WasmInfo) info = wasm_info_parse(data, len, error);
    if (info == NULL) return FALSE;

    doc->monospace = TRUE;
    doc->wrap = FALSE;

    gchar* v_str = g_strdup_printf(_("Version %u"), info->version);
    preview_doc_field(doc, _("Format"), v_str);
    g_free(v_str);

    guint total_funcs = info->n_imported_funcs + info->n_defined_funcs;
    gchar* f_str = g_strdup_printf(_("%1$u (%2$u defined, %3$u imported)"), total_funcs,
                                   info->n_defined_funcs, info->n_imported_funcs);
    preview_doc_field(doc, _("Functions"), f_str);
    g_free(f_str);

    if (info->n_types > 0) {
        gchar* t_str = g_strdup_printf("%u", info->n_types);
        preview_doc_field(doc, _("Types"), t_str);
        g_free(t_str);
    }

    guint total_mems = info->n_mems + info->n_imported_mems;
    if (total_mems > 0) {
        guint64 kb = info->mem_initial_pages * 64;
        gchar* pages = g_strdup_printf("%" G_GUINT64_FORMAT, info->mem_initial_pages);
        gchar* kbs = g_strdup_printf("%" G_GUINT64_FORMAT, kb);
        gchar* max = g_strdup_printf("%" G_GUINT64_FORMAT, info->mem_max_pages);
        gchar* m_str = info->has_mem_max ? g_strdup_printf(_("%1$s pages (%2$s KB), max %3$s pages"), pages, kbs, max)
                                         : g_strdup_printf(_("%1$s pages (%2$s KB), unbounded"), pages, kbs);
        g_free(max);
        g_free(kbs);
        g_free(pages);
        preview_doc_field(doc, _("Memory"), m_str);
        g_free(m_str);
    }

    if (info->n_tables + info->n_imported_tables > 0) {
        gchar* tbl_str = g_strdup_printf("%u", info->n_tables + info->n_imported_tables);
        preview_doc_field(doc, _("Tables"), tbl_str);
        g_free(tbl_str);
    }

    if (info->n_globals + info->n_imported_globals > 0) {
        gchar* g_str = g_strdup_printf("%u", info->n_globals + info->n_imported_globals);
        preview_doc_field(doc, _("Globals"), g_str);
        g_free(g_str);
    }

    if (info->n_data_segments > 0) {
        gchar* d_str = g_strdup_printf(ngettext("%u segment", "%u segments", info->n_data_segments), info->n_data_segments);
        preview_doc_field(doc, _("Data"), d_str);
        g_free(d_str);
    }

    if (info->imports != NULL && info->imports->len > 0) {
        gchar* title = g_strdup_printf(_("Imports (%u)"), info->imports->len);
        preview_doc_section(doc, title);
        g_free(title);
        for (guint i = 0; i < info->imports->len; i++) {
            const gchar* imp = g_ptr_array_index(info->imports, i);
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, "  • ");
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, imp);
            preview_doc_append_len(doc, "\n", 1);
        }
    }

    if (info->exports != NULL && info->exports->len > 0) {
        gchar* title = g_strdup_printf(_("Exports (%u)"), info->exports->len);
        preview_doc_section(doc, title);
        g_free(title);
        for (guint i = 0; i < info->exports->len; i++) {
            const gchar* exp = g_ptr_array_index(info->exports, i);
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, "  • ");
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, exp);
            preview_doc_append_len(doc, "\n", 1);
        }
    }

    return TRUE;
}
