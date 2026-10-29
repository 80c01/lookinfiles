/* Unit tests for src/elf_info.c: ELF header details (architecture, type,
 * linked libraries, exported symbols). Synthetic files are built in memory;
 * the test binary itself and an object file of the build are read too. */
#include "elf_info.h"

#include <string.h>

/* --- Minimal ELF writer --------------------------------------------------- */

typedef struct {
    GByteArray* buf;
    gboolean is_64;
    gboolean big_endian;
} ElfBuilder;

static void put_at(ElfBuilder* b, gsize at, guint64 v, gsize n) {
    if (b->buf->len < at + n) {
        g_byte_array_set_size(b->buf, (guint)(at + n));
    }
    for (gsize i = 0; i < n; i++) {
        gsize k = b->big_endian ? n - 1 - i : i;
        b->buf->data[at + k] = (guint8)(v >> (8 * i));
    }
}

static gsize addr_size(ElfBuilder* b) {
    return b->is_64 ? 8 : 4;
}

/* Append raw bytes, aligned on 8; returns their offset */
static gsize append(ElfBuilder* b, const void* data, gsize len) {
    while (b->buf->len % 8 != 0) {
        g_byte_array_append(b->buf, (const guint8*)"", 1);
    }
    gsize at = b->buf->len;
    g_byte_array_set_size(b->buf, (guint)(at + len));
    if (data != NULL) {
        memcpy(b->buf->data + at, data, len);
    } else {
        memset(b->buf->data + at, 0, len);
    }
    return at;
}

/* Append a NUL-terminated string to a string table; returns its offset */
static guint64 add_str(GString* table, const gchar* s) {
    guint64 at = table->len;
    g_string_append_len(table, s, (gssize)strlen(s) + 1);
    return at;
}

typedef struct {
    const gchar* name;
    guint8 info;   /* bind << 4 | type */
    guint8 other;  /* visibility */
    guint16 shndx;
} TestSym;

typedef struct {
    gboolean is_64, big_endian;
    guint16 type, machine;
    const gchar* interp;
    const gchar* soname;
    const gchar* runpath;
    const gchar* const* needed;
    const TestSym* syms;
    gsize n_syms;
    gboolean symtab;           /* symbols go to .symtab instead of .dynsym */
    gboolean no_sections;      /* no section headers: PT_DYNAMIC + PT_LOAD only */
    gboolean debug;            /* add a .debug_info section */
} ElfSpec;

#define LOAD_BIAS 0x400000

