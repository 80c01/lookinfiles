/* ELF header reader for the preview of executables and libraries */
#include "elf_info.h"

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

/* Constants of the ELF format (from <elf.h>, kept here to stay portable) */
enum {
    ET_REL = 1, ET_EXEC = 2, ET_DYN = 3, ET_CORE = 4,
    PT_LOAD = 1, PT_DYNAMIC = 2, PT_INTERP = 3, PT_NOTE = 4,
    SHT_SYMTAB = 2, SHT_STRTAB = 3, SHT_DYNAMIC = 6, SHT_NOTE = 7, SHT_DYNSYM = 11,
    DT_NULL = 0, DT_NEEDED = 1, DT_STRTAB = 5, DT_STRSZ = 10, DT_SONAME = 14,
    DT_RPATH = 15, DT_RUNPATH = 29,
    NT_GNU_BUILD_ID = 3,
    STB_GLOBAL = 1, STB_WEAK = 2, STB_GNU_UNIQUE = 10,
    STT_OBJECT = 1, STT_FUNC = 2, STT_TLS = 6, STT_GNU_IFUNC = 10,
    STV_HIDDEN = 2, STV_INTERNAL = 1,
    SHN_UNDEF = 0, SHN_ABS = 0xfff1, SHN_XINDEX = 0xffff,
};

/* Limits against damaged files: tables larger than this are skipped */
#define MAX_TABLE_BYTES   (16u * 1024 * 1024)
#define MAX_PHNUM         4096
#define MAX_SHNUM         65536
#define MAX_DYN_ENTRIES   65536

typedef struct {
    ElfReadFunc read;
    gpointer ctx;
    gboolean is_64;
    gboolean big_endian;
} Reader;

typedef struct {
    guint32 type;
    guint64 offset, vaddr, filesz;
} Segment;

typedef struct {
    guint32 name, type, link;
    guint64 offset, size, entsize;
} Section;

static guint64 get_uint(const Reader* r, const guchar* p, gsize n) {
    guint64 v = 0;
    for (gsize i = 0; i < n; i++) {
        gsize k = r->big_endian ? i : n - 1 - i;
        v = (v << 8) | p[k];
    }
    return v;
}

#define U16(p) ((guint16)get_uint(r, (p), 2))
#define U32(p) ((guint32)get_uint(r, (p), 4))
#define U64(p) get_uint(r, (p), 8)
/* Address-sized field: 4 bytes in ELF32, 8 in ELF64 */
#define UADDR(p) get_uint(r, (p), r->is_64 ? 8 : 4)

/* Read a block of up to MAX_TABLE_BYTES; NULL if out of range */
static guchar* read_block(const Reader* r, guint64 offset, guint64 size) {
    if (size == 0 || size > MAX_TABLE_BYTES) {
        return NULL;
    }
    guchar* buf = g_malloc((gsize)size + 1);
    if (!r->read(r->ctx, offset, buf, (gsize)size)) {
        g_free(buf);
        return NULL;
    }
    buf[size] = '\0'; /* string tables always end with a NUL */
    return buf;
}

/* Copy of the NUL-terminated string at off in a table, as valid UTF-8 */
static gchar* table_string(const guchar* table, guint64 size, guint64 off) {
    if (table == NULL || off >= size) {
        return NULL;
    }
    const gchar* s = (const gchar*)table + off;
    gsize len = strnlen(s, (gsize)(size - off));
    return g_utf8_make_valid(s, (gssize)len);
}

static gboolean read_segments(const Reader* r, guint64 phoff, guint phnum, guint phentsize,
                              GArray* out) {
    gsize min_size = r->is_64 ? 56 : 32;
    if (phoff == 0 || phnum == 0 || phnum > MAX_PHNUM || phentsize < min_size) {
        return FALSE;
    }
    guchar* tab = read_block(r, phoff, (guint64)phnum * phentsize);
    if (tab == NULL) {
        return FALSE;
    }
    for (guint i = 0; i < phnum; i++) {
        const guchar* p = tab + (gsize)i * phentsize;
        Segment s = { .type = U32(p) };
        if (r->is_64) {
            s.offset = U64(p + 8);
            s.vaddr = U64(p + 16);
            s.filesz = U64(p + 32);
        } else {
            s.offset = U32(p + 4);
            s.vaddr = U32(p + 8);
            s.filesz = U32(p + 16);
        }
        g_array_append_val(out, s);
    }
    g_free(tab);
    return TRUE;
}

