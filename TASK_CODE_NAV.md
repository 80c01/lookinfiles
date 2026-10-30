# TASK: code navigation in editor tabs

Follow references in source files (functions, variables, types, macros,
fields) and show what a symbol is (declaration, signature, documentation),
with the same features in the three editor modes (Notepad, Emacs, Vim).

---

## 1. Rules

1. **One engine, three key maps.** The lookups, the jump history and the
   result views are shared; each mode only maps its own keys to them and
   shows short messages where it already does (Notepad status label, Emacs
   echo area, Vim command line).
2. **Best backend available, never nothing.** Each project (git root, else
   the file's directory) uses the best backend it can, and falls back to
   the next one down:

   | Backend | Definition | References | Help / signature | Needs |
   |---|---|---|---|---|
   | LSP | exact | exact | documentation and signature | a server (`clangd`, `pylsp`...) |
   | ctags | good | whole-word search | signature only | `ctags` in `PATH` |
   | Built-in index | heuristic | whole-word search | declaration line | nothing |

   The status bar names the backend in use (`clangd`, `ctags`, `basic`).
3. **No new dependency.** LSP servers and ctags are external programs run
   with `GSubprocess` when installed; nothing is linked.
4. **Never block the UI thread.** Scans, file reads and server requests run
   in workers or asynchronously; a new request cancels the previous one;
   an answer for an older buffer version or a closed tab is dropped.
5. **Bounded.** The built-in index reads at most `CODE_INDEX_MAX_FILES`
   files and `CODE_INDEX_MAX_BYTES` bytes of one language family per lookup.
6. **Everything stays in the window.** Help cards are `GtkPopover`s at the
   cursor, the symbol picker is an `AppDialog`, the reference list is a tab
   of the preview notebook (like the Git tab).
7. **Testable without a display.** Parsing (scanners, ctags output, LSP
   messages, position conversion) is in layer 1 modules with unit tests.

## 2. Modules

**Layer 1 (foundation, unit-tested):**
- `code_scan` — languages (`CodeLang`, families), the identifier under the
  cursor (`code_word_at()`), symbol scanners per language (`CodeSymbol`:
  name, kind, line, byte column, declaration or definition, source line).
  Line-based regular expressions for Python, Shell, JavaScript / TypeScript,
  Go, Rust, Lua, Ruby, Makefile.
- `code_scan_c` — C-family scanner (C, C++, Java, C#): a small state
  machine over comments, strings, preprocessor lines, braces and
  statements; finds functions (definitions and prototypes), macros,
  typedefs, structs / unions / enums / classes, enum constants, fields and
  global variables.
- `code_index` — the project of a file, its source files (`git ls-files`,
  else a bounded directory walk), and the definition lookup: scan the files
  of the same language family that contain the name, rank the candidates
  (definition or declaration wanted, same file, same directory).
- `jump_list` — back / forward history of locations (path, line, column).
- later: `lsp_protocol` (JSON-RPC framing, requests, responses, UTF-16
  positions), `ctags_parse`.

**Layer 3 (feature):**
- `code_nav` — requests from an editor tab (definition, declaration,
  back, forward, by name), the worker, the jump to the result
  (`editor_open_at()`), messages through a callback of the mode.
- later: `code_intel` (backend per project, LSP server lifecycle, buffer
  sync), help card popover, signature help, the References tab, the
  symbol picker.

## 3. Keys

| Action | Notepad (VS Code) | Emacs (xref / eldoc) | Vim |
|---|---|---|---|
| Go to definition | `F12`, `Ctrl+Click` | `M-.`, `Ctrl+Click` | `gd`, `Ctrl-]`, `Ctrl+Click` |
| Go to declaration | `Ctrl+F12` | `C-u M-.` | `gD` |
| References | `Shift+F12` | `M-?` | `gr` |
| Back / forward | `Ctrl+Alt+-` / `Ctrl+Shift+-` | `M-,` / `C-M-,` | `Ctrl-O` / `Tab` (across files) |
| Help | mouse hover, `Ctrl+K Ctrl+I` | `C-h .` | `K` |
| Signature | automatic, `Ctrl+Shift+Space` | automatic in the echo area | automatic in Insert mode |
| Symbols of the file | `Ctrl+Shift+O` | `M-g i` | `:Symbols` |
| By name | — | — | `:tag {name}` |

Notes:
- `Alt+Left / Right` (panel focus) and `Ctrl+T` (terminal) are window keys:
  they are not used for back / forward.
- Vim: `gd` / `gD` keep their local meaning (first occurrence in the
  buffer) when the file is not code. `Ctrl-O` / `Tab` walk the engine's
  jump list in the buffer, then continue in the shared history across files.
- Emacs: `M-,` and `M-.` were aliases of `M-<` / `M->`; they become xref
  keys as in Emacs.
- Org-mode tabs are not concerned (no code language).

## 4. Phases

### Phase 0 — Built-in index, definition, history (no dependency)
- `code_scan`, `code_scan_c`, `code_index`, `jump_list` with tests.
- `code_nav`: definition and declaration lookups in a worker, jump, back,
  forward, fallback to the first occurrence in the buffer.
- Keys in the three modes, `Ctrl+Click`, `:tag`, help and README.

### Phase 1 — ctags
- Use `ctags --output-format=json` when installed; index cached in
  `app_cache_dir()`, refreshed on `APP_EVENT_FILES_CHANGED`.

### Phase 2 — LSP
- `lsp_protocol` + `code_intel`: `lsp.ini` in `app_config_dir()` (language
  to command, built-in defaults: `clangd`, `pylsp`, `rust-analyzer`,
  `gopls`, `typescript-language-server --stdio`, `bash-language-server
  start`), `initialize`, `didOpen` / `didChange` (full text, debounced) /
  `didClose`, definition, declaration, references, hover, signature help,
  document symbols. A missing server falls back silently.
- Help card (`GtkPopover`: signature highlighted with GtkSourceView,
  documentation rendered by `markdown`, location), signature help.

### Phase 3 — Results and polish
- References tab in the preview notebook: grouped by file, context line,
  `Enter` opens the location.
- Symbol picker (`AppDialog` with filter), highlight of the other
  occurrences, `Ctrl+Click` in the read-only preview, diagnostics.

## 5. Definition of done for a phase

- `make check` clean, new modules registered in the Makefile tests and in
  `tools/check_layers.sh`.
- A step in `tests/ui/run_ui_test.sh` for each new key, screenshots checked.
- `help.c` (`KEY_HELP`) and the README shortcut tables updated.
