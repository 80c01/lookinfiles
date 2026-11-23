/* Checks of the libraries and programs LookinFiles uses (see diagnostics.h) */
#include "core/diagnostics.h"

#include <string.h>

#include "core/i18n.h"

/* Packages of a library or a program, per package manager (NULL: none
 * known), and a command that installs it on any system (pipx, npm, go...) */
typedef struct {
    const gchar* key; /* an item id or a program name */
    const gchar* apt;
    const gchar* dnf;
    const gchar* pacman;
    const gchar* zypper;
    const gchar* apk;
    const gchar* brew;
    const gchar* any;
} Packages;

static const Packages PACKAGES[] = {
    /* Built-in libraries: the development packages the build looks for */
    {"vte", "libvte-2.91-gtk4-dev", "vte291-gtk4-devel", "vte4", NULL, NULL, NULL, NULL},
    {"gtksourceview", "libgtksourceview-5-dev", "gtksourceview5-devel", "gtksourceview5", "gtksourceview5-devel",
     "gtksourceview5-dev", "gtksourceview5", NULL},
    {"poppler", "libpoppler-glib-dev", "poppler-glib-devel", "poppler-glib", NULL, "poppler-dev", "poppler", NULL},
    {"sqlite", "libsqlite3-dev", "sqlite-devel", "sqlite", "sqlite3-devel", "sqlite-dev", "sqlite", NULL},
    {"libarchive", "libarchive-dev", "libarchive-devel", "libarchive", "libarchive-devel", "libarchive-dev",
     "libarchive", NULL},
    {"zstd", "libzstd-dev", "libzstd-devel", "zstd", "libzstd-devel", "zstd-dev", "zstd", NULL},
    {"libjpeg", "libjpeg-dev", "libjpeg-turbo-devel", "libjpeg-turbo", "libjpeg8-devel", "libjpeg-turbo-dev",
     "jpeg-turbo", NULL},
    {"webkit", "libwebkitgtk-6.0-dev", "webkitgtk6.0-devel", "webkitgtk-6.0", NULL, NULL, NULL, NULL},
    {"gstreamer", "libgstreamer-plugins-base1.0-dev", "gstreamer1-plugins-base-devel", "gst-plugins-base-libs",
     "gstreamer-plugins-base-devel", "gst-plugins-base-dev", "gst-plugins-base", NULL},
    {"libssh", "libssh-dev", "libssh-devel", "libssh", "libssh-devel", "libssh-dev", "libssh", NULL},
    /* Programs */
    {"ctags", "universal-ctags", "ctags", "ctags", "ctags", "ctags", "universal-ctags", NULL},
    {"latex", "latexmk texlive-latex-recommended", "latexmk", "texlive-binextra texlive-latex", "texlive-latexmk",
     "texlive", "tectonic", NULL},
    {"gs", "ghostscript", "ghostscript", "ghostscript", "ghostscript", "ghostscript", "ghostscript", NULL},
    {"dvips", "texlive-binaries", "texlive-dvips", "texlive-basic", "texlive-dvips", "texlive", NULL, NULL},
    {"ssh", "openssh-client", "openssh-clients", "openssh", "openssh-clients", "openssh-client", NULL, NULL},
    {"python", "python3", "python3", "python", "python3", "python3", "python", NULL},
    {"jupyter", "python3-ipykernel python3-jupyter-client", "python3-ipykernel", "python-ipykernel", NULL, NULL,
     NULL, "pip install jupyter_client ipykernel"},
    {"git", "git", "git", "git", "git", "git", "git", NULL},
    {"find", "findutils", "findutils", "findutils", "findutils", "findutils", NULL, NULL},
    {"grep", "grep", "grep", "grep", "grep", "grep", NULL, NULL},
    /* Language servers, by program */
    {"clangd", "clangd", "clang-tools-extra", "clang", NULL, "clang-extra-tools", "llvm", NULL},
    {"pylsp", "python3-pylsp", "python3-lsp-server", "python-lsp-server", NULL, NULL, NULL,
     "pipx install python-lsp-server"},
    {"pyright-langserver", NULL, NULL, "pyright", NULL, NULL, "pyright", "npm install -g pyright"},
    {"jedi-language-server", NULL, NULL, NULL, NULL, NULL, NULL, "pipx install jedi-language-server"},
    {"typescript-language-server", NULL, NULL, "typescript-language-server", NULL, NULL,
     "typescript-language-server", "npm install -g typescript-language-server typescript"},
    {"bash-language-server", NULL, NULL, "bash-language-server", NULL, NULL, "bash-language-server",
     "npm install -g bash-language-server"},
    {"gopls", "gopls", "golang-x-tools-gopls", "gopls", NULL, NULL, "gopls",
     "go install golang.org/x/tools/gopls@latest"},
    {"rust-analyzer", NULL, NULL, "rust-analyzer", NULL, NULL, "rust-analyzer", "rustup component add rust-analyzer"},
    {"lua-language-server", NULL, NULL, "lua-language-server", NULL, NULL, "lua-language-server", NULL},
    {"solargraph", NULL, NULL, NULL, NULL, NULL, NULL, "gem install solargraph"},
    {"ruby-lsp", NULL, NULL, NULL, NULL, NULL, NULL, "gem install ruby-lsp"},
    {"intelephense", NULL, NULL, NULL, NULL, NULL, NULL, "npm install -g intelephense"},
    {"jdtls", NULL, NULL, NULL, NULL, NULL, "jdtls", NULL},
    {"csharp-ls", NULL, NULL, NULL, NULL, NULL, NULL, "dotnet tool install --global csharp-ls"},
    {"kotlin-language-server", NULL, NULL, NULL, NULL, NULL, "kotlin-language-server", NULL},
    {"haskell-language-server-wrapper", NULL, NULL, NULL, NULL, NULL, NULL, "ghcup install hls"},
    {"elixir-ls", NULL, NULL, NULL, NULL, NULL, "elixir-ls", NULL},
    {"erlang_ls", NULL, NULL, NULL, NULL, NULL, "erlang_ls", NULL},
    {"metals", NULL, NULL, NULL, NULL, NULL, NULL, "cs install metals"},
    {"pls", NULL, NULL, NULL, NULL, NULL, NULL, "cpanm PLS"},
};

