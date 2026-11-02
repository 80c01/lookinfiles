# Configuration

Settings and data are kept in `~/.config/lookinfiles/`
(`$XDG_CONFIG_HOME` is honoured):

| Path | Content |
|---|---|
| `bookmarks.xml` | Bookmarks and bookmark folders |
| `notes/` | Notebooks (one directory each), notes (Canvas notes are `<title>.canvas/` directories: `page.xml` and `media/`), `notes.xml` and the `.trash/` |
| `remotes.xml` | Remote machines added by hand and remote view folders (`~/.ssh/config` hosts are read directly; one moved into a folder is saved here) |
| `layouts.ini` | Saved workspace layouts and the default layout of new workspaces |
| `session.ini` | The workspaces of the last session (folders, expanded folders, selection, sort, open files and their notebooks and splits, panels and split sizes, terminal panes), reopened by a launch without argument |
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
| Editor | Mode of new editor tabs (Notepad, Vim, Emacs; also the global mode of the top bar), line numbers and line wrap of new tabs, a vertical line at a column in every editor tab and in code previews (Vim's `colorcolumn`, colour `editor_ruler`) |
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
# icon set (see Icon sets below); "default" for the standard icons
icons=default

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
`bg_bar`...), text (`fg`, `fg_text`, `fg_subtle`...), accent, Git, file-type
icons (`icon_blue`, `icon_purple`...), diff,
syntax (`syn_comment`, `syn_keyword`, `syn_string`...), editor highlights
and the 16 terminal colours; a file saved with **Save As…** lists them all.
A theme that cannot be read is skipped and reported on the terminal LookinFiles
was started from.

Toolbar icons are bundled in the binary, so they look the same with any icon
theme. If buttons show no icon, `LOOKIN_DEBUG_ICONS=1 ./lookin`
prints which icon file each button uses.

### Icon sets

A theme can also change the icons: `icons=<set>` in its `[Theme]` group picks
an icon set, which replaces the standard icons of buttons, tabs and file lists
at once (a theme inherits its parent's set; `icons=default` goes back to the
standard icons). Two sets are built in: **desktop** (pixel icons of a 1990s
grey desktop; `themes/1990s-desktop.ini` uses it) and **textmode** (icons
drawn like code page 437 characters, in the CGA colours;
`themes/1980s-text-mode.ini` uses it).

Your own sets are folders in `~/.config/lookinfiles/icons/<set>/` (a set there
replaces a built-in one of the same name, unless it has no `lookin-set-*.svg`
file). Each file is an SVG named after the
icon it replaces, with the prefix `lookin-set-`:

| File | Drawn |
|---|---|
| `lookin-set-<icon>-symbolic.svg` | In the text colour (single-colour shapes, like the standard icons) |
| `lookin-set-<icon>.svg` | As it is, with its own colours |

`<icon>` is the name of a file of `icons/scalable/actions/` without
`lookin-` and `-symbolic`: `edit-delete`, `folder-new`, `window-close`
for buttons, `folder`, `user-trash`, `network-server` for the glyphs of the
explorer and the previews, `ft-folder`, `ft-default`, `ft-python`... for file
types (these keep the colour of their type when symbolic). An icon the set
does not have keeps the standard look, so a set can be as small as you like.
Use a 16x16 view box and no `transform`. The icons of the built-in set are
generated by `tools/gen_desktop_icons.py` and `tools/gen_textmode_icons.py`, a good
starting point. Changing the
theme, or saving its file in an editor tab, applies the new icons at once.
