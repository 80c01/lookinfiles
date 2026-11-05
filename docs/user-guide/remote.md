# Remote machines

The remote view browses the files of SSH machines as if they were local:
LookinFiles mounts them with SSHFS, so the explorer, the preview, the
editor and the search all work on them. Open it with `Ctrl+Shift+R` (again
to go back to the files) or the server button at the top of the explorer.

## Requirements

- `sshfs` installed (`sudo apt install sshfs`; Fedora `fuse-sshfs`). Without
  it, connecting explains how to install it.
- An SSH access that works without typing a password (an SSH key, with
  `ssh-agent` if the key has a passphrase).

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

Machines are mounted under `~/.cache/lookinfiles/remotes/`. Machines added
by hand are saved in `~/.config/lookinfiles/remotes.xml`.

## Speed

Every file read goes over the network, so large folders and previews of
big files are slower than on a local disk. Git badges and the search in
files work, but read the repository and the files through the mount.