/* What is checked where */
enum { WHERE_LOCAL = 1, WHERE_REMOTE = 2, WHERE_BOTH = 3 };

typedef struct {
    const gchar* id;
    DiagSection section;
    const gchar* feature;
    const gchar* programs[4]; /* none: built in, or a check of its own (jupyter, find) */
    gint where;
} Check;

static const Check CHECKS[] = {
    {"vte", DIAG_SECTION_BUILT_IN, N_("Embedded terminal"), {NULL}, WHERE_LOCAL},
    {"gtksourceview", DIAG_SECTION_BUILT_IN, N_("Syntax highlighting in the editor"), {NULL}, WHERE_LOCAL},
    {"poppler", DIAG_SECTION_BUILT_IN, N_("PDF preview"), {NULL}, WHERE_LOCAL},
    {"sqlite", DIAG_SECTION_BUILT_IN, N_("Database preview"), {NULL}, WHERE_LOCAL},
    {"libarchive", DIAG_SECTION_BUILT_IN, N_("7z, RAR, xz, zstd and ISO archives"), {NULL}, WHERE_LOCAL},
    {"zstd", DIAG_SECTION_BUILT_IN, N_("Zstandard pages of Parquet files"), {NULL}, WHERE_LOCAL},
    {"libjpeg", DIAG_SECTION_BUILT_IN, N_("Fast JPEG photo preview"), {NULL}, WHERE_LOCAL},
    {"webkit", DIAG_SECTION_BUILT_IN, N_("Browser tabs"), {NULL}, WHERE_LOCAL},
    {"gstreamer", DIAG_SECTION_BUILT_IN, N_("Audio and subtitle tracks of videos"), {NULL}, WHERE_LOCAL},
    {"libssh", DIAG_SECTION_BUILT_IN, N_("Shared SSH connections (libssh)"), {NULL}, WHERE_LOCAL},
    {"ctags", DIAG_SECTION_PROGRAMS, N_("Code outline and Go to Definition without a language server"),
     {"ctags", "universal-ctags", "ctags-universal", NULL}, WHERE_LOCAL},
    {"latex", DIAG_SECTION_PROGRAMS, N_("LaTeX builds"), {"latexmk", "tectonic", "pdflatex", NULL}, WHERE_LOCAL},
    {"gs", DIAG_SECTION_PROGRAMS, N_("PostScript and EPS preview"), {"gs", NULL}, WHERE_LOCAL},
    {"dvips", DIAG_SECTION_PROGRAMS, N_("DVI preview"), {"dvips", NULL}, WHERE_LOCAL},
    {"ssh", DIAG_SECTION_PROGRAMS, N_("Remote machines"), {"ssh", NULL}, WHERE_LOCAL},
    {"python", DIAG_SECTION_PROGRAMS, N_("Python for Jupyter notebooks"), {"python3", "python", NULL}, WHERE_BOTH},
    {"jupyter", DIAG_SECTION_PROGRAMS, N_("Running notebook cells (jupyter_client, ipykernel)"), {NULL},
     WHERE_BOTH},
    {"git", DIAG_SECTION_PROGRAMS, N_("Git tab and Git badges"), {"git", NULL}, WHERE_REMOTE},
    {"grep", DIAG_SECTION_PROGRAMS, N_("Search in files"), {"grep", NULL}, WHERE_REMOTE},
    {"find", DIAG_SECTION_PROGRAMS, N_("Deep Search, Disk Usage and the Git tab (GNU find)"), {NULL},
     WHERE_REMOTE},
};

