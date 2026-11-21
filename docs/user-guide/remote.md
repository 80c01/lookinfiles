# Remote machines

The remote view browses the files of SSH machines as if they were local:
LookinFiles mounts them with SSHFS, so the explorer, the preview, the
editor and the search all work on them. Open it with `Ctrl+Shift+R` (again
to go back to the files) or the server button at the top of the explorer.

## Requirements

- `sshfs` installed (`sudo apt install sshfs`; Fedora `fuse-sshfs`). Without
  it, connecting explains how to install it.
- The OpenSSH client (`ssh`). A password or a key passphrase, when ssh needs
  one, is asked in a dialog of LookinFiles; an SSH key with `ssh-agent`
  spares you the question.

## The list of machines

The view lists:

- every `Host` of your `~/.ssh/config` (`F5` reads the file again);
- the machines you added by hand.

To add one, press `a` and fill in **Friendly Name**, **Host / IP Address**
(required), **User**, **Port** (22 by default) and **Remote Directory** (`~`
by default). **Save & Connect** connects right away.

## Using a machine

| Key | Action |
|---|---|
| `Enter` or `c` | Connect and browse the machine in the files view |
| `c` on a connected machine | Disconnect |
| `t` | Open an SSH terminal to the machine |
| `F2`, `r` or `e` | Edit the machine |
| `d` or `Delete` | Delete it from the list |
| `F5` | Read `~/.ssh/config` again |

While you browse a connected machine, the breadcrumbs start with a server icon and
the machine's name, and the **Terminal** button of the top bar opens an SSH
terminal to it instead of a local shell.

Jupyter notebooks of the machine run their kernel on the machine itself
(see [The editor](editor.md)), through the connection opened for the files.

Paths on the machine are written as on the machine, `name:path`: the
**New File** and **New Folder** dialogs show `server:~/projects` (not the
local mount point), and so do the notebook **Save** dialog, the Emacs
`C-x C-w` prompt and the tab tooltips. Type paths the same way (`server:~/a`,
or `server:docs` from the **Remote Directory**); paths outside the remote
directory are refused.

The connection opened for the files is kept while you browse (OpenSSH
multiplexing; not on Windows) and closed when you disconnect or quit. SSH
terminals open connections of their own: one that asks for a password asks
in the terminal.

Machines are mounted under `$XDG_RUNTIME_DIR/lookinfiles/remotes/`
(usually `/run/user/1000/…`; without `$XDG_RUNTIME_DIR`,
`/tmp/lookinfiles-<uid>/remotes/`), a private folder that cache cleaners and
backups leave alone. If LookinFiles stops without disconnecting (a crash), the
next start unmounts what it left mounted; nothing on the machine is touched.
Machines added by hand are saved in `~/.config/lookinfiles/remotes.xml`.

## Speed

Every file read goes over the network, so large folders and previews of
big files are slower than on a local disk. Git badges and the search in
files work, but read the repository and the files through the mount.
