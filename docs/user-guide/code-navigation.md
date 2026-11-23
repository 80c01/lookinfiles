# Code navigation

In source files, editor tabs can jump to where a name is defined, list where
it is used, and show what it is, in all three modes.

## The keys

| Action | Notepad | Emacs | Vim |
|---|---|---|---|
| Go to the definition | `F12`, `Ctrl+Click` | `M-.`, `Ctrl+Click` | `gd`, `Ctrl+]`, `Ctrl+Click` |
| Go to the declaration | `Ctrl+F12` | `C-u M-.` | `gD` |
| Come back / go forward again | `Ctrl+Alt+-` / `Ctrl+Shift+-` | `M-,` / `C-M-,` | `Ctrl+O` / `Tab` |
| Help card | `Ctrl+K Ctrl+I`, rest the mouse | `C-h .`, mouse | `K`, mouse |
| Signature of the call being typed | automatic, `Ctrl+Shift+Space` | in the echo area | automatic in Insert mode |
| References | `Shift+F12` | `M-?` | `gr` |
| Symbols of the file | `Ctrl+Shift+O` | `M-g i` | `:Symbols` |
| Symbols of the project | `Ctrl+Alt+O` | `M-g s` | `:WSymbols` |
| Problems panel (diagnostics) | `Ctrl+Shift+M` | `M-x flymake-show-buffer-diagnostics` | `:Problems` |
| Next / previous diagnostic | `F8` / `Shift+F8` | `M-g n` / `M-g p` | `]d` / `[d` |
| Autocompletion | `Ctrl+Space` (and auto) | `C-M-i`, `M-/` (and auto) | `Ctrl+Space` (Insert) |
| Rename symbol across files (LSP) | `Shift+F2`, `Ctrl+K Ctrl+R` | `M-x lsp-rename` | `:Rename` |
| Tagbar (symbols beside the text) | `Ctrl+K Ctrl+O` | `M-x imenu-list` | `:TagbarToggle` |

- Pressing "definition" again on a definition goes to the next candidate;
  the message line says which one is shown and how many there are.
- The **References** tab, next to **Git**, lists the uses grouped
  by file. `Enter` or a double click opens one; the cross closes the tab.
  Back (`Ctrl-O` in Vim, `M-,` in Emacs, `Ctrl+Alt+-` in Notepad), from
  the list or from the reference opened, returns to where the search
  started.
- The other uses of the name under the cursor are highlighted.
- `Ctrl+Click` also works in the preview of a source file.
- The symbols of the project can also be searched from the explorer:
  `Ctrl+Alt+O` (`M-g s` in Emacs mode) looks in the project of the
  current folder, in every language.
- In the symbols of the file and of the project, the list beside the search field keeps
  only one kind (functions, macros, types...), each with the number of
  symbols that match; `Alt+K` / `Alt+Shift+K` go to the next / previous
  kind that has some.