static Section parse_section(const Reader* r, const guchar* p) {
    Section s = { .name = U32(p), .type = U32(p + 4) };
    if (r->is_64) {
        s.offset = U64(p + 24);
        s.size = U64(p + 32);
        s.link = U32(p + 40);
        s.entsize = U64(p + 56);
    } else {
        s.offset = U32(p + 16);
        s.size = U32(p + 20);
        s.link = U32(p + 24);
        s.entsize = U32(p + 36);
    }
    return s;
}

/* Section headers; *shstrndx may come from section 0 (extended numbering) */
static gboolean read_sections(const Reader* r, guint64 shoff, guint shnum, guint shentsize,
                              guint* shstrndx, GArray* out) {
    gsize min_size = r->is_64 ? 64 : 40;
    if (shoff == 0 || shentsize < min_size) {
        return FALSE;
    }
    if (shnum == 0 || *shstrndx == SHN_XINDEX) {
        guchar* first = read_block(r, shoff, shentsize);
        if (first == NULL) {
            return FALSE;
        }
        Section s0 = parse_section(r, first);
        g_free(first);
        if (shnum == 0) {
            shnum = (guint)MIN(s0.size, (guint64)G_MAXUINT);
        }
        if (*shstrndx == SHN_XINDEX) {
            *shstrndx = s0.link;
        }
    }
    if (shnum == 0 || shnum > MAX_SHNUM) {
        return FALSE;
    }
    guchar* tab = read_block(r, shoff, (guint64)shnum * shentsize);
    if (tab == NULL) {
        return FALSE;
    }
    for (guint i = 0; i < shnum; i++) {
        Section s = parse_section(r, tab + (gsize)i * shentsize);
        g_array_append_val(out, s);
    }
    g_free(tab);
    return TRUE;
}

/* File offset of a virtual address, through the PT_LOAD segments */
static gboolean vaddr_to_offset(const GArray* segs, guint64 vaddr, guint64* offset) {
    for (guint i = 0; i < segs->len; i++) {
        const Segment* s = &g_array_index(segs, Segment, i);
        if (s->type == PT_LOAD && vaddr >= s->vaddr && vaddr - s->vaddr < s->filesz) {
            *offset = s->offset + (vaddr - s->vaddr);
            return TRUE;
        }
    }
    return FALSE;
}

/* DT_NEEDED, DT_SONAME, DT_RPATH, DT_RUNPATH. The dynamic table comes from the
 * SHT_DYNAMIC section (string table: its link) or, without section headers,
 * from PT_DYNAMIC (string table: DT_STRTAB mapped through PT_LOAD). */
static void read_dynamic(const Reader* r, const GArray* segs, const GArray* secs, ElfInfo* info) {
    guint64 dyn_off = 0, dyn_size = 0;
    const Section* str_sec = NULL;
    for (guint i = 0; i < secs->len; i++) {
        const Section* s = &g_array_index(secs, Section, i);
        if (s->type == SHT_DYNAMIC) {
            dyn_off = s->offset;
            dyn_size = s->size;
            if (s->link < secs->len) {
                str_sec = &g_array_index(secs, Section, s->link);
            }
            break;
        }
    }
    if (dyn_size == 0) {
        for (guint i = 0; i < segs->len; i++) {
            const Segment* s = &g_array_index(segs, Segment, i);
            if (s->type == PT_DYNAMIC) {
                dyn_off = s->offset;
                dyn_size = s->filesz;
                break;
            }
        }
    }
    if (dyn_size == 0) {
        return;
    }
    info->has_dynamic = TRUE;

    gsize entsize = r->is_64 ? 16 : 8;
    guint64 n = MIN(dyn_size / entsize, (guint64)MAX_DYN_ENTRIES);
    guchar* dyn = read_block(r, dyn_off, n * entsize);
    if (dyn == NULL) {
        return;
    }

    guint64 strtab_vaddr = 0, strsz = 0;
    gboolean has_strtab = FALSE;
    for (guint64 i = 0; i < n; i++) {
        const guchar* p = dyn + i * entsize;
        guint64 tag = UADDR(p);
        guint64 val = UADDR(p + entsize / 2);
        if (tag == DT_NULL) {
            n = i;
            break;
        }
        if (tag == DT_STRTAB) {
            strtab_vaddr = val;
            has_strtab = TRUE;
        } else if (tag == DT_STRSZ) {
            strsz = val;
        }
    }

    guchar* strtab = NULL;
    guint64 strtab_size = 0;
    if (str_sec != NULL && str_sec->type == SHT_STRTAB) {
        strtab_size = str_sec->size;
        strtab = read_block(r, str_sec->offset, strtab_size);
    } else if (has_strtab && strsz > 0) {
        guint64 off;
        if (vaddr_to_offset(segs, strtab_vaddr, &off)) {
            strtab_size = strsz;
            strtab = read_block(r, off, strtab_size);
        }
    }

    if (strtab != NULL) {
        for (guint64 i = 0; i < n; i++) {
            const guchar* p = dyn + i * entsize;
            guint64 tag = UADDR(p);
            guint64 val = UADDR(p + entsize / 2);
            gchar** field = NULL;
            if (tag == DT_NEEDED) {
                gchar* name = table_string(strtab, strtab_size, val);
                if (name != NULL) {
                    g_ptr_array_add(info->needed, name);
                }
                continue;
            }
            if (tag == DT_SONAME) {
                field = &info->soname;
            } else if (tag == DT_RPATH) {
                field = &info->rpath;
            } else if (tag == DT_RUNPATH) {
                field = &info->runpath;
            }
            if (field != NULL && *field == NULL) {
                *field = table_string(strtab, strtab_size, val);
            }
        }
    }
    g_free(strtab);
    g_free(dyn);
}

