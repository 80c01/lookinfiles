# Diagnostics

The **Diagnostics** tab lists what LookinFiles can use on this computer
and what is missing, with the command that installs each missing piece.
[Open the Diagnostics tab](lookinfiles:diagnostics), or open it from the
**+** button at the right end of the tab bar: **+** → **Diagnostics**.

## What it checks

- **Built In**: the optional libraries this copy of LookinFiles was built
  with (embedded terminal, syntax highlighting, PDF preview, databases,
  archives, browser tabs...). A library that is missing here cannot be
  added afterwards: install the development package shown, then build
  LookinFiles again (`make`), or use a package of LookinFiles built with it.
- **Programs**: the programs LookinFiles runs when a feature needs them,
  looked for in `PATH`: Universal Ctags (code outline, Go to Definition),
  LaTeX (`latexmk`, `tectonic` or `pdflatex`), Ghostscript (PostScript and
  EPS preview), `dvips` (DVI preview), `ssh` (remote machines), Python and
  its `jupyter_client` and `ipykernel` modules (running notebook cells).
  These work as soon as they are installed, without restarting.
- **Language Servers**: the server of each language (see
  [code navigation](code-navigation.md)). Those of the languages of the
  folder shown come first; **Show N other languages** unfolds the others.

A check that passes shows what was found (the program's path); a missing one
shows the install command for the package manager of this system (`apt`,
`dnf`, `pacman`, `zypper`, `apk` or Homebrew, read from `/etc/os-release`)
and a **Copy** button. Some programs are not packaged everywhere: the
command is then the one of their own installer (`pipx`, `npm`, `go`,
`rustup`...). LookinFiles never runs an install command itself.

Elsewhere in this guide, a link to the Diagnostics tab opens it on the
check of the feature described.

The header names the system and how many checks are missing. **Check
Again** (the refresh button) runs the checks again after you installed
something.

| Key | Action |
|---|---|
| `Ctrl+W` | Close the Diagnostics tab (`Ctrl+W c` in Vim mode) |
| Middle click on the tab | Close it |

## Notebooks and virtual environments

The Python check uses the `python3` of `PATH`. A notebook whose folder has
a virtual environment runs its kernel there (see the notebooks' kernel
menu): install `jupyter_client` and `ipykernel` in that environment too
(`pip install jupyter_client ipykernel` with it activated).
