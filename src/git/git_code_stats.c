/* The files of a repository counted by language (see git_code_stats.h) */
#include "git/git_code_stats.h"
#include "git/git_lib.h"

#include <string.h>

/* Files looked at between two checks of the cancellable */
#define CODE_CANCEL_STEP 64
/* Bytes looked at for a NUL (as git does) */
#define BINARY_PROBE 8000

typedef struct {
    const gchar* key;
    const gchar* language;
} LangKey;

/* Whole file names (compared in lower case) */
static const LangKey NAMES[] = {
    {"makefile", "Makefile"},     {"gnumakefile", "Makefile"},  {"dockerfile", "Dockerfile"},
    {"containerfile", "Dockerfile"}, {"cmakelists.txt", "CMake"}, {"meson.build", "Meson"},
    {"meson_options.txt", "Meson"}, {"meson.options", "Meson"}, {"rakefile", "Ruby"},
    {"gemfile", "Ruby"},          {"vagrantfile", "Ruby"},      {"podfile", "Ruby"},
    {"jenkinsfile", "Groovy"},    {"pkgbuild", "Shell"},        {".bashrc", "Shell"},
    {".bash_profile", "Shell"},   {".zshrc", "Shell"},          {".profile", "Shell"},
    {"configure.ac", "M4"},       {"build.bazel", "Starlark"},  {"workspace.bazel", "Starlark"},
    {"kconfig", "Kconfig"},
};