static void item_free(gpointer data) {
    DiagItem* item = data;
    g_free(item->id);
    g_free(item->feature);
    g_strfreev(item->programs);
    g_free(item->found);
    g_free(item);
}

GPtrArray* diagnostics_items(gboolean remote) {
    GPtrArray* items = diagnostics_items_empty();
    for (gsize i = 0; i < G_N_ELEMENTS(CHECKS); i++) {
        const Check* c = &CHECKS[i];
        if (!(c->where & (remote ? WHERE_REMOTE : WHERE_LOCAL))) continue;
        DiagItem* item = g_new0(DiagItem, 1);
        item->id = g_strdup(c->id);
        item->section = c->section;
        item->feature = g_strdup(c->feature);
        if (c->programs[0] != NULL) item->programs = g_strdupv((gchar**)c->programs);
        g_ptr_array_add(items, item);
    }
    return items;
}

GPtrArray* diagnostics_items_empty(void) {
    return g_ptr_array_new_with_free_func(item_free);
}

void diagnostics_add_info(GPtrArray* items, DiagSection section, const gchar* id,
                          const gchar* feature, const gchar* detail) {
    if (items == NULL || id == NULL || feature == NULL) return;
    DiagItem* item = g_new0(DiagItem, 1);
    item->id = g_strdup(id);
    item->section = section;
    item->feature = g_strdup(feature);
    item->found = g_strdup(detail != NULL ? detail : "");
    item->checked = TRUE;
    g_ptr_array_add(items, item);
}

void diagnostics_add_lang_server(GPtrArray* items, const gchar* key, const gchar* language, gchar** lines) {
    if (lines == NULL || lines[0] == NULL) return;
    GPtrArray* programs = g_ptr_array_new();
    for (gsize i = 0; lines[i] != NULL; i++) {
        gchar** argv = NULL;
        if (!g_shell_parse_argv(lines[i], NULL, &argv, NULL)) continue;
        gboolean seen = FALSE;
        for (guint j = 0; j < programs->len && !seen; j++) seen = strcmp(programs->pdata[j], argv[0]) == 0;
        if (!seen) g_ptr_array_add(programs, g_strdup(argv[0]));
        g_strfreev(argv);
    }
    if (programs->len == 0) {
        g_ptr_array_free(programs, TRUE);
        return;
    }
    g_ptr_array_add(programs, NULL);
    DiagItem* item = g_new0(DiagItem, 1);
    item->id = g_strconcat("lsp-", key, NULL);
    item->section = DIAG_SECTION_LANG_SERVERS;
    item->feature = g_strdup(language);
    item->programs = (gchar**)g_ptr_array_free(programs, FALSE);
    g_ptr_array_add(items, item);
}

DiagItem* diagnostics_find(GPtrArray* items, const gchar* id) {
    for (guint i = 0; items != NULL && id != NULL && i < items->len; i++) {
        DiagItem* item = g_ptr_array_index(items, i);
        if (strcmp(item->id, id) == 0) return item;
    }
    return NULL;
}

