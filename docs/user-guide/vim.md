# Vim mode

Vim mode is a Vim engine with the real grammar, not a handful of
shortcuts: `["x][count]operator[count]motion`, text objects, registers,
marks, the dot repeat, macros, Visual block and Ex commands on ranges.

To use it everywhere, start with `lookinfiles -e vim` or pick **Vim** in the
status bar; the explorer and the preview then also move with `j` / `k`,
`gg` / `G`, `/`, `n` / `N`. To use it in one tab only, right-click the tab
title and pick **Vim**.

## The status line

At the bottom of the tab: the mode badge (NORMAL, INSERT, VISUAL...), the
keys typed so far, `recording @x` while a macro records, and the ruler.
`:`, `/` and `?` open the command line, with:

- history: `Up` / `Down`;
- completion of commands and `:set` options: `Tab` / `Shift+Tab` (all
  matches are listed, like Vim's wildmenu);
- `Ctrl+R x` inserts register `x`.

## What works

| Area | Supported |
|---|---|
| Operators | `d` `c` `y` `>` `<` `=` `g~` `gu` `gU` `g?` `gq` `gw`, doubled for lines (`dd`, `gUU`) |
| Motions | Words, line ends, `f` `t` `F` `T` `;` `,`, `%`, paragraphs, sentences, `H` `M` `L`, `[[` `]]`... |
| Text objects | `iw` `aw`, `is` `as`, `ip` `ap`, brackets, quotes, tags (`it` `at`) |
| Registers | `"a`–`"z` (`"A` appends), `"0`, `"1`–`"9`, `"-`, `"_`, `"+` / `"*` (system clipboard, also the default) |
| Visual | `v` `V` `Ctrl+V`, `gv`, `o`; `I` / `A` / `c` on every line of a block |
| Undo | `u` / `Ctrl+R`, one whole command per step |
| Marks and jumps | `m{a-z}`, `` `a `` / `'a`, `Ctrl+O` / `Tab`, `:marks`, `:jumps` |
| Macros | `qa` … `q`, `@a`, `@@`, `@:` |
| Search | `/` `?` with Vim regular expressions, incremental, all matches highlighted with a `[n/total]` count; `*` `#`, `:noh` |
| Ex | `:s`, `:g` / `:v`, `:d`, `:y`, `:m`, `:t`, `:normal`, `:sort`, `:retab`, `:center`... on ranges (`%`, `'<,'>`, `/pat/`...) |
| Files and tabs | `:w`, `:wq`, `:x`, `ZZ`, `:q!`, `:e`, `:saveas`, `gt` / `gT`, `:tabn` / `:tabp` |

## Options

`:set` knows `ignorecase`, `smartcase`, `hlsearch`, `incsearch`,
`wrapscan`, `expandtab`, `autoindent`, `shiftwidth`, `tabstop`,
`softtabstop`, `textwidth`, `clipboard`, `number` and `wrap`, with the
`no` / `inv` / `!` / `?` / `+=` forms.

Options are not read from a `.vimrc`: set them in the tab with `:set`.

## Code navigation

`gd` or `Ctrl+]` goes to the definition, `gD` to the declaration, `K`
shows a help card, `gr` lists the references, `:Symbols` the symbols of
the file, `:tag name` finds a name. `Ctrl+O` / `Tab` come back, first in
the file then across files. In files that are not code, `gd` keeps its Vim
meaning (first occurrence). See [code navigation](code-navigation.md).

## Window keys

`Ctrl+W w` / `Ctrl+W W` move the focus to the next / previous panel, and
`Ctrl+W c` closes the tab. Vim tabs keep `Ctrl+P` for Vim, so the file
finder is not available from them with that key: use it from the explorer
or the preview.

Every key is listed in the
[keyboard reference](../keyboard.md#editor-tabs) (Vim mode section).
