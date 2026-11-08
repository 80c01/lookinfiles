/* Java bytecode (.class) parser and preview builder. */
#include "class_info.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

#define JAVA_CLASS_MAGIC 0xCAFEBABE
#define MAX_FIELDS_SHOWN 200
#define MAX_METHODS_SHOWN 300

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

static inline gboolean br_read_u16(ByteReader* r, guint16* val) {
    if (r->pos + 2 > r->len) return FALSE;
    *val = ((guint16)r->data[r->pos] << 8) | (guint16)r->data[r->pos + 1];
    r->pos += 2;
    return TRUE;
}

static inline gboolean br_read_u32(ByteReader* r, guint32* val) {
    if (r->pos + 4 > r->len) return FALSE;
    *val = ((guint32)r->data[r->pos] << 24) |
           ((guint32)r->data[r->pos + 1] << 16) |
           ((guint32)r->data[r->pos + 2] << 8) |
           (guint32)r->data[r->pos + 3];
    r->pos += 4;
    return TRUE;
}

static inline gboolean br_skip(ByteReader* r, gsize count) {
    if (r->pos + count > r->len) return FALSE;
    r->pos += count;
    return TRUE;
}

typedef struct {
    guint8 tag;
    gchar* str;
    guint16 idx1;
    guint16 idx2;
} CpEntry;

static const gchar* cp_get_utf8(const CpEntry* cp, guint cp_count, guint16 idx) {
    if (idx == 0 || idx >= cp_count || cp[idx].tag != 1) return NULL;
    return cp[idx].str;
}

static const gchar* cp_get_class_name(const CpEntry* cp, guint cp_count, guint16 idx) {
    if (idx == 0 || idx >= cp_count || cp[idx].tag != 7) return NULL;
    return cp_get_utf8(cp, cp_count, cp[idx].idx1);
}

static gchar* format_class_name(const gchar* raw) {
    if (raw == NULL) return NULL;
    gchar* s = g_strdup(raw);
    for (gchar* p = s; *p != '\0'; p++) {
        if (*p == '/') *p = '.';
    }
    return s;
}

static const gchar* java_version_name(guint16 major) {
    switch (major) {
    case 45: return "Java 1.1";
    case 46: return "Java 1.2";
    case 47: return "Java 1.3";
    case 48: return "Java 1.4";
    case 49: return "Java 5";
    case 50: return "Java 6";
    case 51: return "Java 7";
    case 52: return "Java 8";
    case 53: return "Java 9";
    case 54: return "Java 10";
    case 55: return "Java 11 (LTS)";
    case 56: return "Java 12";
    case 57: return "Java 13";
    case 58: return "Java 14";
    case 59: return "Java 15";
    case 60: return "Java 16";
    case 61: return "Java 17 (LTS)";
    case 62: return "Java 18";
    case 63: return "Java 19";
    case 64: return "Java 20";
    case 65: return "Java 21 (LTS)";
    case 66: return "Java 22";
    case 67: return "Java 23";
    case 68: return "Java 24";
    case 69: return "Java 25 (LTS)";
    default: return NULL;
    }
}

static gchar* format_descriptor_type(const gchar** p) {
    guint dims = 0;
    while (**p == '[') {
        dims++;
        (*p)++;
    }
    GString* str = g_string_new(NULL);
    char ch = **p;
    if (ch != '\0') (*p)++;
    switch (ch) {
    case 'B': g_string_assign(str, "byte"); break;
    case 'C': g_string_assign(str, "char"); break;
    case 'D': g_string_assign(str, "double"); break;
    case 'F': g_string_assign(str, "float"); break;
    case 'I': g_string_assign(str, "int"); break;
    case 'J': g_string_assign(str, "long"); break;
    case 'S': g_string_assign(str, "short"); break;
    case 'Z': g_string_assign(str, "boolean"); break;
    case 'V': g_string_assign(str, "void"); break;
    case 'L': {
        const gchar* semi = strchr(*p, ';');
        if (semi != NULL) {
            gchar* name = g_strndup(*p, (gsize)(semi - *p));
            gchar* formatted = format_class_name(name);
            g_free(name);
            if (g_str_has_prefix(formatted, "java.lang.")) {
                g_string_assign(str, formatted + 10);
            } else {
                g_string_assign(str, formatted);
            }
            g_free(formatted);
            *p = semi + 1;
        } else {
            g_string_assign(str, "Object");
            *p += strlen(*p);
        }
        break;
    }
    default:
        g_string_assign(str, "?");
        break;
    }
    while (dims-- > 0) {
        g_string_append(str, "[]");
    }
    return g_string_free(str, FALSE);
}

