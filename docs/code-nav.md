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
| 1 (foundation) | `code_symbol` (languages, word under the cursor), `code_scan`, `code_scan_c`, `code_scan_tex` (scanners; LaTeX outline), `code_outline_tree` (outline entries, Markdown headings, tree), `code_index` (project files, ranked lookup, workspace symbol search), `code_tags` (ctags), `lsp_protocol`, `lsp_edit` (text edit application, buffer identifier extraction), `code_lint` (built-in JSON/TOML/YAML/XML syntax diagnostics), `lsp_client`, `lsp_config`, `jump_list` |
| 3 (feature) | `code_nav` (lookups, jumps, history), `code_intel` (language servers per project), `code_help` (help cards, signature help, occurrences), `code_refs` (References tab), `code_problems` (Problems tab, diagnostic stepping), `code_outline` (outline of a tab: server, scanner or Markdown, in a worker), `code_symbols` (file symbol picker), `code_workspace_symbols` (project symbol picker), `code_kind_drop` (kind drop-down of both pickers), `code_complete` (autocompletion popover), `code_rename` (multi-file LSP rename), `editor_tagbar` (tagbar) |

## Backends

- **Built-in index**: C, C++, Java, C# (a state machine over comments,
  strings, preprocessor lines and braces), Python, Shell, JavaScript /
  TypeScript, Go, Rust, Lua, Ruby, Makefile (line regexes), LaTeX and
  BibTeX (`code_scan_tex`: labels, macros, environments, bibliography
  entries; `:` and `-` are word characters so `\ref{sec:a-b}` is one key).
  LaTeX does not use ctags (`code_scan_preferred()`): ctags skips its
  labels. Its outline without a server is headings (`\part` 1 to
  `\subparagraph` 7, beamer frames 8) with the symbols under the nearest
  heading.
- **ctags**: `ctags --output-format=json` when installed; index cached in
  `app_cache_dir()/tags/`. Each lookup compares the size and date of the
  files and re-tags the changed ones. Adds Objective-C, PHP, Perl, Kotlin,
  Swift, Scala, Dart, Haskell, Elixir, Erlang, Julia, R, Tcl.
- **LSP**: `lsp.ini` in `app_config_dir()` maps a language to a command;
  built-in defaults `clangd`, `pylsp`, `rust-analyzer`, `gopls`,
  `typescript-language-server --stdio`, `bash-language-server start`,
  `texlab` (LaTeX, BibTeX: language ids `latex` and `bibtex`).
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
| Symbols of the project | `Ctrl+Alt+O` | `M-g s` | `:WSymbols` |
| Problems panel | `Ctrl+Shift+M` | `M-x flymake-show-buffer-diagnostics` | `:Problems` |
| Next / previous diagnostic | `F8` / `Shift+F8` | `M-g n` / `M-g p` | `]d` / `[d` |
| Autocompletion | `Ctrl+Space` (and auto) | `C-M-i`, `M-/` (and auto) | `Ctrl+Space` (Insert, auto) |
| Rename symbol (LSP) | `Shift+F2`, `Ctrl+K Ctrl+R` | `M-x lsp-rename` | `:Rename` |
| Tagbar | `Ctrl+K Ctrl+O` | `M-x imenu-list` | `:TagbarToggle`, `:Tagbar` |
| By name | — | — | `:tag {name}` |

Notes:
- `Alt+Left / Right` (panel focus) and `Ctrl+T` (terminal) are window keys:
  they are not used for back / forward.
- Vim: `gd` / `gD` keep their local meaning (first occurrence in the
  buffer) when the file is not code. `Ctrl-O` / `Tab` walk the engine's
  jump list in the buffer, then continue in the shared history across files.
- Opening a reference from the References tab records where the lookup
  started (`gr`, `Shift+F12`, `M-?`): back returns there. The back key of
  the global mode also works in the list itself.
- `Ctrl+Click` also works in the read-only preview of source files.
- Symbols of the project also work outside editor tabs (`Ctrl+Alt+O`,
  Emacs `M-g s`): the project of the explorer's folder, every language,
  ctags or the built-in index (no language server without a file).
- Both symbol pickers (file and project; not for a Markdown file, whose
  entries are headings) have a drop-down of kinds beside their search
  field (every kind, or functions, methods, macros, types...; `Alt+K` /
  `Alt+Shift+K` from the field step through it). The kind is filtered
  before the results are capped (`code_index_search_symbols()`,
  `code_tags_index_search()`), on the server's answer with a language
  server; each picker keeps its choice for the next time. In the file
  picker the counts follow the filter text. Each row of the
  drop-down gives how many symbols of its kind match the query, whatever
  the kind chosen and the cap (`kind_counts` of
  `code_index_search_symbols()`, `code_tags_index_count()`), and `Alt+K`
  skips the kinds with none; the foot of the dialog names the keys.
- Org-mode tabs are not concerned (no code language).
- Tagbar: a `GtkPaned` in each editor tab (thin handle), shown or hidden
  per tab by its key (not saved); a new tab follows the Preferences
  (`tagbar=` in settings.ini); a layout that saved its tagbars, and the Preferences, set
  every tab, the current layout leaves them as they are; read again 600 ms after the last edit,
  only while shown; follows the cursor (last entry starting at or before its
  line); the rows the user collapsed stay collapsed across refreshes.
  In Vim mode it has its own status line (`outline_pane_set_footer()`,
  `editor_tagbar_set_vim_status()`), and the tab's Vim status line is in
  `tab->window`, beside it; the command line spans both, and the tagbar's
  search bar takes its place while searching (`outline_pane_set_search_host()`).
  The status line of the side without the focus has `vim-statusline-inactive`
  (grey badges).
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
  included); multi-file rename requires a running language server.
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