static GByteArray* build_elf(const ElfSpec* spec) {
    ElfBuilder bb = { g_byte_array_new(), spec->is_64, spec->big_endian };
    ElfBuilder* b = &bb;
    gsize ehsize = b->is_64 ? 64 : 52;
    gsize phentsize = b->is_64 ? 56 : 32;
    gsize shentsize = b->is_64 ? 64 : 40;
    gsize dynent = b->is_64 ? 16 : 8;
    gsize syment = b->is_64 ? 24 : 16;
    append(b, NULL, ehsize);

    /* .dynstr / .strtab: one table for every name */
    GString* str = g_string_new("");
    g_string_append_c(str, '\0');
    guint64 soname_at = spec->soname ? add_str(str, spec->soname) : 0;
    guint64 runpath_at = spec->runpath ? add_str(str, spec->runpath) : 0;
    guint64 needed_at[8] = {0};
    gsize n_needed = 0;
    for (; spec->needed != NULL && spec->needed[n_needed] != NULL; n_needed++) {
        needed_at[n_needed] = add_str(str, spec->needed[n_needed]);
    }
    guint64 sym_name_at[16] = {0};
    for (gsize i = 0; i < spec->n_syms; i++) {
        sym_name_at[i] = add_str(str, spec->syms[i].name);
    }
    gsize str_off = append(b, str->str, str->len);
    gsize str_len = str->len;
    g_string_free(str, TRUE);

    gsize interp_off = 0, interp_len = 0;
    if (spec->interp != NULL) {
        interp_len = strlen(spec->interp) + 1;
        interp_off = append(b, spec->interp, interp_len);
    }

    /* Dynamic table */
    gsize n_dyn = n_needed + 3 + (spec->soname != NULL) + (spec->runpath != NULL);
    gsize dyn_off = append(b, NULL, n_dyn * dynent);
    gsize d = dyn_off;
    #define DYN(tag, val) do { put_at(b, d, (tag), addr_size(b)); \
                               put_at(b, d + dynent / 2, (val), addr_size(b)); d += dynent; } while (0)
    for (gsize i = 0; i < n_needed; i++) {
        DYN(1, needed_at[i]);
    }
    if (spec->soname) {
        DYN(14, soname_at);
    }
    if (spec->runpath) {
        DYN(29, runpath_at);
    }
    DYN(5, LOAD_BIAS + str_off); /* DT_STRTAB (virtual address) */
    DYN(10, str_len);            /* DT_STRSZ */
    DYN(0, 0);

    /* GNU build ID note: de ad be ef */
    guint8 note[20] = {0};
    ElfBuilder nb = { g_byte_array_new(), b->is_64, b->big_endian };
    put_at(&nb, 0, 4, 4);
    put_at(&nb, 4, 4, 4);
    put_at(&nb, 8, 3, 4);
    memcpy(note, nb.buf->data, 12);
    g_byte_array_unref(nb.buf);
    memcpy(note + 12, "GNU\0\xde\xad\xbe\xef", 8);
    gsize note_off = append(b, note, sizeof note);

    /* Symbols (entry 0 is null) */
    gsize sym_off = append(b, NULL, (spec->n_syms + 1) * syment);
    for (gsize i = 0; i < spec->n_syms; i++) {
        gsize p = sym_off + (i + 1) * syment;
        put_at(b, p, sym_name_at[i], 4);
        if (b->is_64) {
            b->buf->data[p + 4] = spec->syms[i].info;
            b->buf->data[p + 5] = spec->syms[i].other;
            put_at(b, p + 6, spec->syms[i].shndx, 2);
        } else {
            b->buf->data[p + 12] = spec->syms[i].info;
            b->buf->data[p + 13] = spec->syms[i].other;
            put_at(b, p + 14, spec->syms[i].shndx, 2);
        }
    }

    const gchar shstr[] = "\0.dynstr\0.dynamic\0.dynsym\0.note\0.shstrtab\0.debug_info";
    gsize shstr_off = append(b, shstr, sizeof shstr);

    /* Program headers: LOAD (whole file), INTERP, DYNAMIC */
    gsize phnum = spec->interp ? 3 : 2;
    gsize ph_off = append(b, NULL, phnum * phentsize);
    gsize file_end = ph_off + phnum * phentsize;
    struct { guint32 type; gsize off, len; } ph[3] = {
        { 1, 0, 0 }, { 2, dyn_off, n_dyn * dynent }, { 3, interp_off, interp_len },
    };
    for (gsize i = 0; i < phnum; i++) {
        gsize p = ph_off + i * phentsize;
        gsize len = (i == 0) ? 0x100000 : ph[i].len; /* LOAD: patched below */
        put_at(b, p, ph[i].type, 4);
        if (b->is_64) {
            put_at(b, p + 8, ph[i].off, 8);
            put_at(b, p + 16, LOAD_BIAS + ph[i].off, 8);
            put_at(b, p + 32, len, 8);
        } else {
            put_at(b, p + 4, ph[i].off, 4);
            put_at(b, p + 8, LOAD_BIAS + ph[i].off, 4);
            put_at(b, p + 16, len, 4);
        }
    }

    /* Section headers */
    gsize shnum = 0, sh_off = 0;
    if (!spec->no_sections) {
        struct { guint32 name, type, link; gsize off, size, entsize; } sh[] = {
            { 0, 0, 0, 0, 0, 0 },
            { 1, 3, 0, str_off, str_len, 0 },                         /* .dynstr */
            { 9, 6, 1, dyn_off, n_dyn * dynent, dynent },             /* .dynamic */
            { 18, spec->symtab ? 2 : 11, 1, sym_off, (spec->n_syms + 1) * syment, syment },
            { 26, 7, 0, note_off, sizeof note, 0 },                   /* .note */
            { 32, 3, 0, shstr_off, sizeof shstr, 0 },                 /* .shstrtab */
            { 42, 1, 0, 0, 0, 0 },                                    /* .debug_info */
        };
        shnum = spec->debug ? 7 : 6;
        sh_off = append(b, NULL, shnum * shentsize);
        file_end = sh_off + shnum * shentsize;
        for (gsize i = 0; i < shnum; i++) {
            gsize p = sh_off + i * shentsize;
            put_at(b, p, sh[i].name, 4);
            put_at(b, p + 4, sh[i].type, 4);
            if (b->is_64) {
                put_at(b, p + 24, sh[i].off, 8);
                put_at(b, p + 32, sh[i].size, 8);
                put_at(b, p + 40, sh[i].link, 4);
                put_at(b, p + 56, sh[i].entsize, 8);
            } else {
                put_at(b, p + 16, sh[i].off, 4);
                put_at(b, p + 20, sh[i].size, 4);
                put_at(b, p + 24, sh[i].link, 4);
                put_at(b, p + 36, sh[i].entsize, 4);
            }
        }
    }
    /* LOAD covers the whole file */
    put_at(b, ph_off + (b->is_64 ? 32 : 16), file_end, addr_size(b));

    /* ELF header */
    memcpy(b->buf->data, "\x7f" "ELF", 4);
    b->buf->data[4] = b->is_64 ? 2 : 1;
    b->buf->data[5] = b->big_endian ? 2 : 1;
    b->buf->data[6] = 1;
    put_at(b, 16, spec->type, 2);
    put_at(b, 18, spec->machine, 2);
    put_at(b, 20, 1, 4);
    put_at(b, 24, 0x1040, addr_size(b));
    if (b->is_64) {
        put_at(b, 32, ph_off, 8);
        put_at(b, 40, sh_off, 8);
        put_at(b, 52, ehsize, 2);
        put_at(b, 54, phentsize, 2);
        put_at(b, 56, phnum, 2);
        put_at(b, 58, shentsize, 2);
        put_at(b, 60, shnum, 2);
        put_at(b, 62, shnum ? 5 : 0, 2);
    } else {
        put_at(b, 28, ph_off, 4);
        put_at(b, 32, sh_off, 4);
        put_at(b, 40, ehsize, 2);
        put_at(b, 42, phentsize, 2);
        put_at(b, 44, phnum, 2);
        put_at(b, 46, shentsize, 2);
        put_at(b, 48, shnum, 2);
        put_at(b, 50, shnum ? 5 : 0, 2);
    }
    return b->buf;
}

