# Code navigation

Editor tabs follow references in source files (functions, variables, types,
macros, fields) and show what a symbol is (declaration, signature,
documentation), with the same features in the three editor modes (Notepad,
Emacs, Vim). The modules are described in
[architecture.md](architecture.md).

## Rules

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

   The bar of the editor tab names the backend in use (`clangd`, `ctags`, `basic`).
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

## Modules

| Layer | Modules |
|---|---|
| 1 (foundation) | `code_symbol` (languages, word under the cursor), `code_scan`, `code_scan_c` (scanners), `code_outline_tree` (outline entries, Markdown headings, tree), `code_index` (project files, ranked lookup), `code_tags` (ctags), `lsp_protocol`, `lsp_client`, `lsp_config`, `jump_list` |
| 3 (feature) | `code_nav` (lookups, jumps, history), `code_intel` (language servers per project), `code_help` (help cards, signature help, occurrences), `code_refs` (References tab), `code_outline` (outline of a tab: server, scanner or Markdown, in a worker), `code_symbols` (symbol picker), `editor_tagbar` (tagbar) |

## Backends

- **Built-in index**: C, C++, Java, C# (a state machine over comments,
  strings, preprocessor lines and braces), Python, Shell, JavaScript /
  TypeScript, Go, Rust, Lua, Ruby, Makefile (line regexes).
- **ctags**: `ctags --output-format=json` when installed; index cached in
  `app_cache_dir()/tags/`. Each lookup compares the size and date of the
  files and re-tags the changed ones. Adds Objective-C, PHP, Perl, Kotlin,
  Swift, Scala, Dart, Haskell, Elixir, Erlang, Julia, R, Tcl.
- **LSP**: `lsp.ini` in `app_config_dir()` maps a language to a command;
  built-in defaults `clangd`, `pylsp`, `rust-analyzer`, `gopls`,
  `typescript-language-server --stdio`, `bash-language-server start`.
  Full-text sync (edits sent after a 600 ms pause), definition,
  declaration, references, hover, signature help, document symbols,
  diagnostics (underlined, counted in the bar of the editor tab, explained in help
  cards). A missing server falls back silently; a failed one is retried
  after a minute. The server's signature is shown as Markdown.
- **Help without a server**: the declaration line (or ctags signature) and
  the comment above it (`code_scan_doc_comment()`).

## Keys

| Action | Notepad (VS Code) | Emacs (xref / eldoc) | Vim |
|---|---|---|---|
| Go to definition | `F12`, `Ctrl+Click` | `M-.`, `Ctrl+Click` | `gd`, `Ctrl-]`, `Ctrl+Click` |
| Go to declaration | `Ctrl+F12` | `C-u M-.` | `gD` |
| References | `Shift+F12` | `M-?` | `gr` |
| Back / forward | `Ctrl+Alt+-` / `Ctrl+Shift+-` | `M-,` / `C-M-,` | `Ctrl-O` / `Tab` (across files) |
| Help | mouse hover, `Ctrl+K Ctrl+I` | `C-h .` | `K` |
| Signature | automatic, `Ctrl+Shift+Space` | automatic in the echo area | automatic in Insert mode |
| Symbols of the file | `Ctrl+Shift+O` | `M-g i` | `:Symbols` |
| Tagbar | `Ctrl+K Ctrl+O` | `M-x imenu-list` | `:TagbarToggle`, `:Tagbar` |
| By name | — | — | `:tag {name}` |

Notes:
- `Alt+Left / Right` (panel focus) and `Ctrl+T` (terminal) are window keys:
  they are not used for back / forward.
- Vim: `gd` / `gD` keep their local meaning (first occurrence in the
  buffer) when the file is not code. `Ctrl-O` / `Tab` walk the engine's
  jump list in the buffer, then continue in the shared history across files.
- `Ctrl+Click` also works in the read-only preview of source files.
- Org-mode tabs are not concerned (no code language).
- Tagbar: a `GtkPaned` in each editor tab (thin handle), shown or hidden
  per tab by its key (not saved); a new tab follows the Preferences
  (`tagbar=` in settings.ini); a layout that saved its tagbars, and the Preferences, set
  every tab, the current layout leaves them as they are; read again 600 ms after the last edit,
  only while shown; follows the cursor (last entry starting at or before its
  line); the rows the user collapsed stay collapsed across refreshes.
  A language server that has not answered within 2.5 s (starting,
  indexing, hung) is not waited for: the built-in scanner fills it.
  The widget is `outline_pane`, shared with the preview tagbar
  (`preview_tagbar`, docs/previews.md) and the tagbar of the Jupyter
  notebook tabs (`ipynb_tagbar.c`: the headings of the Markdown cells,
  docs/keyboard.md).
  Its list takes the navigation keys (`nav_keys`) of the style its owner
  gives (the tab's mode, the global mode for the preview): moving, and a
  search bar at its bottom over every name, collapsed rows included (a
  match opens the rows above it); panel and tab keys go back to the owner
  (`focus_route_panel_action()`).

## Known limits

- References without a server are whole-word matches (comments and strings
  included); workspace-wide symbol search is not offered.
- Built-in index: no scopes or types, so `x->name` offers every `name`
  field of the project (best first: same file, same directory). Local
  variables come from the "first use in this scope" fallback.
- Built-in index, C: only the first branch of `#if / #else` is read;
  macro-generated symbols are not seen.
- Built-in index: no cache, each lookup reads the project again (a few ms
  on this repository, bounded on large ones; a partial search is reported).

## Adding a feature: definition of done

- `make check` clean, new modules registered in the Makefile tests and in
  `tools/check_layers.sh`.
- A step in `tests/ui/run_ui_test.sh` for each new key, screenshots checked.
- `help.c` (`KEY_HELP`) and `docs/keyboard.md` updated.
