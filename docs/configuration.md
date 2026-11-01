# Configuration

Settings and data are kept in `~/.config/lookinfiles/`
(`$XDG_CONFIG_HOME` is honoured):

| Path | Content |
|---|---|
| `bookmarks.xml` | Bookmarks and bookmark folders |
| `notes/` | Notebooks (one directory each), notes (Canvas notes are `<title>.canvas/` directories: `page.xml` and `media/`), `notes.xml` and the `.trash/` |
| `remotes.xml` | Remote machines added by hand (`~/.ssh/config` hosts are read directly) |
| `layouts.ini` | Saved workspace layouts and the default layout of new workspaces |
| `session.ini` | The workspaces of the last session (folders, expanded folders, selection, sort, open files), reopened by a launch without argument |
| `recent.ini` | Recent folders and files (`Alt+R`, and first in `Ctrl+P` when nothing is typed) |
| `lsp.ini` | Language server commands for code navigation (optional, see below) |
| `settings.ini` | Your preferences (written by the Preferences dialog, `Ctrl+,`, see [Preferences](#preferences)) |
| `theme.ini` | The chosen theme and your fonts (written by Preferences → Appearance, `Alt+T`) |
| `themes/` | Your themes, one `.ini` file each (see [Themes](#themes)) |

Remote machines are mounted under `~/.cache/lookinfiles/remotes/`.

## Preferences

`Ctrl+,` (or the gear button of the top bar) opens the Preferences dialog,
on the section shown last; `Alt+T` opens it on **Appearance**. **Apply**
keeps every change, **Cancel** restores them all.

| Section | Content |
|---|---|
| Appearance | Theme and fonts (see [Themes](#themes)) |
| Editor | Mode of new editor tabs (Notepad, Vim, Emacs; also the global mode of the status bar), line numbers and line wrap of new tabs, a vertical line at a column in every editor tab and in code previews (Vim's `colorcolumn`, colour `editor_ruler`) |
| Window | Window size at startup (**Use the current size** fills it in) |
| Code navigation | **Edit lsp.ini** opens `lsp.ini` in an editor tab, created with a commented example if missing (see [Language servers](#language-servers)) |

The Editor and Window values are kept in `settings.ini`; the command-line
options `-e` and `--size` win over them:

```ini
[Editor]
mode=vim
line-numbers=true
wrap=false
color-column=81    # default; 0: no line

[Window]
width=1600
height=900
```

## Language servers

Code navigation starts a language server when one is installed for the
file's language: `clangd` (C, C++, Objective-C), `pylsp` /
`pyright-langserver` / `jedi-language-server` (Python), `gopls`,
`rust-analyzer`, `typescript-language-server`, `bash-language-server`,
`lua-language-server`, `solargraph`, `jdtls`, `csharp-ls`, `intelephense`...
`lsp.ini` replaces a command or turns a server off (keys are the language ids:
`c`, `cpp`, `python`, `javascript`, `go`, `rust`...):

```ini
[servers]
c=clangd --background-index
python=pyright-langserver --stdio
rust=none
```

The status bar shows what serves the current editor tab: the server name,
`ctags` or `built-in index`.

## Themes

`Alt+T` opens the Preferences dialog on **Appearance** (also `Ctrl+,` or
the gear button of the top bar, then **Appearance**).
Selecting a theme applies it at once to the whole window, including open
editors, previews and terminals; **Apply** keeps it and **Cancel** goes back.
Three themes are built in: **Dark Modern** (the default), **Light Modern**
and **High Contrast Dark**.
The [`themes/`](../themes/) folder of the source tree has six more, one per
decade from the 1970s to the 2020s, to copy into your themes folder.

Your fonts apply over any theme: the dialog sets the interface font (menus,
panels, rendered documents) and the code font (file contents, code,
terminals), each a list of font names separated by commas and a size in
pixels. `Ctrl+=` / `Ctrl+-` make both larger or smaller, `Ctrl+0` returns to
the theme's sizes; the Notepad status bar shows the zoom.

A theme is an INI file in `~/.config/lookinfiles/themes/`. **Save As…** writes the
current look as a complete file (every colour, with its description as a
comment), and **Edit** opens one of your themes in an editor tab: saving the
file applies it again, or shows what is wrong with it. A theme can inherit
from another one and set only what differs:

```ini
[Theme]
name=Solar Light
# light GTK widgets and syntax base
dark=false
# values left out come from this theme
inherits=Light Modern

[Colors]
bg_view=#fdf6e3
bg_window=#eee8d5
accent=#268bd2
syn_keyword=#859900

[Fonts]
code=JetBrains Mono, DejaVu Sans Mono, monospace
code_size=14
```

Comments are lines starting with `#` (not allowed after a value). The colour names are the
roles of the palette (`src/palette.h`): surfaces (`bg_window`, `bg_view`,
`bg_bar`...), text (`fg`, `fg_text`, `fg_subtle`...), accent, Git, diff,
syntax (`syn_comment`, `syn_keyword`, `syn_string`...), editor highlights
and the 16 terminal colours; a file saved with **Save As…** lists them all.
A theme that cannot be read is skipped and reported on the terminal LookinFiles
was started from.

Toolbar icons are bundled in the binary, so they look the same with any icon
theme. If buttons show no icon, `LOOKIN_DEBUG_ICONS=1 ./lookin`
prints which icon file each button uses.