/* --- Tests ---------------------------------------------------------------- */

static const gchar* const NEEDED[] = { "libfoo.so.1", "libc.so.6", NULL };
static const TestSym SYMS[] = {
    { "zeta",      0x12, 0, 5 },  /* global function */
    { "alpha",     0x11, 0, 5 },  /* global object */
    { "weak_fn",   0x22, 0, 5 },  /* weak function */
    { "undef_fn",  0x12, 0, 0 },  /* undefined: imported */
    { "hidden_fn", 0x12, 2, 5 },  /* hidden visibility */
    { "local_fn",  0x02, 0, 5 },  /* local binding */
    { "LIB_1.0",   0x11, 0, 0xfff1 }, /* version name: absolute, size 0 */
};

static ElfSpec pie_spec(void) {
    ElfSpec s = {
        .is_64 = TRUE, .type = 3, .machine = 62,
        .interp = "/lib64/ld-linux-x86-64.so.2", .runpath = "$ORIGIN/../lib",
        .needed = NEEDED, .syms = SYMS, .n_syms = G_N_ELEMENTS(SYMS),
    };
    return s;
}

static void assert_strv(GPtrArray* got, const gchar* const* expected) {
    guint n = 0;
    for (; expected[n] != NULL; n++) {
        g_assert_cmpuint(n, <, got->len);
        g_assert_cmpstr(got->pdata[n], ==, expected[n]);
    }
    g_assert_cmpuint(got->len, ==, n);
}