/* Whether this build has the library of a built-in item */
static gboolean built_in(const gchar* id) {
    static const struct {
        const gchar* id;
        gboolean have;
    } BUILT[] = {
#ifdef HAVE_VTE
        {"vte", TRUE},
#endif
#ifdef HAVE_GSV
        {"gtksourceview", TRUE},
#endif
#ifdef HAVE_POPPLER
        {"poppler", TRUE},
#endif
#ifdef HAVE_SQLITE
        {"sqlite", TRUE},
#endif
#ifdef HAVE_LIBARCHIVE
        {"libarchive", TRUE},
#endif
#ifdef HAVE_ZSTD
        {"zstd", TRUE},
#endif
#ifdef HAVE_LIBJPEG
        {"libjpeg", TRUE},
#endif
#if defined(HAVE_WEBKIT) || defined(HAVE_WEBVIEW2)
        {"webkit", TRUE},
#endif
#ifdef HAVE_GSTREAMER
        {"gstreamer", TRUE},
#endif
#ifdef HAVE_LIBSSH
        {"libssh", TRUE},
#endif
        {NULL, FALSE},
    };
    for (gsize i = 0; BUILT[i].id != NULL; i++) {
        if (strcmp(BUILT[i].id, id) == 0) return BUILT[i].have;
    }
    return FALSE;
}

/* The first of programs in PATH, or NULL. Caller frees. */
static gchar* first_in_path(gchar** programs) {
    for (gsize i = 0; programs != NULL && programs[i] != NULL; i++) {
        gchar* path = g_find_program_in_path(programs[i]);
        if (path != NULL) return path;
    }
    return NULL;
}

static const gchar* KERNEL_IMPORT = "import jupyter_client, ipykernel";

/* The Python of this computer that has the kernel modules, or NULL */
static gchar* local_kernel_python(void) {
    static const gchar* const PYTHONS[] = {"python3", "python", NULL};
    gchar* python = first_in_path((gchar**)PYTHONS);
    if (python == NULL) return NULL;
    gchar* argv[] = {python, (gchar*)"-c", (gchar*)KERNEL_IMPORT, NULL};
    gint status = 0;
    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
                      NULL, NULL, &status, NULL) ||
        !g_spawn_check_wait_status(status, NULL)) {
        g_clear_pointer(&python, g_free);
    }
    return python;
}

void diagnostics_check_local(GPtrArray* items) {
    for (guint i = 0; items != NULL && i < items->len; i++) {
        DiagItem* item = g_ptr_array_index(items, i);
        if (item->section == DIAG_SECTION_CONNECTION) continue;
        g_clear_pointer(&item->found, g_free);
        if (item->section == DIAG_SECTION_BUILT_IN) {
            item->found = built_in(item->id) ? g_strdup(_("built in")) : NULL;
        } else if (strcmp(item->id, "jupyter") == 0) {
            item->found = local_kernel_python();
        } else {
            item->found = first_in_path(item->programs);
        }
        item->checked = TRUE;
    }
}

gchar* diagnostics_remote_command(GPtrArray* items) {
    GString* s = g_string_new("exec 2>/dev/null; LC_ALL=C; export LC_ALL; "
                              "echo @@os; cat /etc/os-release || cat /usr/lib/os-release; "
                              "echo @@find; find --version | head -n 1; "
                              "echo @@jupyter; for p in python3 python; do if command -v $p >/dev/null; then ");
    g_string_append_printf(s, "$p -c '%s' && command -v $p; break; fi; done; ", KERNEL_IMPORT);
    g_string_append(s, "echo @@path; for p in");
    GHashTable* seen = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; items != NULL && i < items->len; i++) {
        DiagItem* item = g_ptr_array_index(items, i);
        for (gsize j = 0; item->programs != NULL && item->programs[j] != NULL; j++) {
            if (!g_hash_table_add(seen, item->programs[j])) continue;
            gchar* q = g_shell_quote(item->programs[j]);
            g_string_append_printf(s, " %s", q);
            g_free(q);
        }
    }
    g_hash_table_destroy(seen);
    g_string_append(s, "; do printf '%s\\t%s\\n' \"$p\" \"$(command -v \"$p\")\"; done; echo @@end");
    return g_string_free(s, FALSE);
}

