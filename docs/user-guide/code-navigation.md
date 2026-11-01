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

- Pressing "definition" again on a definition goes to the next candidate;
  the message line says which one is shown and how many there are.
- The **References** tab, next to **Git Commits**, lists the uses grouped
  by file. `Enter` or a double click opens one; the cross closes the tab.
- The other uses of the name under the cursor are highlighted.
- `Ctrl+Click` also works in the preview of a source file.

## Where the answers come from

LookinFiles uses the best source it finds for the project (the git
repository of the file, else its folder), and falls back to the next one:

| Source | Quality | Needs |
|---|---|---|
| Language server | Exact definitions and references, documentation, errors and warnings | A server such as `clangd`, `pylsp`, `gopls`, `rust-analyzer` |
| Universal Ctags | Good definitions, signatures | `ctags` installed |
| Built-in index | Good guesses for C, C++, Java, C#, Python, Shell, JavaScript / TypeScript, Go, Rust, Lua, Ruby, Makefiles | Nothing |

The status bar shows the source used by the current tab: the server's name,
`ctags` or the built-in index.

With a language server, errors are underlined in red and warnings in
yellow, counted in the status bar, and explained when the mouse rests on
them.

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
`compile_commands.json` at the root of the project.

## Ctags

When [Universal Ctags](https://ctags.io) is installed, its index is used
and kept up to date in `~/.cache/lookinfiles/tags/`. It also adds
Objective-C, PHP, Perl, Kotlin, Swift, Scala, Dart, Haskell, Elixir,
Erlang, Julia, R and Tcl.