static void test_not_elf(void) {
    gchar* err = NULL;
    g_assert_null(elf_info_read_data((const guchar*)"hello world", 11, &err));
    g_assert_nonnull(err);
    g_free(err);
    g_assert_false(elf_info_is_elf((const guchar*)"\x7f" "EL", 3));
    g_assert_true(elf_info_is_elf((const guchar*)"\x7f" "ELF\x02", 5));
    /* Valid magic, unknown class */
    guchar bad[64] = "\x7f" "ELF\x07\x01";
    g_assert_null(elf_info_read_data(bad, sizeof bad, NULL));
}

static void test_pie_64_little_endian(void) {
    ElfSpec spec = pie_spec();
    GByteArray* elf = build_elf(&spec);
    gchar* err = NULL;
    ElfInfo* info = elf_info_read_data(elf->data, elf->len, &err);
    g_assert_null(err);
    g_assert_nonnull(info);
    g_assert_true(info->is_64);
    g_assert_false(info->big_endian);
    g_assert_cmpuint(info->machine, ==, 62);
    g_assert_cmpstr(info->interpreter, ==, "/lib64/ld-linux-x86-64.so.2");
    g_assert_cmpstr(info->runpath, ==, "$ORIGIN/../lib");
    g_assert_null(info->soname);
    g_assert_cmpstr(info->build_id, ==, "deadbeef");
    g_assert_true(info->has_dynamic);
    g_assert_cmpuint(info->entry, ==, 0x1040);
    g_assert_cmpuint(info->n_sections, ==, 6);
    assert_strv(info->needed, NEEDED);
    const gchar* const exported[] = { "alpha", "weak_fn", "zeta", NULL };
    assert_strv(info->exported, exported);
    g_assert_cmpuint(info->n_exported, ==, 3);
    g_assert_cmpstr(elf_info_type_name(info), ==, "Position-independent executable");

    gchar* text = elf_info_format(info);
    g_assert_nonnull(strstr(text, "Architecture   x86-64 (64-bit, little-endian)\n"));
    g_assert_nonnull(strstr(text, "Type           Position-independent executable\n"));
    g_assert_nonnull(strstr(text, "Linking        Dynamic\n"));
    g_assert_nonnull(strstr(text, "Symbols        Stripped\n"));
    g_assert_nonnull(strstr(text, "\nLinked libraries (2)\n  libfoo.so.1\n  libc.so.6\n"));
    g_assert_nonnull(strstr(text, "\nExported symbols (3)\n  alpha\n  weak_fn\n  zeta\n"));
    g_assert_true(g_utf8_validate(text, -1, NULL));
    g_free(text);
    elf_info_free(info);
    g_byte_array_unref(elf);
}

static void test_library_32_big_endian(void) {
    ElfSpec spec = pie_spec();
    spec.is_64 = FALSE;
    spec.big_endian = TRUE;
    spec.machine = 20;
    spec.interp = NULL;
    spec.soname = "libbar.so.2";
    spec.debug = TRUE;
    GByteArray* elf = build_elf(&spec);
    ElfInfo* info = elf_info_read_data(elf->data, elf->len, NULL);
    g_assert_nonnull(info);
    g_assert_false(info->is_64);
    g_assert_true(info->big_endian);
    g_assert_cmpstr(info->soname, ==, "libbar.so.2");
    g_assert_null(info->interpreter);
    g_assert_true(info->has_debug);
    assert_strv(info->needed, NEEDED);
    g_assert_cmpuint(info->n_exported, ==, 3);
    g_assert_cmpstr(elf_info_type_name(info), ==, "Shared library");
    gchar* text = elf_info_format(info);
    g_assert_nonnull(strstr(text, "Architecture   PowerPC (32-bit, big-endian)\n"));
    g_assert_nonnull(strstr(text, "Soname         libbar.so.2\n"));
    g_assert_nonnull(strstr(text, "Symbols        Debug info\n"));
    g_free(text);
    elf_info_free(info);
    g_byte_array_unref(elf);
}