/* Extensions (without the dot, compared in lower case) */
static const LangKey EXTENSIONS[] = {
    {"c", "C"}, {"h", "C"},
    {"cc", "C++"}, {"cpp", "C++"}, {"cxx", "C++"}, {"c++", "C++"}, {"hpp", "C++"}, {"hh", "C++"},
    {"hxx", "C++"}, {"h++", "C++"}, {"ipp", "C++"}, {"tpp", "C++"}, {"inl", "C++"}, {"ino", "C++"},
    {"m", "Objective-C"}, {"mm", "Objective-C++"},
    {"cs", "C#"}, {"java", "Java"}, {"kt", "Kotlin"}, {"kts", "Kotlin"}, {"scala", "Scala"},
    {"groovy", "Groovy"}, {"gradle", "Groovy"}, {"go", "Go"}, {"rs", "Rust"}, {"swift", "Swift"},
    {"zig", "Zig"}, {"d", "D"}, {"nim", "Nim"}, {"vala", "Vala"}, {"vapi", "Vala"},
    {"py", "Python"}, {"pyw", "Python"}, {"pyi", "Python"}, {"pyx", "Cython"}, {"pxd", "Cython"},
    {"rb", "Ruby"}, {"rake", "Ruby"}, {"gemspec", "Ruby"}, {"pl", "Perl"}, {"pm", "Perl"},
    {"php", "PHP"}, {"lua", "Lua"}, {"r", "R"}, {"jl", "Julia"},
    {"js", "JavaScript"}, {"mjs", "JavaScript"}, {"cjs", "JavaScript"}, {"jsx", "JavaScript"},
    {"ts", "TypeScript"}, {"mts", "TypeScript"}, {"cts", "TypeScript"}, {"tsx", "TSX"},
    {"vue", "Vue"}, {"svelte", "Svelte"}, {"dart", "Dart"},
    {"ex", "Elixir"}, {"exs", "Elixir"}, {"erl", "Erlang"}, {"hrl", "Erlang"},
    {"hs", "Haskell"}, {"lhs", "Haskell"}, {"ml", "OCaml"}, {"mli", "OCaml"},
    {"fs", "F#"}, {"fsi", "F#"}, {"fsx", "F#"}, {"elm", "Elm"}, {"cr", "Crystal"}, {"hx", "Haxe"},
    {"clj", "Clojure"}, {"cljs", "Clojure"}, {"cljc", "Clojure"}, {"edn", "Clojure"},
    {"el", "Emacs Lisp"}, {"lisp", "Common Lisp"}, {"cl", "Common Lisp"}, {"scm", "Scheme"},
    {"ss", "Scheme"}, {"rkt", "Racket"},
    {"sh", "Shell"}, {"bash", "Shell"}, {"zsh", "Shell"}, {"ksh", "Shell"}, {"dash", "Shell"},
    {"fish", "Fish"}, {"ps1", "PowerShell"}, {"psm1", "PowerShell"}, {"psd1", "PowerShell"},
    {"bat", "Batchfile"}, {"cmd", "Batchfile"}, {"vim", "Vim Script"}, {"tcl", "Tcl"},
    {"awk", "Awk"}, {"sql", "SQL"}, {"asm", "Assembly"}, {"s", "Assembly"}, {"nasm", "Assembly"},
    {"f", "Fortran"}, {"for", "Fortran"}, {"f90", "Fortran"}, {"f95", "Fortran"}, {"f03", "Fortran"},
    {"f08", "Fortran"}, {"pas", "Pascal"}, {"ada", "Ada"}, {"adb", "Ada"}, {"ads", "Ada"},
    {"cob", "COBOL"}, {"cbl", "COBOL"}, {"vhd", "VHDL"}, {"vhdl", "VHDL"}, {"v", "Verilog"},
    {"sv", "SystemVerilog"}, {"svh", "SystemVerilog"}, {"cu", "CUDA"}, {"cuh", "CUDA"},
    {"glsl", "GLSL"}, {"vert", "GLSL"}, {"frag", "GLSL"}, {"geom", "GLSL"}, {"comp", "GLSL"},
    {"hlsl", "HLSL"}, {"wgsl", "WGSL"}, {"metal", "Metal"},
    {"proto", "Protocol Buffers"}, {"thrift", "Thrift"}, {"graphql", "GraphQL"}, {"gql", "GraphQL"},
    {"sol", "Solidity"}, {"tf", "HCL"}, {"hcl", "HCL"}, {"nix", "Nix"}, {"gd", "GDScript"},
    {"qml", "QML"}, {"blp", "Blueprint"}, {"typ", "Typst"}, {"lean", "Lean"}, {"m4", "M4"},
    {"cmake", "CMake"}, {"mk", "Makefile"}, {"mak", "Makefile"}, {"am", "Makefile"},
    {"html", "HTML"}, {"htm", "HTML"}, {"xhtml", "HTML"}, {"css", "CSS"}, {"scss", "SCSS"},
    {"sass", "Sass"}, {"less", "Less"},
    {"xml", "XML"}, {"xsd", "XML"}, {"xsl", "XSLT"}, {"xslt", "XSLT"}, {"ui", "XML"},
    {"glade", "XML"}, {"plist", "XML"}, {"svg", "SVG"},
    {"json", "JSON"}, {"jsonc", "JSON"}, {"json5", "JSON"}, {"jsonl", "JSON"},
    {"ipynb", "Jupyter Notebook"}, {"yaml", "YAML"}, {"yml", "YAML"}, {"toml", "TOML"},
    {"ini", "INI"}, {"cfg", "INI"}, {"conf", "INI"}, {"desktop", "INI"},
    {"md", "Markdown"}, {"markdown", "Markdown"}, {"mdx", "MDX"}, {"rst", "reStructuredText"},
    {"org", "Org"}, {"adoc", "AsciiDoc"}, {"asciidoc", "AsciiDoc"}, {"tex", "TeX"},
    {"sty", "TeX"}, {"cls", "TeX"}, {"ltx", "TeX"}, {"dtx", "TeX"}, {"bib", "BibTeX"},
    {"txt", "Text"}, {"po", "Gettext Catalog"}, {"pot", "Gettext Catalog"},
    {"csv", "CSV"}, {"tsv", "CSV"}, {"diff", "Diff"}, {"patch", "Diff"},
};

/* Interpreters of a #! line (version digits dropped: python3 is python) */
static const LangKey INTERPRETERS[] = {
    {"sh", "Shell"},   {"bash", "Shell"},     {"zsh", "Shell"},  {"dash", "Shell"},
    {"ksh", "Shell"},  {"ash", "Shell"},      {"fish", "Fish"},  {"python", "Python"},
    {"perl", "Perl"},  {"ruby", "Ruby"},      {"node", "JavaScript"}, {"deno", "TypeScript"},
    {"lua", "Lua"},    {"php", "PHP"},        {"tclsh", "Tcl"},  {"wish", "Tcl"},
    {"awk", "Awk"},    {"gawk", "Awk"},       {"Rscript", "R"},  {"julia", "Julia"},
    {"pwsh", "PowerShell"}, {"make", "Makefile"},
};

/* Folders of vendored or generated files, anywhere in the path */
static const gchar* const VENDORED_DIRS[] = {
    "node_modules", "vendor", "third_party", "third-party", "thirdparty", "bower_components",
    "dist", "Pods", "Carthage", "__pycache__",
};

