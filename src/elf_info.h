#ifndef ELF_INFO_H
#define ELF_INFO_H

#include <glib.h>

/* Header details of an ELF file (executables, shared libraries, object
 * files, core dumps): architecture, type, linked libraries, exported symbols.
 * Only the parts needed are read, through a callback, so a large library is
 * never loaded whole. No widgets (unit-tested in tests/). */

/* Reads len bytes at offset into buf; FALSE if they are not all available */
typedef gboolean (*ElfReadFunc)(gpointer ctx, guint64 offset, gpointer buf, gsize len);

/* Width of the field labels in elf_info_format() (values start one column after) */
#define ELF_INFO_LABEL_WIDTH 14

/* At most this many exported symbol names are kept (the count is exact) */
#define ELF_INFO_MAX_SYMBOLS 500

typedef struct {
    gboolean is_64;        /* ELFCLASS64 */
    gboolean big_endian;
    guint16 type;          /* ET_REL, ET_EXEC, ET_DYN, ET_CORE */
    guint16 machine;       /* EM_* */
    guint8 osabi;
    guint64 entry;
    guint n_sections;

    gchar* interpreter;    /* PT_INTERP, e.g. /lib64/ld-linux-x86-64.so.2 */
    gboolean has_dynamic;  /* PT_DYNAMIC or a SHT_DYNAMIC section */
    gchar* soname;         /* DT_SONAME */
    gchar* rpath;          /* DT_RPATH */
    gchar* runpath;        /* DT_RUNPATH */
    GPtrArray* needed;     /* DT_NEEDED library names, in file order */

    gboolean has_symtab;   /* .symtab present (not stripped) */
    gboolean has_debug;    /* .debug_info present */
    gchar* build_id;       /* GNU build ID in hex, or NULL */

    guint n_exported;      /* defined global / weak functions and objects */
    GPtrArray* exported;   /* their names (sorted, first ELF_INFO_MAX_SYMBOLS) */
} ElfInfo;

/* TRUE if the first bytes are an ELF signature */
gboolean elf_info_is_elf(const guchar* head, gsize len);

/* Parse an ELF file. NULL (and *error set if given) when the header is not
 * valid ELF; missing or damaged optional parts are skipped. Free with
 * elf_info_free(). */
ElfInfo* elf_info_read(ElfReadFunc read, gpointer ctx, gchar** error);

/* elf_info_read() on a file on disk (blocking: worker thread only) */
ElfInfo* elf_info_read_file(const gchar* path, gchar** error);

/* elf_info_read() on bytes in memory */
ElfInfo* elf_info_read_data(const guchar* data, gsize len, gchar** error);

void elf_info_free(ElfInfo* info);

/* "x86-64", "AArch64"... or "machine 1234" for an unknown one. Caller frees. */
gchar* elf_info_machine_name(guint16 machine);

/* "Shared library", "Position-independent executable", "Relocatable object"... */
const gchar* elf_info_type_name(const ElfInfo* info);

/* Text summary for the preview: aligned fields, then the linked libraries and
 * the exported symbols. Display in a monospace font. Caller frees. */
gchar* elf_info_format(const ElfInfo* info);

#endif /* ELF_INFO_H */