static gchar* format_method_signature(const gchar* name, const gchar* desc) {
    if (desc == NULL || desc[0] != '(') {
        return g_strdup(name);
    }
    const gchar* p = desc + 1;
    GString* args = g_string_new("(");
    gboolean first = TRUE;
    while (*p != ')' && *p != '\0') {
        if (!first) g_string_append(args, ", ");
        gchar* arg_type = format_descriptor_type(&p);
        g_string_append(args, arg_type);
        g_free(arg_type);
        first = FALSE;
    }
    g_string_append_c(args, ')');
    if (*p == ')') p++;
    gchar* ret_type = format_descriptor_type(&p);
    gchar* sig = NULL;
    if (g_strcmp0(name, "<init>") == 0) {
        sig = g_strdup_printf("new %s", args->str);
    } else if (g_strcmp0(name, "<clinit>") == 0) {
        sig = g_strdup(_("static block"));
    } else {
        sig = g_strdup_printf("%s %s%s", ret_type, name, args->str);
    }
    g_free(ret_type);
    g_string_free(args, TRUE);
    return sig;
}

static void append_member_flags(GString* out, guint16 flags) {
    if (flags & 0x0001) g_string_append(out, "public ");
    if (flags & 0x0002) g_string_append(out, "private ");
    if (flags & 0x0004) g_string_append(out, "protected ");
    if (flags & 0x0008) g_string_append(out, "static ");
    if (flags & 0x0010) g_string_append(out, "final ");
    if (flags & 0x0400) g_string_append(out, "abstract ");
    if (flags & 0x0040) g_string_append(out, "volatile ");
    if (flags & 0x0080) g_string_append(out, "transient ");
}

void class_info_free(ClassInfo* info) {
    if (info == NULL) return;
    g_free(info->this_class);
    g_free(info->super_class);
    g_free(info->source_file);
    if (info->interfaces != NULL) g_ptr_array_free(info->interfaces, TRUE);
    if (info->fields != NULL) g_ptr_array_free(info->fields, TRUE);
    if (info->methods != NULL) g_ptr_array_free(info->methods, TRUE);
    g_free(info);
}

