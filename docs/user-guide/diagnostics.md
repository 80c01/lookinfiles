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
- **Compilers and Build Tools**: what the [build tasks](code-navigation.md#build-and-test-tasks) (`F7`)
  and your own builds use: the C and C++ compilers (`gcc`, `clang`), Make,
  CMake, Meson and Ninja, Rust (`cargo`, `rustc`), Go, Java (`javac`), .NET
  and Node.js (`node`, `npm`). Each one found shows its version and path
  (every compiler found: `gcc 14.2.0 (/usr/bin/gcc) · clang 19.1.7
  (/usr/bin/clang)`). A missing one is listed only when the folder shown
  has files of its language (a `Makefile`, `.rs` files...); **Show N other
  tools** unfolds the others.
- **Language Servers**: the server of each language (see
  [code navigation](code-navigation.md)). Those of the languages of the
  folder shown come first; **Show N other languages** unfolds the others.

A check that passes shows what was found (the program's path); a missing one
shows the install command for the package manager of this system (`apt`,
`dnf`, `pacman`, `zypper`, `apk` or Homebrew, read from `/etc/os-release`)
and a **Copy** button. **Open in Terminal** opens a terminal tab with the
command typed at its prompt, not run: read it, then press `Enter` (it may
ask for your password, through `sudo`). For a remote machine checked from a
workspace of this computer, the line typed is `ssh -t <machine> '<command>'`.
Some programs are not packaged everywhere: the
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

## Remote machines

When you have [remote machines](remote.md), a list in the header chooses
the machine checked: **This computer** or one of them (the machine of the
workspace comes first). It shows which SSH transport the machine uses
(`libssh`, shared OpenSSH master, or separate OpenSSH connections). To run
its program checks, the remote machine must be connected; its checks are
those LookinFiles needs there: Python and the kernel modules for its
notebooks, `git` for the Git tab, `grep` for Search in Files, GNU `find`
(Linux) for Deep Search, Disk Usage and the Git tab, the compilers and
build tools (for tasks run there), and the language servers. The install commands are those of that machine's system: run them
there, in a terminal connected to it.

## Notebooks and virtual environments

The Python check uses the `python3` of `PATH`. A notebook whose folder has
a virtual environment runs its kernel there (see the notebooks' kernel
menu): install `jupyter_client` and `ipykernel` in that environment too
(`pip install jupyter_client ipykernel` with it activated).