static gchar* read_interpreter(const Reader* r, const GArray* segs) {
    for (guint i = 0; i < segs->len; i++) {
        const Segment* s = &g_array_index(segs, Segment, i);
        if (s->type == PT_INTERP && s->filesz > 0 && s->filesz < 4096) {
            guchar* buf = read_block(r, s->offset, s->filesz);
            gchar* str = table_string(buf, s->filesz, 0);
            g_free(buf);
            return str;
        }
    }
    return NULL;
}

/* GNU build ID in a block of notes, or NULL */
static gchar* find_build_id(const Reader* r, const guchar* notes, guint64 size) {
    guint64 pos = 0;
    while (pos + 12 <= size) {
        guint64 namesz = U32(notes + pos);
        guint64 descsz = U32(notes + pos + 4);
        guint32 type = U32(notes + pos + 8);
        guint64 name_at = pos + 12;
        guint64 desc_at = name_at + ((namesz + 3) & ~(guint64)3);
        guint64 next = desc_at + ((descsz + 3) & ~(guint64)3);
        if (desc_at > size || descsz > size - desc_at) {
            return NULL;
        }
        if (type == NT_GNU_BUILD_ID && namesz == 4 && memcmp(notes + name_at, "GNU", 4) == 0 &&
            descsz > 0 && descsz <= 64) {
            GString* hex = g_string_new(NULL);
            for (guint64 i = 0; i < descsz; i++) {
                g_string_append_printf(hex, "%02x", notes[desc_at + i]);
            }
            return g_string_free(hex, FALSE);
        }
        pos = next;
    }
    return NULL;
}

static gchar* read_build_id(const Reader* r, const GArray* segs, const GArray* secs) {
    for (guint pass = 0; pass < 2; pass++) {
        guint count = pass == 0 ? secs->len : segs->len;
        for (guint i = 0; i < count; i++) {
            guint64 off, size;
            if (pass == 0) {
                const Section* s = &g_array_index(secs, Section, i);
                if (s->type != SHT_NOTE) {
                    continue;
                }
                off = s->offset;
                size = s->size;
            } else {
                const Segment* s = &g_array_index(segs, Segment, i);
                if (s->type != PT_NOTE) {
                    continue;
                }
                off = s->offset;
                size = s->filesz;
            }
            if (size > 65536) {
                continue;
            }
            guchar* notes = read_block(r, off, size);
            gchar* id = notes != NULL ? find_build_id(r, notes, size) : NULL;
            g_free(notes);
            if (id != NULL) {
                return id;
            }
        }
    }
    return NULL;
}

static gint compare_names(gconstpointer a, gconstpointer b) {
    return strcmp(*(const gchar* const*)a, *(const gchar* const*)b);
}

/* Defined global symbols with default visibility, from .symtab for object
 * files and .dynsym otherwise (what other code can link against) */