ClassInfo* class_info_parse(const guchar* data, gsize len, GError** error) {
    if (data == NULL || len < 10) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("file too small for Java class"));
        return NULL;
    }
    ByteReader r = { .data = data, .len = len, .pos = 0 };
    guint32 magic = 0;
    if (!br_read_u32(&r, &magic) || magic != JAVA_CLASS_MAGIC) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not a Java class (bad magic 0x%08X)"), magic);
        return NULL;
    }
    guint16 minor = 0, major = 0;
    if (!br_read_u16(&r, &minor) || !br_read_u16(&r, &major)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("truncated class header"));
        return NULL;
    }

    guint16 cp_count = 0;
    if (!br_read_u16(&r, &cp_count) || cp_count == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("invalid constant pool count"));
        return NULL;
    }

    CpEntry* cp = g_new0(CpEntry, cp_count);
    gboolean ok = TRUE;
    for (guint16 i = 1; i < cp_count && ok; i++) {
        guint8 tag = 0;
        if (!br_read_u8(&r, &tag)) { ok = FALSE; break; }
        cp[i].tag = tag;
        switch (tag) {
        case 1: { /* Utf8 */
            guint16 ulen = 0;
            if (!br_read_u16(&r, &ulen) || r.pos + ulen > r.len) { ok = FALSE; break; }
            cp[i].str = g_strndup((const gchar*)r.data + r.pos, ulen);
            r.pos += ulen;
            break;
        }
        case 3: /* Integer */
        case 4: /* Float */
            ok = br_skip(&r, 4);
            break;
        case 5: /* Long */
        case 6: /* Double */
            ok = br_skip(&r, 8);
            i++; /* 8-byte entries take two slots */
            break;
        case 7: /* Class */
        case 8: /* String */
        case 16: /* MethodType */
        case 19: /* Module */
        case 20: /* Package */
            ok = br_read_u16(&r, &cp[i].idx1);
            break;
        case 9:  /* Fieldref */
        case 10: /* Methodref */
        case 11: /* InterfaceMethodref */
        case 12: /* NameAndType */
        case 17: /* Dynamic */
        case 18: /* InvokeDynamic */
            ok = br_read_u16(&r, &cp[i].idx1) && br_read_u16(&r, &cp[i].idx2);
            break;
        case 15: /* MethodHandle */
            ok = br_skip(&r, 3);
            break;
        default:
            ok = FALSE;
            break;
        }
    }

    if (!ok) {
        for (guint16 i = 1; i < cp_count; i++) g_free(cp[i].str);
        g_free(cp);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("corrupted constant pool"));
        return NULL;
    }

    guint16 access_flags = 0, this_class_idx = 0, super_class_idx = 0, interfaces_count = 0;
    if (!br_read_u16(&r, &access_flags) ||
        !br_read_u16(&r, &this_class_idx) ||
        !br_read_u16(&r, &super_class_idx) ||
        !br_read_u16(&r, &interfaces_count)) {
        for (guint16 i = 1; i < cp_count; i++) g_free(cp[i].str);
        g_free(cp);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("truncated class declaration"));
        return NULL;
    }

    ClassInfo* info = g_new0(ClassInfo, 1);
    info->major = major;
    info->minor = minor;
    info->access_flags = access_flags;
    info->this_class = format_class_name(cp_get_class_name(cp, cp_count, this_class_idx));
    const gchar* raw_super = cp_get_class_name(cp, cp_count, super_class_idx);
    if (raw_super != NULL && g_strcmp0(raw_super, "java/lang/Object") != 0) {
        info->super_class = format_class_name(raw_super);
    }
    info->interfaces = g_ptr_array_new_with_free_func(g_free);
    for (guint16 i = 0; i < interfaces_count; i++) {
        guint16 iface_idx = 0;
        if (br_read_u16(&r, &iface_idx)) {
            gchar* iname = format_class_name(cp_get_class_name(cp, cp_count, iface_idx));
            if (iname != NULL) g_ptr_array_add(info->interfaces, iname);
        }
    }

    /* Fields */
    guint16 fields_count = 0;
    info->fields = g_ptr_array_new_with_free_func(g_free);
    if (br_read_u16(&r, &fields_count)) {
        for (guint16 i = 0; i < fields_count && r.pos < r.len; i++) {
            guint16 f_flags = 0, f_name = 0, f_desc = 0, f_attrs = 0;
            if (!br_read_u16(&r, &f_flags) || !br_read_u16(&r, &f_name) ||
                !br_read_u16(&r, &f_desc) || !br_read_u16(&r, &f_attrs)) break;
            const gchar* fname = cp_get_utf8(cp, cp_count, f_name);
            const gchar* fdesc = cp_get_utf8(cp, cp_count, f_desc);
            if (fname != NULL && fdesc != NULL && info->fields->len < MAX_FIELDS_SHOWN) {
                const gchar* p = fdesc;
                gchar* ftype = format_descriptor_type(&p);
                GString* s = g_string_new(NULL);
                append_member_flags(s, f_flags);
                g_string_append_printf(s, "%s %s", ftype, fname);
                g_free(ftype);
                g_ptr_array_add(info->fields, g_string_free(s, FALSE));
            }
            for (guint16 a = 0; a < f_attrs; a++) {
                guint16 aname = 0; guint32 alen = 0;
                if (!br_read_u16(&r, &aname) || !br_read_u32(&r, &alen) || !br_skip(&r, alen)) break;
            }
        }
    }

    /* Methods */
    guint16 methods_count = 0;
    info->methods = g_ptr_array_new_with_free_func(g_free);
    if (br_read_u16(&r, &methods_count)) {
        for (guint16 i = 0; i < methods_count && r.pos < r.len; i++) {
            guint16 m_flags = 0, m_name = 0, m_desc = 0, m_attrs = 0;
            if (!br_read_u16(&r, &m_flags) || !br_read_u16(&r, &m_name) ||
                !br_read_u16(&r, &m_desc) || !br_read_u16(&r, &m_attrs)) break;
            const gchar* mname = cp_get_utf8(cp, cp_count, m_name);
            const gchar* mdesc = cp_get_utf8(cp, cp_count, m_desc);
            if (mname != NULL && mdesc != NULL && info->methods->len < MAX_METHODS_SHOWN) {
                gchar* msig = format_method_signature(mname, mdesc);
                GString* s = g_string_new(NULL);
                append_member_flags(s, m_flags);
                g_string_append(s, msig);
                g_free(msig);
                g_ptr_array_add(info->methods, g_string_free(s, FALSE));
            }
            for (guint16 a = 0; a < m_attrs; a++) {
                guint16 aname = 0; guint32 alen = 0;
                if (!br_read_u16(&r, &aname) || !br_read_u32(&r, &alen) || !br_skip(&r, alen)) break;
            }
        }
    }

    /* Attributes (look for SourceFile) */
    guint16 class_attrs = 0;
    if (br_read_u16(&r, &class_attrs)) {
        for (guint16 a = 0; a < class_attrs && r.pos < r.len; a++) {
            guint16 aname = 0; guint32 alen = 0;
            if (!br_read_u16(&r, &aname) || !br_read_u32(&r, &alen)) break;
            const gchar* attr_name = cp_get_utf8(cp, cp_count, aname);
            if (g_strcmp0(attr_name, "SourceFile") == 0 && alen == 2) {
                guint16 sidx = 0;
                if (br_read_u16(&r, &sidx)) {
                    info->source_file = g_strdup(cp_get_utf8(cp, cp_count, sidx));
                }
            } else {
                if (!br_skip(&r, alen)) break;
            }
        }
    }

    for (guint16 i = 1; i < cp_count; i++) g_free(cp[i].str);
    g_free(cp);
    return info;
}

