#ifndef LSP_CONFIG_H
#define LSP_CONFIG_H

/* Language server commands (docs/code-nav.md): built-in defaults
 * per language (clangd, pylsp, gopls...), overridden by the [servers]
 * group of lsp.ini in the configuration directory:
 *
 *   [servers]
 *   python=pyright-langserver --stdio
 *   c=clangd --background-index
 *   rust=none            # no server for Rust
 *
 * Keys are the languages' LSP ids (lsp_config_key()); C++ and Objective-C
 * fall back to the "c" entry. Pure GLib, unit-tested. */
#include <glib.h>

#include "code_symbol.h"

/* Key of lang in lsp.ini ("c", "cpp", "python", "javascript"...), NULL when
 * the language has no server */
const gchar* lsp_config_key(CodeLang lang);

/* Command of the server for lang (argv, g_strfreev), or NULL when none is
 * configured or installed: the configured command is used as is (if its
 * program exists), else the first default whose program is in PATH.
 * config_path may be NULL or missing. */
gchar** lsp_config_command(CodeLang lang, const gchar* config_path);

#endif /* LSP_CONFIG_H */