static void test_object_file_uses_symtab(void) {
    ElfSpec spec = pie_spec();
    spec.type = 1;
    spec.interp = NULL;
    spec.symtab = TRUE;
    GByteArray* elf = build_elf(&spec);
    ElfInfo* info = elf_info_read_data(elf->data, elf->len, NULL);
    g_assert_nonnull(info);
    g_assert_true(info->has_symtab);
    g_assert_cmpuint(info->n_exported, ==, 3);
    g_assert_cmpstr(elf_info_type_name(info), ==, "Relocatable object");
    gchar* text = elf_info_format(info);
    g_assert_nonnull(strstr(text, "Symbols        Not stripped\n"));
    g_assert_null(strstr(text, "Linking"));          /* not linked yet */
    g_assert_null(strstr(text, "Linked libraries"));
    g_assert_null(strstr(text, "Entry point"));
    g_free(text);
    elf_info_free(info);
    g_byte_array_unref(elf);
}

static void test_without_section_headers(void) {
    ElfSpec spec = pie_spec();
    spec.no_sections = TRUE;
    GByteArray* elf = build_elf(&spec);
    ElfInfo* info = elf_info_read_data(elf->data, elf->len, NULL);
    g_assert_nonnull(info);
    g_assert_cmpuint(info->n_sections, ==, 0);
    /* Dynamic table from PT_DYNAMIC, strings through DT_STRTAB and PT_LOAD */
    assert_strv(info->needed, NEEDED);
    g_assert_cmpstr(info->runpath, ==, "$ORIGIN/../lib");
    g_assert_cmpstr(info->interpreter, ==, "/lib64/ld-linux-x86-64.so.2");
    g_assert_cmpuint(info->n_exported, ==, 0);
    elf_info_free(info);
    g_byte_array_unref(elf);
}

static void test_static_executable(void) {
    ElfSpec spec = { .is_64 = TRUE, .type = 2, .machine = 183 };
    GByteArray* elf = build_elf(&spec);
    /* Drop the dynamic table: PT_DYNAMIC becomes PT_NULL, .dynamic SHT_NULL */
    ElfInfo* probe = elf_info_read_data(elf->data, elf->len, NULL);
    g_assert_nonnull(probe);
    elf_info_free(probe);
    guint64 phoff = 0, shoff = 0;
    for (gint i = 7; i >= 0; i--) {
        phoff = (phoff << 8) | elf->data[32 + i];
        shoff = (shoff << 8) | elf->data[40 + i];
    }
    memset(elf->data + phoff + 56, 0, 4);       /* second program header */
    memset(elf->data + shoff + 2 * 64 + 4, 0, 4); /* section 2 type */
    ElfInfo* info = elf_info_read_data(elf->data, elf->len, NULL);
    g_assert_nonnull(info);
    g_assert_false(info->has_dynamic);
    gchar* text = elf_info_format(info);
    g_assert_nonnull(strstr(text, "Architecture   AArch64 (64-bit, little-endian)\n"));
    g_assert_nonnull(strstr(text, "Linking        Static\n"));
    g_assert_nonnull(strstr(text, "Linked libraries: none\n"));
    g_free(text);
    elf_info_free(info);
    g_byte_array_unref(elf);
}

