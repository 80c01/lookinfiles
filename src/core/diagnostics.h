#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

/* What this computer or a remote machine has of the libraries and programs
 * LookinFiles uses, and how to install what is missing (the Diagnostics
 * tab, diagnostics_tab.h). A check is an item: the programs that can serve
 * a feature (any one of them is enough), or a library built in. The items
 * are checked here with g_find_program_in_path(), or on a remote machine by
 * one shell command (diagnostics_remote_command(), its output read by
 * diagnostics_remote_apply()). Install commands follow the package manager
 * of the machine's system, read from its /etc/os-release. Pure GLib,
 * unit-tested. */
#include <glib.h>

typedef enum {
    DIAG_SECTION_BUILT_IN = 0, /* libraries of this build (local only) */
    DIAG_SECTION_PROGRAMS,     /* programs looked for in PATH */
    DIAG_SECTION_LANG_SERVERS  /* language servers (docs/code-nav.md) */
} DiagSection;

/* Package manager of a system */
typedef enum {
    DIAG_FAMILY_UNKNOWN = 0,
    DIAG_FAMILY_APT,    /* Debian, Ubuntu, Mint... */
    DIAG_FAMILY_DNF,    /* Fedora, RHEL, CentOS, Rocky, Alma */
    DIAG_FAMILY_PACMAN, /* Arch, Manjaro, EndeavourOS */
    DIAG_FAMILY_ZYPPER, /* openSUSE */
    DIAG_FAMILY_APK,    /* Alpine */
    DIAG_FAMILY_BREW    /* macOS (Homebrew) */
} DiagFamily;

typedef struct {
    gchar* id;        /* stable name ("ctags", "latex", "lsp-python"): guide anchors, rows */
    DiagSection section;
    gchar* feature;   /* what it gives, in English (translated with _() when shown),
                       * or a language name for a language server */
    gchar** programs; /* programs that serve it, any one is enough (NULL: built in) */
    gchar* found;     /* what was found (a program's path, "built in"), NULL when missing */
    gboolean checked; /* found is meaningful */
} DiagItem;

/* The checks of this computer (remote FALSE: with the built-in libraries
 * and the local-only programs) or of a remote machine, without the language
 * servers (diagnostics_add_lang_server()). GPtrArray of DiagItem*, frees
 * them. */
GPtrArray* diagnostics_items(gboolean remote);

/* Append the check of a language server: lines are its command lines
 * (lsp_config_command_lines()), best first; the programs are their first
 * words. Nothing when lines is NULL or empty. */
void diagnostics_add_lang_server(GPtrArray* items, const gchar* key, const gchar* language, gchar** lines);

/* The item named id, or NULL */
DiagItem* diagnostics_find(GPtrArray* items, const gchar* id);

/* Check the items on this computer: PATH, the built-in libraries, the
 * Python modules of the kernels (spawns python; run it off the interface
 * thread) */
void diagnostics_check_local(GPtrArray* items);

/* One POSIX sh command checking the items on a remote machine (output for
 * diagnostics_remote_apply()). Caller frees. */
gchar* diagnostics_remote_command(GPtrArray* items);

/* Fill the items from the output of diagnostics_remote_command(); *os_release
 * (may be NULL) gets the machine's /etc/os-release text, or NULL. */
void diagnostics_remote_apply(GPtrArray* items, const gchar* output, gchar** os_release);

/* Package manager of the system described by os_release (the text of
 * /etc/os-release: ID, then ID_LIKE); DIAG_FAMILY_UNKNOWN for NULL */
DiagFamily diagnostics_family(const gchar* os_release);

/* Package manager of this computer (its /etc/os-release, Homebrew on macOS) */
DiagFamily diagnostics_local_family(void);

/* A shell command installing what item needs on a system of family, or
 * NULL when it is found or nothing is known. Built-in libraries: the
 * development package to install before building again. Caller frees. */
gchar* diagnostics_install_command(const DiagItem* item, DiagFamily family);

/* Pretty name of the system (PRETTY_NAME, else NAME, of os_release), or NULL.
 * Caller frees. */
gchar* diagnostics_os_name(const gchar* os_release);

#endif /* DIAGNOSTICS_H */