static void read_exported(const Reader* r, const GArray* secs, ElfInfo* info) {
    guint32 wanted = (info->type == ET_REL) ? SHT_SYMTAB : SHT_DYNSYM;
    const Section* sym = NULL;
    for (guint i = 0; i < secs->len; i++) {
        const Section* s = &g_array_index(secs, Section, i);
        if (s->type == wanted) {
            sym = s;
            break;
        }
    }
    gsize entsize = r->is_64 ? 24 : 16;
    if (sym == NULL || sym->link >= secs->len) {
        return;
    }
    const Section* str = &g_array_index(secs, Section, sym->link);
    guchar* tab = read_block(r, sym->offset, (sym->size / entsize) * entsize);
    guchar* strtab = read_block(r, str->offset, str->size);
    if (tab == NULL || strtab == NULL) {
        g_free(tab);
        g_free(strtab);
        return;
    }

    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    guint64 n = sym->size / entsize;
    for (guint64 i = 1; i < n; i++) { /* entry 0 is the null symbol */
        const guchar* p = tab + i * entsize;
        guint32 name = U32(p);
        guint8 st_info, other;
        guint16 shndx;
        guint64 size;
        if (r->is_64) {
            st_info = p[4];
            other = p[5];
            shndx = U16(p + 6);
            size = U64(p + 16);
        } else {
            size = U32(p + 8);
            st_info = p[12];
            other = p[13];
            shndx = U16(p + 14);
        }
        guint bind = st_info >> 4, type = st_info & 0xf, vis = other & 3;
        if (name == 0 || shndx == SHN_UNDEF ||
            (bind != STB_GLOBAL && bind != STB_WEAK && bind != STB_GNU_UNIQUE) ||
            (type != STT_FUNC && type != STT_OBJECT && type != STT_TLS && type != STT_GNU_IFUNC) ||
            vis == STV_HIDDEN || vis == STV_INTERNAL ||
            (shndx == SHN_ABS && size == 0)) { /* version names (GLIBC_2.34) */
            continue;
        }
        gchar* s = table_string(strtab, str->size, name);
        if (s != NULL) {
            g_ptr_array_add(names, s);
        }
    }
    g_free(tab);
    g_free(strtab);

    g_ptr_array_sort(names, compare_names);
    info->n_exported = names->len;
    if (names->len > ELF_INFO_MAX_SYMBOLS) {
        g_ptr_array_set_size(names, ELF_INFO_MAX_SYMBOLS);
    }
    g_ptr_array_unref(info->exported);
    info->exported = names;
}

gboolean elf_info_is_elf(const guchar* head, gsize len) {
    return head != NULL && len >= 4 && memcmp(head, "\x7f" "ELF", 4) == 0;
}

static void set_error(gchar** error, const gchar* msg) {
    if (error != NULL) {
        *error = g_strdup(msg);
    }
}