/* The sections of the output ("@@<name>" lines): name -> text */
static GHashTable* sections(const gchar* text) {
    GHashTable* out = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gchar** lines = g_strsplit(text != NULL ? text : "", "\n", -1);
    gchar* name = NULL;
    GString* body = NULL;
    for (gsize i = 0; lines[i] != NULL; i++) {
        if (g_str_has_prefix(lines[i], "@@")) {
            if (name != NULL) g_hash_table_replace(out, name, g_string_free(body, FALSE));
            name = g_strstrip(g_strdup(lines[i] + 2));
            body = g_string_new(NULL);
        } else if (name != NULL) {
            g_string_append(body, lines[i]);
            g_string_append_c(body, '\n');
        }
    }
    if (name != NULL) g_hash_table_replace(out, name, g_string_free(body, FALSE));
    g_strfreev(lines);
    return out;
}

/* Text of a section, trimmed; NULL when missing or blank. Caller frees. */
static gchar* section_text(GHashTable* sec, const gchar* name) {
    const gchar* text = g_hash_table_lookup(sec, name);
    if (text == NULL) return NULL;
    gchar* s = g_strstrip(g_strdup(text));
    if (s[0] == '\0') g_clear_pointer(&s, g_free);
    return s;
}

void diagnostics_remote_apply(GPtrArray* items, const gchar* output, gchar** os_release) {
    GHashTable* sec = sections(output);
    /* program -> path (absent or "" when missing) */
    GHashTable* paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gchar** lines = g_strsplit(g_hash_table_lookup(sec, "path") != NULL ? g_hash_table_lookup(sec, "path") : "",
                               "\n", -1);
    for (gsize i = 0; lines[i] != NULL; i++) {
        gchar* tab = strchr(lines[i], '\t');
        if (tab == NULL) continue;
        *tab = '\0';
        gchar* path = g_strstrip(g_strdup(tab + 1));
        g_hash_table_replace(paths, g_strdup(lines[i]), path);
    }
    g_strfreev(lines);
    gchar* find = section_text(sec, "find");
    gchar* jupyter = section_text(sec, "jupyter");
    for (guint i = 0; items != NULL && i < items->len; i++) {
        DiagItem* item = g_ptr_array_index(items, i);
        if (item->section == DIAG_SECTION_CONNECTION) continue;
        g_clear_pointer(&item->found, g_free);
        if (strcmp(item->id, "find") == 0) {
            if (find != NULL && strstr(find, "GNU") != NULL) item->found = g_strdup(find);
        } else if (strcmp(item->id, "jupyter") == 0) {
            if (jupyter != NULL) item->found = g_strdup(jupyter);
        } else {
            for (gsize j = 0; item->programs != NULL && item->programs[j] != NULL && item->found == NULL; j++) {
                const gchar* path = g_hash_table_lookup(paths, item->programs[j]);
                if (path != NULL && path[0] != '\0') item->found = g_strdup(path);
            }
        }
        item->checked = TRUE;
    }
    if (os_release != NULL) *os_release = section_text(sec, "os");
    g_free(find);
    g_free(jupyter);
    g_hash_table_destroy(paths);
    g_hash_table_destroy(sec);
}

/* Value of key in os_release, unquoted; NULL when absent. Caller frees. */
static gchar* os_value(const gchar* os_release, const gchar* key) {
    if (os_release == NULL) return NULL;
    gchar** lines = g_strsplit(os_release, "\n", -1);
    gchar* value = NULL;
    gsize klen = strlen(key);
    for (gsize i = 0; lines[i] != NULL && value == NULL; i++) {
        const gchar* line = lines[i];
        while (*line == ' ' || *line == '\t') line++;
        if (strncmp(line, key, klen) != 0 || line[klen] != '=') continue;
        gchar* v = g_strstrip(g_strdup(line + klen + 1));
        gsize len = strlen(v);
        if (len >= 2 && (v[0] == '"' || v[0] == '\'') && v[len - 1] == v[0]) {
            value = g_strndup(v + 1, len - 2);
            g_free(v);
        } else {
            value = v;
        }
    }
    g_strfreev(lines);
    return value;
}

