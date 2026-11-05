# Troubleshooting

## A feature is missing

Some features need an optional library at build time. Install it, then
run `make` again:

| Symptom | Install (Debian / Ubuntu) |
|---|---|
| "Terminal panel disabled" | `libvte-2.91-gtk4-dev` |
| No syntax colours in code | `libgtksourceview-5-dev` |
| "PDF preview disabled" | `libpoppler-glib-dev` |
| SQLite databases show only file details | `libsqlite3-dev` |
| 7z, RAR, xz, zstd, ISO or RPM files are not listed | `libarchive-dev` |

One program is used at run time, without rebuilding:

| Symptom | Install |
|---|---|
| Remote machines do not connect ("sshfs Required") | `sshfs` |

## "cannot open display"

LookinFiles is a graphical program: it needs a desktop session (X11 or
Wayland). Over SSH, use `ssh -X` or run it on the machine's own screen.

## A remote machine does not connect

- Check that `ssh user@host` works in a terminal **without asking for a
  password**: LookinFiles cannot type one. Use an SSH key, and `ssh-agent`
  if the key has a passphrase.
- Check the host, user, port and remote directory with `F2` in the remote
  view.
- A machine from `~/.ssh/config` uses the settings written there; press
  `F5` after editing that file.

## Go to definition finds nothing, or the wrong thing

- Look at the bar of the editor tab: it names the source used (a language server,
  `ctags` or the built-in index). The built-in index only guesses.
- Install a language server for the language (`clangd`, `pylsp`,
  `gopls`...) or Universal Ctags; see
  [code navigation](code-navigation.md).
- For C and C++, give `clangd` a `compile_commands.json` at the root of the
  project.

## A key does not do what I expect

- Look at the global mode in the top bar: navigation keys depend on it
  (`j` / `k` only work in Vim mode).
- An editor tab can have its own mode: right-click its title to see it.
- Press `?` to see the keys of the current mode.
- Your desktop may take some keys for itself (`Alt+1`…`Alt+8`,
  `Ctrl+Alt+Arrows`); change the desktop shortcut in that case.

## The buttons have no icons

The icons are built into the program, so they should always show. To see
which icon file each button uses, start LookinFiles with
`LOOKIN_DEBUG_ICONS=1 ./lookinfiles`.

## My theme is not applied

A theme file that cannot be read is skipped, and the reason is printed on
the terminal LookinFiles was started from. When you edit a theme in an
editor tab (`Alt+T` → **Edit**), saving it shows the problem in the
window. See [themes](themes.md).

## Starting fresh

Settings and data are in `~/.config/lookinfiles/` (see
[Configuration](../configuration.md)). To forget the last session only,
delete `session.ini`; to go back to the default theme, delete `theme.ini`.
Keep `notes/` and `bookmarks.xml`: they hold your notes and bookmarks.