gboolean class_info_append(PreviewDoc* doc, const guchar* data, gsize len, GError** error) {
    g_autoptr(ClassInfo) info = class_info_parse(data, len, error);
    if (info == NULL) return FALSE;

    doc->monospace = TRUE;
    doc->wrap = FALSE;

    const gchar* vname = java_version_name(info->major);
    gchar* target = vname != NULL ? g_strdup_printf(_("%1$s (class %2$u.%3$u)"), vname, info->major, info->minor)
                                  : g_strdup_printf(_("class %1$u.%2$u"), info->major, info->minor);
    preview_doc_field(doc, _("Class"), info->this_class != NULL ? info->this_class : "(unnamed)");
    preview_doc_field(doc, _("Target"), target);
    g_free(target);

    if (info->super_class != NULL) {
        preview_doc_field(doc, _("Extends"), info->super_class);
    }
    if (info->interfaces != NULL && info->interfaces->len > 0) {
        GString* ifaces = g_string_new(NULL);
        for (guint i = 0; i < info->interfaces->len; i++) {
            if (i > 0) g_string_append(ifaces, ", ");
            g_string_append(ifaces, (const gchar*)g_ptr_array_index(info->interfaces, i));
        }
        preview_doc_field(doc, _("Implements"), ifaces->str);
        g_string_free(ifaces, TRUE);
    }
    if (info->source_file != NULL) {
        preview_doc_field(doc, _("Source file"), info->source_file);
    }

    if (info->fields != NULL && info->fields->len > 0) {
        gchar* title = g_strdup_printf(_("Fields (%u)"), info->fields->len);
        preview_doc_section(doc, title);
        g_free(title);
        for (guint i = 0; i < info->fields->len; i++) {
            const gchar* f = g_ptr_array_index(info->fields, i);
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, "  ");
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, f);
            preview_doc_append_len(doc, "\n", 1);
        }
    }

    if (info->methods != NULL && info->methods->len > 0) {
        gchar* title = g_strdup_printf(_("Methods (%u)"), info->methods->len);
        preview_doc_section(doc, title);
        g_free(title);
        for (guint i = 0; i < info->methods->len; i++) {
            const gchar* m = g_ptr_array_index(info->methods, i);
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, "  ");
            preview_doc_append(doc, PREVIEW_STYLE_PLAIN, m);
            preview_doc_append_len(doc, "\n", 1);
        }
    }

    return TRUE;
}