ElfInfo* elf_info_read(ElfReadFunc read, gpointer ctx, gchar** error) {
    guchar eh[64];
    if (!read(ctx, 0, eh, 52) || !elf_info_is_elf(eh, 52)) {
        set_error(error, "Not an ELF file");
        return NULL;
    }
    if ((eh[4] != 1 && eh[4] != 2) || (eh[5] != 1 && eh[5] != 2)) {
        set_error(error, "Unknown ELF class or byte order");
        return NULL;
    }
    Reader rd = { read, ctx, eh[4] == 2, eh[5] == 2 };
    const Reader* r = &rd;
    if (r->is_64 && !read(ctx, 0, eh, 64)) {
        set_error(error, "Truncated ELF header");
        return NULL;
    }

    ElfInfo* info = g_new0(ElfInfo, 1);
    info->is_64 = r->is_64;
    info->big_endian = r->big_endian;
    info->osabi = eh[7];
    info->type = U16(eh + 16);
    info->machine = U16(eh + 18);
    info->needed = g_ptr_array_new_with_free_func(g_free);
    info->exported = g_ptr_array_new_with_free_func(g_free);

    guint64 phoff, shoff;
    guint phentsize, phnum, shentsize, shnum, shstrndx;
    if (r->is_64) {
        info->entry = U64(eh + 24);
        phoff = U64(eh + 32);
        shoff = U64(eh + 40);
        phentsize = U16(eh + 54);
        phnum = U16(eh + 56);
        shentsize = U16(eh + 58);
        shnum = U16(eh + 60);
        shstrndx = U16(eh + 62);
    } else {
        info->entry = U32(eh + 24);
        phoff = U32(eh + 28);
        shoff = U32(eh + 32);
        phentsize = U16(eh + 42);
        phnum = U16(eh + 44);
        shentsize = U16(eh + 46);
        shnum = U16(eh + 48);
        shstrndx = U16(eh + 50);
    }

    GArray* segs = g_array_new(FALSE, FALSE, sizeof(Segment));
    GArray* secs = g_array_new(FALSE, FALSE, sizeof(Section));
    read_segments(r, phoff, phnum, phentsize, segs);
    if (!read_sections(r, shoff, shnum, shentsize, &shstrndx, secs)) {
        g_array_set_size(secs, 0);
    }
    info->n_sections = secs->len;

    /* Section names: debug info and symbol table */
    if (shstrndx < secs->len) {
        const Section* names = &g_array_index(secs, Section, shstrndx);
        guchar* shstr = read_block(r, names->offset, names->size);
        for (guint i = 0; shstr != NULL && i < secs->len; i++) {
            const Section* s = &g_array_index(secs, Section, i);
            if (s->name >= names->size) {
                continue;
            }
            const gchar* name = (const gchar*)shstr + s->name;
            if (strcmp(name, ".debug_info") == 0 || strcmp(name, ".zdebug_info") == 0) {
                info->has_debug = TRUE;
            }
        }
        g_free(shstr);
    }
    for (guint i = 0; i < secs->len; i++) {
        if (g_array_index(secs, Section, i).type == SHT_SYMTAB) {
            info->has_symtab = TRUE;
        }
    }

    info->interpreter = read_interpreter(r, segs);
    read_dynamic(r, segs, secs, info);
    info->build_id = read_build_id(r, segs, secs);
    read_exported(r, secs, info);

    g_array_unref(segs);
    g_array_unref(secs);
    return info;
}

static gboolean read_from_file(gpointer ctx, guint64 offset, gpointer buf, gsize len) {
    FILE* fp = ctx;
    if (offset > (guint64)G_MAXINT64 || fseeko(fp, (off_t)offset, SEEK_SET) != 0) {
        return FALSE;
    }
    return fread(buf, 1, len, fp) == len;
}

ElfInfo* elf_info_read_file(const gchar* path, gchar** error) {
    FILE* fp = g_fopen(path, "rb");
    if (fp == NULL) {
        set_error(error, "Cannot open the file");
        return NULL;
    }
    ElfInfo* info = elf_info_read(read_from_file, fp, error);
    fclose(fp);
    return info;
}

typedef struct {
    const guchar* data;
    gsize len;
} MemSource;

static gboolean read_from_memory(gpointer ctx, guint64 offset, gpointer buf, gsize len) {
    const MemSource* m = ctx;
    if (offset > m->len || len > m->len - offset) {
        return FALSE;
    }
    memcpy(buf, m->data + offset, len);
    return TRUE;
}

ElfInfo* elf_info_read_data(const guchar* data, gsize len, gchar** error) {
    MemSource m = { data, len };
    return elf_info_read(read_from_memory, &m, error);
}

void elf_info_free(ElfInfo* info) {
    if (info == NULL) {
        return;
    }
    g_free(info->interpreter);
    g_free(info->soname);
    g_free(info->rpath);
    g_free(info->runpath);
    g_free(info->build_id);
    g_ptr_array_unref(info->needed);
    g_ptr_array_unref(info->exported);
    g_free(info);
}

gchar* elf_info_machine_name(guint16 machine) {
    static const struct { guint16 id; const gchar* name; } machines[] = {
        { 2, "SPARC" },       { 3, "x86 (i386)" }, { 4, "Motorola 68000" },
        { 8, "MIPS" },        { 20, "PowerPC" },   { 21, "PowerPC 64" },
        { 22, "IBM S/390" },  { 40, "ARM" },       { 42, "SuperH" },
        { 43, "SPARC V9" },   { 50, "IA-64" },     { 62, "x86-64" },
        { 183, "AArch64" },   { 243, "RISC-V" },   { 247, "BPF" },
        { 258, "LoongArch" },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(machines); i++) {
        if (machines[i].id == machine) {
            return g_strdup(machines[i].name);
        }
    }
    return machine == 0 ? g_strdup("none") : g_strdup_printf("machine %u", machine);
}

