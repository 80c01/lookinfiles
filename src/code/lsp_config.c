/* Language server commands (see lsp_config.h) */
#include "code/lsp_config.h"
#include <string.h>

typedef struct {
    CodeLang lang;
    const gchar* key;
    const gchar* defaults[4];   /* candidate command lines, best first */
} ServerDefaults;

static const ServerDefaults SERVERS[] = {
    {CODE_LANG_C, "c", {"clangd", NULL}},
    {CODE_LANG_CPP, "cpp", {"clangd", NULL}},
    {CODE_LANG_OBJC, "objective-c", {"clangd", NULL}},
    {CODE_LANG_JAVA, "java", {"jdtls", NULL}},
    {CODE_LANG_CSHARP, "csharp", {"csharp-ls", "OmniSharp -lsp", NULL}},
    {CODE_LANG_PYTHON, "python", {"pylsp", "pyright-langserver --stdio", "jedi-language-server", NULL}},
    {CODE_LANG_SHELL, "shellscript", {"bash-language-server start", NULL}},
    {CODE_LANG_JS, "javascript", {"typescript-language-server --stdio", NULL}},
    {CODE_LANG_GO, "go", {"gopls", NULL}},
    {CODE_LANG_RUST, "rust", {"rust-analyzer", NULL}},
    {CODE_LANG_LUA, "lua", {"lua-language-server", NULL}},
    {CODE_LANG_RUBY, "ruby", {"solargraph stdio", "ruby-lsp", NULL}},
    {CODE_LANG_PHP, "php", {"intelephense --stdio", "phpactor language-server", NULL}},
    {CODE_LANG_PERL, "perl", {"pls", NULL}},
    {CODE_LANG_KOTLIN, "kotlin", {"kotlin-language-server", NULL}},
    {CODE_LANG_SWIFT, "swift", {"sourcekit-lsp", NULL}},
    {CODE_LANG_SCALA, "scala", {"metals", NULL}},
    {CODE_LANG_DART, "dart", {"dart language-server", NULL}},
    {CODE_LANG_HASKELL, "haskell", {"haskell-language-server-wrapper --lsp", NULL}},
    {CODE_LANG_ELIXIR, "elixir", {"elixir-ls", NULL}},
    {CODE_LANG_ERLANG, "erlang", {"erlang_ls", NULL}},
};

static const ServerDefaults* defaults_of(CodeLang lang) {
    for (gsize i = 0; i < G_N_ELEMENTS(SERVERS); i++) {
        if (SERVERS[i].lang == lang) return &SERVERS[i];
    }
    return NULL;
}

const gchar* lsp_config_key(CodeLang lang) {
    const ServerDefaults* d = defaults_of(lang);
    return d != NULL ? d->key : NULL;
}

/* argv of a command line when its program is installed, else NULL */
static gchar** installed(const gchar* line) {
    gchar** argv = NULL;
    if (line == NULL || !g_shell_parse_argv(line, NULL, &argv, NULL)) return NULL;
    gchar* program = g_find_program_in_path(argv[0]);
    if (program == NULL) {
        g_strfreev(argv);
        return NULL;
    }
    g_free(argv[0]);
    argv[0] = program;
    return argv;
}

/* Configured command line of key: NULL when absent, "" when disabled */
static gchar* configured(const gchar* config_path, const gchar* key) {
    if (config_path == NULL || key == NULL) return NULL;
    GKeyFile* kf = g_key_file_new();
    gchar* value = NULL;
    if (g_key_file_load_from_file(kf, config_path, G_KEY_FILE_NONE, NULL)) {
        value = g_key_file_get_string(kf, "servers", key, NULL);
        if (value != NULL) {
            g_strstrip(value);
            if (g_ascii_strcasecmp(value, "none") == 0 || g_ascii_strcasecmp(value, "off") == 0) value[0] = '\0';
        }
    }
    g_key_file_free(kf);
    return value;
}

gchar** lsp_config_command(CodeLang lang, const gchar* config_path) {
    const ServerDefaults* d = defaults_of(lang);
    if (d == NULL) return NULL;
    gchar* line = configured(config_path, d->key);
    if (line == NULL && code_lang_family(lang) == CODE_LANG_C && lang != CODE_LANG_C) {
        line = configured(config_path, "c");
    }
    if (line != NULL) {
        gchar** argv = line[0] != '\0' ? installed(line) : NULL;
        g_free(line);
        return argv;
    }
    for (gsize i = 0; d->defaults[i] != NULL; i++) {
        gchar** argv = installed(d->defaults[i]);
        if (argv != NULL) return argv;
    }
    return NULL;
}
