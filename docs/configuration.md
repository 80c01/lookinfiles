# Configuration

Settings and data are kept in `~/.config/lookinfiles/`
(`$XDG_CONFIG_HOME` is honoured):

| Path | Content |
|---|---|
| `bookmarks.xml` | Bookmarks and bookmark folders |
| `notes/` | Notebooks (one directory each), notes (Canvas notes are `<title>.canvas/` directories: `page.xml` and `media/`), `notes.xml` and the `.trash/` |
| `remotes.xml` | Remote machines added by hand and remote view folders (`~/.ssh/config` hosts are read directly; one moved into a folder is saved here) |
| `layouts.ini` | Saved workspace layouts and the default layout of new workspaces |
| `sessions/<id>.ini` | One saved workspace per file, in the format of `session.ini` (workspace bookmarks: **Save Workspace as Bookmark…** in the bookmarks view); the bookmark points to it as `lookinfiles-session://<id>` and deleting it deletes the file |
| `searches/<id>.ini` | One saved Deep Search per file (search bookmarks: the bookmark button or tab menu in Deep Search); stores only the criteria (not the results) as `lookinfiles-search://<id>` and deleting the bookmark deletes the file |
| `session.ini` | The workspaces of the last session (folders, expanded folders, selection, sort, explorer view (files, bookmarks, notes, remote), open files and notes (text and Canvas), browser, Video and Jupyter tabs, and their notebooks and splits, panels and split sizes, terminal panes), reopened by a launch without argument |
| `recent.ini` | Recent folders and files (`Alt+R`, and first in `Ctrl+P` when nothing is typed) |
| `lsp.ini` | Language server commands for code navigation (optional, see below) |
| `tasks.ini` | The task chosen last in each project (`Shift+F7`), run by `F7`: a group per project folder |
| `jupyter.ini` | The Python chosen for the notebooks of a folder and of its subfolders (**Python Interpreter…** of a notebook's kernel menu): a group per folder, `python=<path>` |
| `settings.ini` | Your preferences (written by the Preferences dialog, `Ctrl+,`, see [Preferences](#preferences)) |
| `theme.ini` | The chosen theme and your fonts (written by Preferences → Appearance, `Alt+T`) |
| `themes/` | Your themes, one `.ini` file each (see [Themes](#themes)) |

A project can also define its own build and test tasks in
`.lookinfiles/tasks.toml` at its root (see [Project tasks](#project-tasks)).

Remote machines are mounted under `$XDG_RUNTIME_DIR/lookinfiles/remotes/`
(`/tmp/lookinfiles-<uid>/remotes/` without `$XDG_RUNTIME_DIR`), never under
`~/.cache`; mounts left by a crash are unmounted at the next start.

## Preferences

`Ctrl+,` (or the gear button of the top bar) opens the Preferences dialog,
on the section shown last; `Alt+T` opens it on **Appearance**. **Apply**
keeps every change, **Cancel** restores them all.

| Section | Content |
|---|---|
| Appearance | Theme and fonts (see [Themes](#themes)) |
| Editor | Mode of new editor tabs (Notepad, Vim, Emacs; also the global mode of the top bar), line numbers and line wrap of new tabs, the tagbar of the code and Markdown tabs (open ones too) and of the preview, the minimap of the editor tabs, the preview and Help (open tabs too), whether the editor tabs update the preview (open tabs too), a vertical line at a column in every editor tab and in code previews (Vim's `colorcolumn`, colour `editor_ruler`) |
| Window | Window size at startup (**Use the current size** fills it in) |
| Layouts | Terminal, preview and editor tagbar of the **Auto** layout (`Alt+L`): as they are, shown or hidden; the terminal's place: where it is, below or on the right (as `Ctrl+Shift+L` does) |
| Clock | The time of the top bar: shown or hidden (an icon then opens the timers while none runs), 24-hour or 12-hour, with or without the seconds; duration of top bar timer 1 at launch, in minutes (0: no timer; a new duration restarts it), and what it does when it expires: restart, stop at 0:00 or count on below 0:00 (the timers menu sets up all three until the next launch) |
| Code navigation | **Edit lsp.ini** opens `lsp.ini` in an editor tab, created with a commented example if missing (see [Language servers](#language-servers)) |
| Remote machines | **SSH connection**: automatic (the built-in libssh on Windows, the `ssh` program elsewhere), the built-in libssh, or the `ssh` program; taken at the next launch, the variable `LOOKINFILES_SSH_LIBSSH` (`1` or `0`) wins over it (see [remote-libssh.md](remote-libssh.md)) |

The Editor, Window, Layouts, Clock and Remote machines values are kept in `settings.ini`; the command-line
options `-e` and `--size` win over them:

```ini
[Editor]
mode=vim
line-numbers=true
wrap=false
color-column=81    # default; 0: no line
tagbar=false       # new code and Markdown tabs show the tagbar
minimap=true       # editor tabs, the preview and Help show the minimap
update-preview=true # saving, selecting or focusing an editor tab shows its file in the preview

[Tasks]
save-first=true    # F7 saves the edited files of the project before its task (default)

[LaTeX]
compile-on-save=true # saving a .tex file compiles its document, as F7 does (default)

[Preview]
tagbar=false       # previews with headings or symbols show the tagbar (o in the preview; new workspaces)

[Clock]
clock=true            # the top bar shows the time (default)
clock-format=24h      # 24h (default, 14:05) or 12h (2:05 PM)
clock-seconds=false   # the time shows the seconds
timers=true           # the top bar has timers (default; false: none)
timer-minutes=60      # top bar timer 1 started at launch (default; 0: no timer)
timer-expiry=restart  # timer 1 at 0:00: restart (default), stop or count-on

[Remote]
ssh=auto              # auto (default), libssh or openssh; read at launch

[Window]
width=1600
height=900

[Layouts]
auto-terminal=true # Auto shows the terminal (false: hides it; missing: as it is)
auto-preview=false # Auto hides the preview (true: shows it; missing: as it is)
auto-tagbar=false # Auto hides the tagbar of editor tabs (true: shows it; missing: as it is)
auto-terminal-place=below # Auto puts the terminal below (beside: on the right; missing: where it is)
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

## Project tasks

`F7` / `Shift+F7` run the tasks found from a project's build files (see
[Build and test tasks](user-guide/code-navigation.md#build-and-test-tasks)).
`.lookinfiles/tasks.toml` at the root of the project adds its own, listed
first; the file alone also makes its folder a project:

```toml
[[task]]
name = "fast tests"                   # required: shown in the lists
command = "./run_tests.sh --fast"     # through the shell (sh -c; cmd /c on Windows)
cwd = "tests"                         # folder, relative to the project (default: the project)
env = { CC = "clang", V = "1" }       # variables added to the environment
default = true                        # what F7 runs before any choice (else the first task)

[[task]]
name = "install"
args = ["make", "-C", "build", "install"]   # the program and its arguments, no shell
matcher = '^(?<file>[^:]+):(?<line>\d+): (?<severity>\w+): (?<message>.*)$'
```

A task has a `name` and either `command` or `args`. `matcher` is a regular
expression (PCRE, as GLib's) tried on each line of the output before the
built-in ones: the groups `file` and `line` are required, `column`,
`severity` (`error`, `warning`, `note`...; an error when missing) and
`message` (the whole line when missing) are optional; relative files are
read from the task's folder. The file is a small part of TOML: comments,
`[[task]]` tables, strings (`"..."` with escapes, `'...'` as written),
`true` / `false`, arrays of strings (also over several lines) and inline
tables of strings. A mistake (unknown key, missing command, bad matcher...)
is said on the message line with its line number, and the file's tasks are
left out until it is fixed; the others are still offered.

## Themes

`Alt+T` opens the Preferences dialog on **Appearance** (also `Ctrl+,` or
the gear button of the top bar, then **Appearance**).
Selecting a theme applies it at once to the whole window, including open
editors, previews and terminals; **Apply** keeps it and **Cancel** goes back.
Ten themes are built in: **Dark Modern** (the default), **Light Modern**,
**High Contrast Dark**, six more, one per decade from the 1970s to the
2020s, and **Dark LightRed** (the files of the [`themes/`](../themes/) folder of the source tree,
bundled into the binary; copy one into your themes folder to start your own).

Your fonts apply over any theme: the dialog sets the interface font (menus,
panels, rendered documents) and the code font (file contents, code,
terminals), each a list of font names separated by commas and a size in
pixels. `Ctrl+=` / `Ctrl+-` make both larger or smaller, `Ctrl+0` returns to
the theme's sizes; the Notepad status bar shows the zoom.

A theme is an INI file in `~/.config/lookinfiles/themes/`. **Save As…** writes the
current look as a complete file (every colour, with its description as a
comment), **From Image…** writes one from the colours of an image (the one
selected in the explorer by default; see `image_theme`), and **Edit** opens one of your themes in an editor tab: saving the
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
roles of the palette (`src/theme/palette.h`): surfaces (`bg_window`, `bg_view`,
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
standard icons). Three sets are built in: **desktop** (pixel icons of a 1990s
grey desktop; `themes/1990s-desktop.ini` uses it) and **textmode** (icons
drawn like code page 437 characters, in the CGA colours;
`themes/1980s-text-mode.ini` uses it) and **phosphor** (the same drawings in
green phosphor intensities; `themes/1970s-phosphor.ini` uses it).

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
generated by `tools/gen_desktop_icons.py`, `tools/gen_textmode_icons.py` and
`tools/gen_phosphor_icons.py`, a good
starting point. Changing the
theme, or saving its file in an editor tab, applies the new icons at once.