/* Truncated or corrupted files must never crash or read out of bounds */
static void test_damaged_files(void) {
    ElfSpec spec = pie_spec();
    GByteArray* elf = build_elf(&spec);
    for (guint len = 0; len <= elf->len; len++) {
        elf_info_free(elf_info_read_data(elf->data, len, NULL));
    }
    guchar* copy = g_memdup2(elf->data, elf->len);
    for (guint i = 4; i < elf->len; i++) {
        guchar saved = copy[i];
        const guchar values[] = { 0x00, 0x7f, 0xff };
        for (gsize v = 0; v < G_N_ELEMENTS(values); v++) {
            copy[i] = values[v];
            ElfInfo* info = elf_info_read_data(copy, elf->len, NULL);
            if (info != NULL) {
                g_free(elf_info_format(info));
            }
            elf_info_free(info);
        }
        copy[i] = saved;
    }
    g_free(copy);
    g_byte_array_unref(elf);
}

static void test_symbol_list_is_capped(void) {
    /* Format with more exported symbols than kept */
    ElfSpec spec = pie_spec();
    GByteArray* elf = build_elf(&spec);
    ElfInfo* info = elf_info_read_data(elf->data, elf->len, NULL);
    info->n_exported = 9999;
    gchar* text = elf_info_format(info);
    g_assert_nonnull(strstr(text, "\nExported symbols (9999, first 3 shown)\n"));
    g_free(text);
    elf_info_free(info);
    g_byte_array_unref(elf);
}

static gchar* test_dir = NULL;

/* The test program: a dynamically linked executable using GLib */
static void test_real_executable(void) {
    gchar* err = NULL;
    ElfInfo* info = elf_info_read_file("/proc/self/exe", &err);
    if (info == NULL) {
        g_test_skip(err);
        g_free(err);
        return;
    }
    g_assert_true(info->type == 2 || info->type == 3);
    g_assert_true(info->has_dynamic);
    gboolean has_glib = FALSE;
    for (guint i = 0; i < info->needed->len; i++) {
        has_glib |= g_str_has_prefix(info->needed->pdata[i], "libglib-2.0.so");
    }
    g_assert_true(has_glib);
    g_assert_cmpuint(info->n_sections, >, 10);
    elf_info_free(info);
}

/* An object file of the build: its global functions are exported */
static void test_real_object_file(void) {
    gchar* path = g_build_filename(test_dir, "..", "elf_info.o", NULL);
    ElfInfo* info = elf_info_read_file(path, NULL);
    g_free(path);
    if (info == NULL) {
        g_test_skip("build/elf_info.o not found");
        return;
    }
    g_assert_cmpuint(info->type, ==, 1);
    gboolean found = FALSE;
    for (guint i = 0; i < info->exported->len; i++) {
        found |= g_strcmp0(info->exported->pdata[i], "elf_info_read") == 0;
        g_assert_true(g_strcmp0(info->exported->pdata[i], "read_dynamic") != 0); /* static */
    }
    g_assert_true(found);
    elf_info_free(info);
}

static void test_machine_names(void) {
    gchar* s = elf_info_machine_name(243);
    g_assert_cmpstr(s, ==, "RISC-V");
    g_free(s);
    s = elf_info_machine_name(9999);
    g_assert_cmpstr(s, ==, "machine 9999");
    g_free(s);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    test_dir = g_path_get_dirname(argv[0]);
    g_test_add_func("/elf_info/not_elf", test_not_elf);
    g_test_add_func("/elf_info/pie_64_little_endian", test_pie_64_little_endian);
    g_test_add_func("/elf_info/library_32_big_endian", test_library_32_big_endian);
    g_test_add_func("/elf_info/object_file_uses_symtab", test_object_file_uses_symtab);
    g_test_add_func("/elf_info/without_section_headers", test_without_section_headers);
    g_test_add_func("/elf_info/static_executable", test_static_executable);
    g_test_add_func("/elf_info/damaged_files", test_damaged_files);
    g_test_add_func("/elf_info/symbol_list_is_capped", test_symbol_list_is_capped);
    g_test_add_func("/elf_info/real_executable", test_real_executable);
    g_test_add_func("/elf_info/real_object_file", test_real_object_file);
    g_test_add_func("/elf_info/machine_names", test_machine_names);
    gint res = g_test_run();
    g_free(test_dir);
    return res;
}