/* Generated files (whole names) */
static const gchar* const GENERATED_NAMES[] = {
    "package-lock.json", "npm-shrinkwrap.json", "yarn.lock", "pnpm-lock.yaml", "bun.lockb",
    "Cargo.lock", "poetry.lock", "Pipfile.lock", "uv.lock", "Gemfile.lock", "composer.lock",
    "go.sum", "flake.lock", "Package.resolved", "pubspec.lock", "mix.lock", "packages.lock.json",
};

/* Generated files (ends of names) */
static const gchar* const GENERATED_SUFFIXES[] = {
    ".min.js", ".min.css", ".min.mjs", ".js.map", ".css.map", ".pb.go", ".pb.cc", ".pb.h",
    "_pb2.py", ".designer.cs", ".g.dart", ".freezed.dart",
};

/* The tables above as hash tables, made once (by any thread) */
static GHashTable* names_table;
static GHashTable* ext_table;
static GHashTable* interp_table;

static GHashTable* table_of(const LangKey* keys, gsize n) {
    GHashTable* t = g_hash_table_new(g_str_hash, g_str_equal);
    for (gsize i = 0; i < n; i++) g_hash_table_insert(t, (gpointer)keys[i].key, (gpointer)keys[i].language);
    return t;
}

static void ensure_tables(void) {
    static gsize done = 0;
    if (!g_once_init_enter(&done)) return;
    names_table = table_of(NAMES, G_N_ELEMENTS(NAMES));
    ext_table = table_of(EXTENSIONS, G_N_ELEMENTS(EXTENSIONS));
    interp_table = table_of(INTERPRETERS, G_N_ELEMENTS(INTERPRETERS));
    g_once_init_leave(&done, 1);
}

/* The language of a #! line */
static const gchar* shebang_language(const gchar* head, gsize len) {
    if (len < 3 || head[0] != '#' || head[1] != '!') return NULL;
    const gchar* end = memchr(head, '\n', MIN(len, (gsize)256));
    gchar* line = g_strndup(head + 2, end != NULL ? (gsize)(end - head - 2) : MIN(len, (gsize)256) - 2);
    gchar** words = g_strsplit_set(g_strstrip(line), " \t", -1);
    const gchar* language = NULL;
    gboolean env = FALSE;
    for (gint i = 0; words[i] != NULL && language == NULL; i++) {
        if (words[i][0] == '\0') continue;
        gchar* base = g_path_get_basename(words[i]);
        if (i == 0 && strcmp(base, "env") == 0) {
            env = TRUE;
        } else if (env && base[0] == '-') {
            /* an option of env (-S) */
        } else if (env && strchr(base, '=') != NULL) {
            /* a variable set by env */
        } else {
            gsize n = strlen(base);
            while (n > 0 && (g_ascii_isdigit(base[n - 1]) || base[n - 1] == '.')) base[--n] = '\0';
            language = g_hash_table_lookup(interp_table, base);
            g_free(base);
            break;
        }
        g_free(base);
    }
    g_strfreev(words);
    g_free(line);
    return language;
}

const gchar* git_code_language(const gchar* path, const gchar* head, gsize len) {
    const gchar* slash = strrchr(path, '/');
    const gchar* name = slash != NULL ? slash + 1 : path;
    ensure_tables();
    gchar* lower = g_ascii_strdown(name, -1);
    const gchar* language = g_hash_table_lookup(names_table, lower);
    const gchar* dot = strrchr(lower, '.');
    gboolean extension = dot != NULL && dot != lower; /* not the dot of a dot-file */
    if (language == NULL && extension && dot[1] != '\0') language = g_hash_table_lookup(ext_table, dot + 1);
    g_free(lower);
    if (language == NULL && !extension) language = shebang_language(head, len);
    return language;
}

gboolean git_code_path_generated(const gchar* path) {
    gchar** parts = g_strsplit(path, "/", -1);
    guint n = g_strv_length(parts);
    gboolean generated = FALSE;
    for (guint i = 0; i + 1 < n && !generated; i++) {
        for (gsize k = 0; k < G_N_ELEMENTS(VENDORED_DIRS) && !generated; k++)
            generated = strcmp(parts[i], VENDORED_DIRS[k]) == 0;
    }
    const gchar* name = n > 0 ? parts[n - 1] : "";
    for (gsize k = 0; k < G_N_ELEMENTS(GENERATED_NAMES) && !generated; k++)
        generated = strcmp(name, GENERATED_NAMES[k]) == 0;
    for (gsize k = 0; k < G_N_ELEMENTS(GENERATED_SUFFIXES) && !generated; k++)
        generated = g_str_has_suffix(name, GENERATED_SUFFIXES[k]);
    g_strfreev(parts);
    return generated;
}