The **tagbar** shows the symbols of a code file beside the text, grouped by
kind (Functions, Types...) and nested in their struct or class, or the
headings of a Markdown file. It follows the cursor and is refreshed shortly
after each edit. A click or `Enter` goes to the symbol (back with the usual
key), `s` switches between grouping by kind and file order, `Esc` returns to
the text, `q` hides it; `Alt+Right` from the text goes into it, `Alt+Left` comes back (also the panel keys of the tab's mode: `Ctrl+W l` / `Ctrl+W h` in Vim, `C-x o`, `F6` / `Shift+F6`); its header shows an accent line while it has the focus. In it, the keys of the tab's mode move (`j` / `k`, `gg` / `G`, `Ctrl+F` / `Ctrl+B`, `h` / `l` close / open a row in Vim; `C-n` / `C-p`, `M-<` / `M->`, `C-v` / `M-v`, `C-b` / `C-f` in Emacs; the arrows, `Page Up` / `Page Down`, `Ctrl+Home` / `Ctrl+End` in every mode) and search the names, collapsed rows included, in a bar at its bottom (`/` then `n` / `N` in Vim, `C-s` / `C-r` in Emacs, `Ctrl+F` then `F3` / `Shift+F3` in Notepad; `Enter` or `Esc` stays on the match, `C-g` goes back); the preview tagbar takes the keys of the global mode. Each tab shows or hides its own (a new
tab follows the Preferences, Editor page), and its width is
shared by the tabs.

In a Markdown file, the symbol list and the tagbar show the headings.

In Vim mode, as in Vim, the tagbar and the text each have their own status
line: `Tagbar`, then `Name` (grouped by kind) or `Order` (file order) and
the file under the tagbar; the command line stays below both, and `/` in
the tagbar searches in it. As in airline, the status line of the side
without the focus is grey.

## Where the answers come from

LookinFiles uses the best source it finds for the project (the git
repository of the file, else its folder), and falls back to the next one:

| Source | Quality | Needs |
|---|---|---|
| Language server | Exact definitions and references, documentation, errors and warnings | A server such as `clangd`, `pylsp`, `gopls`, `rust-analyzer` |
| Universal Ctags | Good definitions, signatures | `ctags` installed |
| Built-in index | Good guesses for C, C++, Java, C#, Python, Shell, JavaScript / TypeScript, Go, Rust, Lua, Ruby, Makefiles | Nothing |

The bar at the bottom of an editor tab shows the source it uses: the server's name,
`ctags` or the built-in index.

With a language server, errors are highlighted in red and warnings underlined in
yellow, counted in the bar of the editor tab, listed in the **Problems** panel,
and explained when the mouse rests on them. For JSON, TOML, YAML and XML files,
the built-in parsers provide live syntax diagnostics automatically with no server
needed.

Without a server, references are whole-word matches (comments and strings
included), and when no definition is found the editor goes to the first
use of the name in the current function.

## Setting up a language server

Install the server for your language; LookinFiles starts it when you open
a file of that language. To change the command, or to turn a server off,
write `~/.config/lookinfiles/lsp.ini`:

```ini
[servers]
c=clangd --background-index
python=pyright-langserver --stdio
rust=none
```

See [Configuration](../configuration.md#language-servers) for the list of
servers it looks for. For C and C++, `clangd` works best with a
`compile_commands.json` at the root of the project. The
[Diagnostics tab](lookinfiles:diagnostics) shows which servers are
installed and how to install the others.

## Ctags

When [Universal Ctags](https://ctags.io) is installed, its index is used
and kept up to date in `~/.cache/lookinfiles/tags/`. It also adds
Objective-C, PHP, Perl, Kotlin, Swift, Scala, Dart, Haskell, Elixir,
Erlang, Julia, R and Tcl. The [Diagnostics tab](lookinfiles:diagnostics#ctags)
says whether it is found.

## Build and test tasks

`F7` builds the project of the focused editor tab's file (else of the
explorer's folder): the nearest folder above it holding a build file. The
tasks come from those files:

| Build file | Tasks |
|---|---|
| `Makefile` | `make -j<n>`, then `make -j<n> <target>` (the `.PHONY` targets, else the rules with a plain name) |
| `Cargo.toml` | `cargo build`, `check`, `test`, `clippy`, `run` (in the workspace's folder for a member crate) |
| `package.json` | `npm run <script>` for each script (`pnpm`, `yarn` or `bun` when their lock file is there) |
| `go.mod` | `go build ./...`, `go test ./...`, `go vet ./...` |
| `CMakeLists.txt` | `cmake --build build --parallel <n>` (a configured build folder), else `cmake -B build` |
| `meson.build`, `build.ninja` | `ninja -C build` and `meson test -C build`, else `meson setup build`; `ninja` |
| `pyproject.toml`, `setup.py`, `pytest.ini` | `pytest` |
| `.lookinfiles/tasks.toml` | the project's own tasks, listed first: any command, its folder, variables and the pattern of its messages (see [Project tasks](../configuration.md#project-tasks)) |

The builds use every processor: `<n>` is their number for `make` and
`cmake`; cargo, ninja, `meson test` and go use them all by themselves.

| Key | Action |
|---|---|
| `F7` | Run the task chosen last in this project (at first the first one of the list: `make`, `cargo build`, `npm run build`...); from the Build tab, the task it shows |
| `Shift+F7` | Choose the task in a list (type to filter, `Enter` runs it); it becomes the one `F7` runs in this project |
| `Ctrl+F7` | Stop the task and what it started (killed after three seconds) |
| `F8` / `Shift+F8` | Next / previous error or warning of the Build tab, opened in an editor tab (while the Build tab is open; otherwise the diagnostics of the file) |

The **Build** tab shows the messages above (errors, warnings and notes, in
the order of the output; `Enter` or a double click opens the place) and the
output below, as it comes. Its header says what runs, in which folder, the
counts and how long it took, with **Run Again** and **Stop** buttons. The
messages of gcc, clang, the linker, rustc and cargo, tsc, eslint, go,
pytest and Python tracebacks are recognized, and the folders `make -C` and
`ninja -C` enter are followed. One task runs at a time: another one stops
it. The messages that name a file are also underlined in the editor tabs,
counted and listed in the **Problems** tab (the program as their source;
it does not open by itself: `Ctrl+Shift+M` from an editor tab) and stepped
through with the keys of the diagnostics, until
the next task; on a line where the language server reports the same kind of
problem, only the server's is kept. The task gets no input, and files left unsaved are not saved first
(the message line says how many there are). Tasks do not run on remote
machines yet.