/* Package manager of one os-release id ("ubuntu", "rhel"...) */
static DiagFamily family_of_id(const gchar* id) {
    static const struct {
        const gchar* id;
        DiagFamily family;
    } IDS[] = {
        {"debian", DIAG_FAMILY_APT},    {"ubuntu", DIAG_FAMILY_APT},     {"linuxmint", DIAG_FAMILY_APT},
        {"pop", DIAG_FAMILY_APT},       {"raspbian", DIAG_FAMILY_APT},   {"fedora", DIAG_FAMILY_DNF},
        {"rhel", DIAG_FAMILY_DNF},      {"centos", DIAG_FAMILY_DNF},     {"rocky", DIAG_FAMILY_DNF},
        {"almalinux", DIAG_FAMILY_DNF}, {"arch", DIAG_FAMILY_PACMAN},    {"manjaro", DIAG_FAMILY_PACMAN},
        {"endeavouros", DIAG_FAMILY_PACMAN}, {"suse", DIAG_FAMILY_ZYPPER}, {"opensuse", DIAG_FAMILY_ZYPPER},
        {"alpine", DIAG_FAMILY_APK},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(IDS); i++) {
        if (g_ascii_strcasecmp(IDS[i].id, id) == 0) return IDS[i].family;
    }
    if (g_ascii_strncasecmp(id, "opensuse", 8) == 0) return DIAG_FAMILY_ZYPPER;
    return DIAG_FAMILY_UNKNOWN;
}

DiagFamily diagnostics_family(const gchar* os_release) {
    DiagFamily family = DIAG_FAMILY_UNKNOWN;
    gchar* id = os_value(os_release, "ID");
    if (id != NULL) family = family_of_id(id);
    g_free(id);
    if (family != DIAG_FAMILY_UNKNOWN) return family;
    gchar* like = os_value(os_release, "ID_LIKE");
    gchar** ids = g_strsplit(like != NULL ? like : "", " ", -1);
    for (gsize i = 0; ids[i] != NULL && family == DIAG_FAMILY_UNKNOWN; i++) {
        if (ids[i][0] != '\0') family = family_of_id(ids[i]);
    }
    g_strfreev(ids);
    g_free(like);
    return family;
}

DiagFamily diagnostics_local_family(void) {
#ifdef __APPLE__
    return DIAG_FAMILY_BREW;
#else
    gchar* text = NULL;
    if (!g_file_get_contents("/etc/os-release", &text, NULL, NULL)) {
        g_file_get_contents("/usr/lib/os-release", &text, NULL, NULL);
    }
    DiagFamily family = diagnostics_family(text);
    g_free(text);
    return family;
#endif
}

gchar* diagnostics_os_name(const gchar* os_release) {
    gchar* name = os_value(os_release, "PRETTY_NAME");
    if (name == NULL || name[0] == '\0') {
        g_free(name);
        name = os_value(os_release, "NAME");
    }
    return name;
}

static const Packages* packages_of(const gchar* key) {
    for (gsize i = 0; key != NULL && i < G_N_ELEMENTS(PACKAGES); i++) {
        if (strcmp(PACKAGES[i].key, key) == 0) return &PACKAGES[i];
    }
    return NULL;
}

/* Command installing p on family (its package, else the command for any
 * system), or NULL. Caller frees. */
static gchar* packages_command(const Packages* p, DiagFamily family) {
    if (p == NULL) return NULL;
    switch (family) {
    case DIAG_FAMILY_APT:
        if (p->apt != NULL) return g_strdup_printf("sudo apt install %s", p->apt);
        break;
    case DIAG_FAMILY_DNF:
        if (p->dnf != NULL) return g_strdup_printf("sudo dnf install %s", p->dnf);
        break;
    case DIAG_FAMILY_PACMAN:
        if (p->pacman != NULL) return g_strdup_printf("sudo pacman -S %s", p->pacman);
        break;
    case DIAG_FAMILY_ZYPPER:
        if (p->zypper != NULL) return g_strdup_printf("sudo zypper install %s", p->zypper);
        break;
    case DIAG_FAMILY_APK:
        if (p->apk != NULL) return g_strdup_printf("sudo apk add %s", p->apk);
        break;
    case DIAG_FAMILY_BREW:
        if (p->brew != NULL) return g_strdup_printf("brew install %s", p->brew);
        break;
    case DIAG_FAMILY_UNKNOWN:
        break;
    }
    return g_strdup(p->any);
}

gchar* diagnostics_install_command(const DiagItem* item, DiagFamily family) {
    if (item == NULL || item->found != NULL) return NULL;
    if (item->section != DIAG_SECTION_LANG_SERVERS) return packages_command(packages_of(item->id), family);
    /* The first server that has an install command */
    for (gsize i = 0; item->programs != NULL && item->programs[i] != NULL; i++) {
        gchar* command = packages_command(packages_of(item->programs[i]), family);
        if (command != NULL) return command;
    }
    return NULL;
}