gboolean git_code_is_binary(const gchar* data, gsize len) {
    return len > 0 && memchr(data, '\0', MIN(len, (gsize)BINARY_PROBE)) != NULL;
}

void git_code_count_lines(const gchar* data, gsize len, guint64* lines, guint64* nonblank) {
    guint64 all = 0, full = 0;
    gboolean filled = FALSE, open = FALSE;
    for (gsize i = 0; i < len; i++) {
        gchar c = data[i];
        if (c == '\n') {
            all++;
            if (filled) full++;
            filled = FALSE;
            open = FALSE;
            continue;
        }
        open = TRUE;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\f' && c != '\v') filled = TRUE;
    }
    if (open) {
        all++;
        if (filled) full++;
    }
    *lines = all;
    *nonblank = full;
}

static void lang_free(gpointer data) {
    GitLangStats* l = data;
    g_free(l->name);
    g_free(l);
}

GitCodeStats* git_code_stats_new(void) {
    GitCodeStats* s = g_new0(GitCodeStats, 1);
    s->languages = g_ptr_array_new_with_free_func(lang_free);
    s->by_name = g_hash_table_new(g_str_hash, g_str_equal);
    return s;
}

void git_code_stats_free(GitCodeStats* s) {
    if (s == NULL) return;
    g_hash_table_unref(s->by_name);
    g_ptr_array_unref(s->languages);
    g_free(s->tree);
    g_free(s);
}

void git_code_stats_add(GitCodeStats* s, const gchar* path, const gchar* data, gsize len) {
    if (git_code_path_generated(path)) {
        s->generated++;
        return;
    }
    if (len > GIT_CODE_MAX_BYTES) {
        s->large++;
        return;
    }
    if (git_code_is_binary(data, len)) {
        s->binary++;
        return;
    }
    const gchar* language = git_code_language(path, data, len);
    GitLangStats* l = g_hash_table_lookup(s->by_name, language != NULL ? language : "");
    if (l == NULL) {
        l = g_new0(GitLangStats, 1);
        l->name = g_strdup(language);
        g_ptr_array_add(s->languages, l);
        g_hash_table_insert(s->by_name, (gpointer)(language != NULL ? language : ""), l);
    }
    guint64 lines, nonblank;
    git_code_count_lines(data, len, &lines, &nonblank);
    l->files++;
    l->lines += lines;
    l->nonblank += nonblank;
    l->bytes += len;
    s->files++;
    s->lines += lines;
    s->nonblank += nonblank;
    s->bytes += len;
}

static gint by_lines(gconstpointer pa, gconstpointer pb) {
    const GitLangStats* a = *(GitLangStats* const*)pa;
    const GitLangStats* b = *(GitLangStats* const*)pb;
    if ((a->name == NULL) != (b->name == NULL)) return a->name == NULL ? 1 : -1;
    if (a->lines != b->lines) return a->lines > b->lines ? -1 : 1;
    if (a->bytes != b->bytes) return a->bytes > b->bytes ? -1 : 1;
    return g_strcmp0(a->name, b->name);
}

void git_code_stats_finish(GitCodeStats* s) {
    g_ptr_array_sort(s->languages, by_lines);
}

typedef struct {
    git_repository* repo;
    git_odb* odb;
    GitCodeStats* stats;
    GCancellable* cancellable;
    guint seen;
    gboolean cancelled;
    gboolean linguist; /* some attributes name linguist-: asked for each file */
} Walk;

static gboolean mentions_linguist(const gchar* data, gsize len) {
    return g_strstr_len(data, (gssize)len, "linguist-") != NULL;
}

/* A .gitattributes of the tree mentions linguist- */
static int find_linguist(const char* dir, const git_tree_entry* entry, void* payload) {
    (void)dir;
    Walk* w = payload;
    git_filemode_t mode = git_tree_entry_filemode(entry);
    if (mode != GIT_FILEMODE_BLOB || strcmp(git_tree_entry_name(entry), ".gitattributes") != 0) return 0;
    git_blob* blob = NULL;
    if (git_blob_lookup(&blob, w->repo, git_tree_entry_id(entry)) == 0) {
        w->linguist = mentions_linguist(git_blob_rawcontent(blob), (gsize)git_blob_rawsize(blob));
        git_blob_free(blob);
    }
    return w->linguist ? -1 : 0;
}

