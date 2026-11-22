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

The folders of a connected machine are listed over SFTP, through the SSH
connection already open (no new login), rather than through the mount; text
files are previewed and opened in the editor the same way. A slow network
makes the listing or the file wait, not the whole window. Saving, new files
and folders, renames and deletes go over SFTP too. A remote machine has no
trash: deleting a file or a folder there (after a confirmation that says
so) is for good, and cannot be undone with `u`.

Images, PDFs, archives, documents, videos and sounds are copied over SFTP
into the cache folder (`~/.cache/lookinfiles/remote-files`) to be previewed,
once per version of the file (up to 64 MB each, 512 MB for a video or a
sound, 1 GB in all, the oldest removed first). A large video takes the time
of its copy before it plays.

A file of a remote machine open in the editor is checked for changes made
on the machine every 3 seconds, over SFTP: the editor then reacts as for a
local file (reload, warning or banner, depending on its mode). The same
way, the folders shown in the explorer and the previewed file are checked
every 3 seconds: files created, removed or renamed on the machine appear
in the explorer within that time.

While you browse a connected machine, the breadcrumbs start with a server icon and
the machine's name, and the **Terminal** button of the top bar opens an SSH
terminal to it instead of a local shell.

Jupyter notebooks of the machine run their kernel on the machine itself
(see [The editor](editor.md)), and the System Monitor tab shows the machine
(see [The preview panel](preview-panel.md#system-monitor)), both through the
connection opened when connecting.

Paths on the machine are written as on the machine, `name:path`: the
**New File** and **New Folder** dialogs show `server:~/projects` (not the
local mount point), and so do the notebook **Save** dialog, the Emacs
`C-x C-w` prompt and the tab tooltips. Type paths the same way (`server:~/a`,
or `server:docs` from the **Remote Directory**); paths outside the remote
directory are refused.

Connecting opens a connection for the commands run on the machine (Jupyter
kernels; OpenSSH multiplexing, not on Windows), kept until you disconnect or
quit; the files and the SSH terminals have connections of their own. A
password typed when connecting serves the files' connection too; a terminal
that needs one asks in the terminal.

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