const gchar* elf_info_type_name(const ElfInfo* info) {
    switch (info->type) {
    case ET_REL:  return "Relocatable object";
    case ET_EXEC: return "Executable";
    case ET_DYN:
        /* A PIE has an interpreter; a library has none (or a soname, like libc) */
        return (info->interpreter != NULL && info->soname == NULL)
                   ? "Position-independent executable" : "Shared library";
    case ET_CORE: return "Core dump";
    default:      return "Unknown type";
    }
}

static const gchar* osabi_name(guint8 osabi) {
    switch (osabi) {
    case 0:  return "System V";
    case 2:  return "NetBSD";
    case 3:  return "GNU/Linux";
    case 6:  return "Solaris";
    case 9:  return "FreeBSD";
    case 12: return "OpenBSD";
    case 97: return "ARM";
    case 255: return "Standalone";
    default: return NULL;
    }
}

static void add_field(GString* out, const gchar* label, const gchar* value) {
    g_string_append_printf(out, "%-*s %s\n", ELF_INFO_LABEL_WIDTH, label, value);
}

gchar* elf_info_format(const ElfInfo* info) {
    GString* out = g_string_new(NULL);
    gchar* machine = elf_info_machine_name(info->machine);
    gchar* arch = g_strdup_printf("%s (%s, %s)", machine, info->is_64 ? "64-bit" : "32-bit",
                                  info->big_endian ? "big-endian" : "little-endian");
    add_field(out, "Architecture", arch);
    g_free(arch);
    g_free(machine);

    gboolean linkable = (info->type == ET_EXEC || info->type == ET_DYN);
    if (info->type == ET_EXEC || info->type == ET_DYN || info->type == ET_REL ||
        info->type == ET_CORE) {
        add_field(out, "Type", elf_info_type_name(info));
    } else {
        gchar* t = g_strdup_printf("Unknown type %u", info->type);
        add_field(out, "Type", t);
        g_free(t);
    }
    const gchar* abi = osabi_name(info->osabi);
    gchar* abi_num = abi == NULL ? g_strdup_printf("%u", info->osabi) : NULL;
    add_field(out, "OS ABI", abi != NULL ? abi : abi_num);
    g_free(abi_num);
    if (linkable) {
        add_field(out, "Linking", info->has_dynamic ? "Dynamic" : "Static");
    }
    if (info->interpreter != NULL) {
        add_field(out, "Interpreter", info->interpreter);
    }
    if (linkable && info->entry != 0) {
        gchar* e = g_strdup_printf("0x%" G_GINT64_MODIFIER "x", info->entry);
        add_field(out, "Entry point", e);
        g_free(e);
    }
    if (info->soname != NULL) {
        add_field(out, "Soname", info->soname);
    }
    if (info->rpath != NULL) {
        add_field(out, "RPATH", info->rpath);
    }
    if (info->runpath != NULL) {
        add_field(out, "RUNPATH", info->runpath);
    }
    if (info->build_id != NULL) {
        add_field(out, "Build ID", info->build_id);
    }
    if (info->type != ET_CORE) {
        add_field(out, "Symbols", info->has_debug ? "Debug info" :
                                  info->has_symtab ? "Not stripped" : "Stripped");
    }
    gchar* n = g_strdup_printf("%u", info->n_sections);
    add_field(out, "Sections", n);
    g_free(n);

    if (linkable) {
        if (info->needed->len > 0) {
            g_string_append_printf(out, "\nLinked libraries (%u)\n", info->needed->len);
            for (guint i = 0; i < info->needed->len; i++) {
                g_string_append_printf(out, "  %s\n", (const gchar*)info->needed->pdata[i]);
            }
        } else {
            g_string_append(out, "\nLinked libraries: none\n");
        }
    }

    if (info->n_exported > 0) {
        if (info->exported->len < info->n_exported) {
            g_string_append_printf(out, "\nExported symbols (%u, first %u shown)\n",
                                   info->n_exported, info->exported->len);
        } else {
            g_string_append_printf(out, "\nExported symbols (%u)\n", info->n_exported);
        }
        for (guint i = 0; i < info->exported->len; i++) {
            g_string_append_printf(out, "  %s\n", (const gchar*)info->exported->pdata[i]);
        }
    }
    return g_string_free(out, FALSE);
}