/* The attributes of the repository (its .gitattributes files at HEAD, or
 * .git/info/attributes) mention linguist- */
static gboolean uses_linguist(Walk* w, git_tree* tree) {
    gchar* info = g_build_filename(git_repository_path(w->repo), "info", "attributes", NULL);
    gchar* data = NULL;
    gsize len = 0;
    if (g_file_get_contents(info, &data, &len, NULL)) w->linguist = mentions_linguist(data, len);
    g_free(data);
    g_free(info);
    if (!w->linguist) git_tree_walk(tree, GIT_TREEWALK_PRE, find_linguist, w);
    return w->linguist;
}

/* The linguist-generated or linguist-vendored attribute of path is set */
static gboolean attr_generated(git_repository* repo, const gchar* path) {
    static const gchar* const attrs[] = {"linguist-generated", "linguist-vendored"};
    guint32 flags = GIT_ATTR_CHECK_FILE_THEN_INDEX | GIT_ATTR_CHECK_INCLUDE_HEAD;
    for (gsize i = 0; i < G_N_ELEMENTS(attrs); i++) {
        const char* value = NULL;
        if (git_attr_get(&value, repo, flags, path, attrs[i]) == 0 && git_attr_value(value) == GIT_ATTR_VALUE_TRUE)
            return TRUE;
    }
    return FALSE;
}

static int walk_entry(const char* dir, const git_tree_entry* entry, void* payload) {
    Walk* w = payload;
    git_filemode_t mode = git_tree_entry_filemode(entry);
    if (mode != GIT_FILEMODE_BLOB && mode != GIT_FILEMODE_BLOB_EXECUTABLE) return 0; /* folders, links, submodules */
    if (++w->seen % CODE_CANCEL_STEP == 0 && g_cancellable_is_cancelled(w->cancellable)) {
        w->cancelled = TRUE;
        return -1;
    }
    gchar* path = g_strconcat(dir, git_tree_entry_name(entry), NULL);
    const git_oid* oid = git_tree_entry_id(entry);
    size_t size = 0;
    git_object_t type;
    if (git_code_path_generated(path) || (w->linguist && attr_generated(w->repo, path))) {
        w->stats->generated++;
    } else if (git_odb_read_header(&size, &type, w->odb, oid) == 0 && size > GIT_CODE_MAX_BYTES) {
        w->stats->large++;
    } else {
        git_blob* blob = NULL;
        if (git_blob_lookup(&blob, w->repo, oid) == 0) {
            git_code_stats_add(w->stats, path, git_blob_rawcontent(blob), (gsize)git_blob_rawsize(blob));
            git_blob_free(blob);
        }
    }
    g_free(path);
    return 0;
}

GitCodeStats* git_code_stats_read(const gchar* root, const gchar* unless_tree, gboolean* unchanged,
                                  GCancellable* cancellable) {
    if (unchanged != NULL) *unchanged = FALSE;
    git_repository* repo = git_lib_open(root);
    if (repo == NULL) return NULL;
    GitCodeStats* s = git_code_stats_new();
    git_object* head = NULL;
    git_tree* tree = NULL;
    if (git_revparse_single(&head, repo, "HEAD^{tree}") == 0) tree = (git_tree*)head; /* fails without commits */
    if (tree != NULL) {
        const gchar* id = git_oid_tostr_s(git_tree_id(tree));
        if (unless_tree != NULL && strcmp(id, unless_tree) == 0) {
            if (unchanged != NULL) *unchanged = TRUE;
            g_clear_pointer(&s, git_code_stats_free);
        } else {
            s->tree = g_strdup(id);
            Walk w = {repo, NULL, s, cancellable, 0, FALSE, FALSE};
            uses_linguist(&w, tree);
            if (git_repository_odb(&w.odb, repo) == 0) {
                git_tree_walk(tree, GIT_TREEWALK_PRE, walk_entry, &w);
                git_odb_free(w.odb);
            }
            if (w.cancelled) g_clear_pointer(&s, git_code_stats_free);
        }
        git_object_free(head);
    }
    git_repository_free(repo);
    if (s != NULL && g_cancellable_is_cancelled(cancellable)) g_clear_pointer(&s, git_code_stats_free);
    if (s != NULL) git_code_stats_finish(s);
    return s;
}
