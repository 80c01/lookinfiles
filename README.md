# LookinFiles — look in your files

A file explorer that shows what is inside your files. Select a file and see it
rendered right away: code, Markdown, PDF, images, office documents (Word,
Excel, PowerPoint), archives, databases and many more. Also play videos. Edit
text files and run commands next to it, without leaving the window. Written in
C and GTK 4 for Linux and Windows, with a VS Code look and a keyboard-first design.

![LookinFiles showing a Markdown preview](docs/screenshot.png)

## Features

- **Preview almost any file**, with no external tool: source code with syntax
  highlighting, Markdown, Org-mode, PDF, images, video, Office documents,
  mail and mailboxes, archives, databases, certificates, ELF binaries, and a
  hex dump for the rest.
- **Edit in place**: tabs you can split in every direction, each in Notepad,
  Emacs or Vim mode (with a full Vim grammar), Org-mode editing, and go to
  definition and references (built-in index, ctags or a language server).
- **Browser tabs for web development** (needs WebKitGTK): open a page, a local
  HTML file or a dev server in a tab of the preview panel, beside your editor
  and terminal. Press `F5` to reload after saving.
- **Terminal built in** (VTE on Linux, ConPTY on Windows): shell panes next to the preview and the
  editors.
- **Find fast**: fuzzy file finder (`Ctrl+P`) and search in files
  (`Ctrl+Shift+F`).
- **Git at a glance**: status badges in the tree, change bars in the gutter,
  a commit history tab.
- **Four explorer views**: files, bookmarks (folders, files, web pages and
  saved workspaces), notes (plain text, Markdown, Org) and SSH machines
  browsed over SSHFS.
- **Workspaces**: up to eight, each with its own folder, panels, editors and
  terminals, restored at the next launch. Save one as a **bookmark** to
  reopen that whole session later, in a new workspace.
- **Keyboard first**: the same key for the same action in every view, and a
  cheat sheet on `?`.
- **Themes and fonts**: Dark Modern, Light Modern, High Contrast Dark, six
  decade themes, or your own INI file.

## Quick start

```bash
sudo apt install build-essential pkg-config libgtk-4-dev libgit2-dev
make
./lookinfiles .
```

Press `F1` for the user guide, `?` for the shortcuts, `Alt+Left` / `Alt+Right` to move between
panels, `Ctrl+Q` to quit.

## Installation

### Requirements

| Component | Package (Debian / Ubuntu) | Needed for |
|---|---|---|
| GTK 4.12 or newer | `libgtk-4-dev` | Required |
| libgit2 1.1 or newer | `libgit2-dev` | Required (status badges, change bars, Git Commits tab) |
| C compiler, make, pkg-config | `build-essential pkg-config` | Required |
| VTE for GTK 4 | `libvte-2.91-gtk4-dev` | Embedded terminal |
| GtkSourceView 5 | `libgtksourceview-5-dev` | Syntax highlighting |
| poppler-glib | `libpoppler-glib-dev` | PDF preview |
| SQLite | `libsqlite3-dev` | Database preview |
| libarchive | `libarchive-dev` | 7z, RAR, xz, zstd, ISO, RPM listings |
| WebKitGTK 6.0 | `libwebkitgtk-6.0-dev` | Browser tabs |
| SSHFS | `sshfs` | Remote machines (run time) |

Everything below the first three rows is optional: the Makefile detects what
is installed, and a missing feature shows a notice instead. `make deps`
lists what was found and the package to install for each missing library;
the same list is printed after each build.

To install every optional library at once:

```bash
sudo apt install libvte-2.91-gtk4-dev libgtksourceview-5-dev \
  libpoppler-glib-dev libsqlite3-dev libarchive-dev libwebkitgtk-6.0-dev \
  sshfs
```

### Build

```bash
make         # creates ./lookinfiles (objects in build/)
make clean   # removes both
```

The default flags are `-O2 -g`. `CFLAGS` and `LDFLAGS`, given on the
command line or in the environment, replace them; `CFLAGS_EXTRA` adds to
them. Changing flags rebuilds everything.

```bash
make CFLAGS="-O0 -g"                   # debug build
make CFLAGS_EXTRA=-Werror              # fail on any warning
make CFLAGS_EXTRA=-fsanitize=address   # AddressSanitizer
```

### Install

```bash
sudo make install     # into /usr/local (binary, menu entry, icon)
sudo make uninstall
make install PREFIX=/usr DESTDIR=pkgroot   # staging directory for packages
make dist             # lookinfiles-<version>.tar.gz of the last commit
```

### Windows Build

LookinFiles builds natively on Windows using MSYS2 UCRT64:

```bash
make             # creates lookinfiles.exe
make windows-zip # packages build/lookinfiles-<version>-windows-x86_64.zip
make windows-exe # the same in one file: build/lookinfiles-<version>-windows-x86_64.exe
```

See [packaging/windows/README.md](packaging/windows/README.md) for prerequisites and setup details.

## Usage

```bash
./lookinfiles                     # reopen the last session (else the current directory)
./lookinfiles /path/to/directory  # start in a directory
./lookinfiles path/to/file.txt    # edit a file
./lookinfiles --editor vim        # mode of new editor tabs: notepad, emacs or vim (-e)
./lookinfiles --note Work/Ideas   # open or create a note (-n)
./lookinfiles --size 1920x1080    # window size (-s)
./lookinfiles --fullscreen        # (-f)
./lookinfiles --help              # (-h)
./lookinfiles --version           # (-V)
```

The session (workspaces, folders, open files and where they were, panels, split sizes, terminal panes) is saved when LookinFiles quits.

### Essential shortcuts

| Key | Action |
|---|---|
| `F1` | User guide (Help tab) |
| `?` | Cheat sheet with every shortcut of the current mode |
| `Ctrl+P` | Go to a file of the project |
| `Ctrl+Shift+F` | Search in files |
| `Enter` | Open the selected file in an editor tab |
| `m` | Rendered / raw preview |
| `a` / `F2` / `d` / `u` | New file / rename / trash / undo |
| `Ctrl+T` | Terminal pane |
| `Alt+1` … `Alt+8` | Switch workspace |
| `Ctrl+,` | Preferences |
| `Alt+T` | Themes and fonts |
| `Ctrl+Q` | Quit |

The full reference, per view and per editor mode (Notepad, Emacs, Vim,
Org-mode), is in [docs/keyboard.md](docs/keyboard.md).

## Documentation

| Guide | Content |
|---|---|
| [User guide](docs/user-guide/README.md) | How to use every feature, one topic per page |
| [Keyboard reference](docs/keyboard.md) | Every shortcut, by panel and editor mode |
| [Previews](docs/previews.md) | Supported formats and their limits |
| [Code navigation](docs/code-nav.md) | Definitions, references, language servers |
| [Configuration and themes](docs/configuration.md) | `~/.config/lookinfiles/`, `lsp.ini`, writing a theme |

## Configuration

Settings live in `~/.config/lookinfiles/` (`$XDG_CONFIG_HOME` is honoured).
Most of them are written by the application itself (`Ctrl+,` opens the
Preferences); the two files you may edit by hand are:

- `themes/*.ini`: your themes (`Alt+T` → **Save As…** writes a complete one
  to start from; the six decade themes of [`themes/`](themes/) are built in).
- `lsp.ini`: which language server to start per language (Preferences →
  Code navigation → **Edit lsp.ini** opens it).

See [docs/configuration.md](docs/configuration.md) for every file and the
theme format.

## Contributing

Contributions are welcome. Read [docs/architecture.md](docs/architecture.md)
and [docs/ui-conventions.md](docs/ui-conventions.md) first, then:

```bash
make check     # zero warnings, unit tests, module layering rules
make ui-test   # fast headless UI smoke test (needs xvfb, xdotool)
make ui-shots  # full UI scenario with screenshots (also needs imagemagick)
```

Keep commits focused, with a short imperative subject. When a shortcut
changes, update `src/help.c` and [docs/keyboard.md](docs/keyboard.md)
together. Release history is in [CHANGELOG.md](CHANGELOG.md).

## License

LookinFiles is released under the MIT License (see [LICENSE](LICENSE)).
The bundled toolbar icons come from
the GNOME Adwaita icon theme and keep their own license (CC-BY-SA 3.0 or
LGPL 3, and CC-BY-SA 4.0), and the file-type icons come from Seti UI (MIT);
see [icons/COPYING](icons/COPYING).

The PDF preview uses poppler-glib, which is GPL: a binary built with it and
distributed must follow the GPL terms. The other libraries (GTK, VTE,
GtkSourceView, WebKitGTK) are LGPL.

LookinFiles is an independent project, not affiliated with or endorsed by
Microsoft. "VS Code" and "Visual Studio Code" are Microsoft trademarks, used
here only to describe the look.
