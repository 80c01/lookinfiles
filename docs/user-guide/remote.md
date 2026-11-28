# Remote machines

The Machines view lists this computer and your SSH machines, and browses
the files of the SSH machines: LookinFiles reads and
writes them over SFTP through one SSH connection per machine, so the
explorer, the preview and the editor work on them, with nothing to mount.
Open it with `Ctrl+Shift+R` (again
to go back to the files) or the server button at the top of the explorer.

## Requirements

- On Linux, the OpenSSH client (`ssh`). On Windows, builds with `libssh`
  connect directly without needing `ssh.exe` (builds without `libssh` fall
  back to `ssh.exe`).
- On the remote machine, an SSH server with the SFTP subsystem (the default
  of OpenSSH servers).
- A password, a key passphrase or an unknown host key confirmation is asked
  in a dialog of LookinFiles; an SSH key with `ssh-agent` spares you the
  question. The [Diagnostics tab](lookinfiles:diagnostics#ssh) says whether
  `ssh` is found, whether [libssh](lookinfiles:diagnostics#libssh) is built
  in, and which transport each machine uses.

## The list of machines

The view lists:

- this computer, always first (its name, then **This computer** and your
  user name, in green while the current workspace browses it): `Enter` or
  `c` goes back to its files, `t` opens a terminal on it. Below it,
  **Disks** lists its disks: the file system, partitions, USB keys, optical
  discs and network folders that have a local folder (drive letters on
  Windows), the disks not mounted yet marked *not mounted*. The list follows
  the disks you plug in or remove. On a disk, `Enter` browses it (mounting
  it first if needed), `c` mounts or unmounts it (ejects a removable one, so
  that it can be removed safely), `t` opens a terminal in it;
- every `Host` of your `~/.ssh/config` (`F5` reads the file again);
- the machines you added by hand.

To add one, press `a` and fill in **Friendly Name**, **Host / IP Address**
(required), **User**, **Port** (22 by default) and **Remote Directory** (`~`
by default). **Save & Connect** connects right away.

## Using a machine

| Key | Action |
|---|---|
| `Enter` | Connect and browse the machine in the files view |
| `c` | Connect without leaving the Machines view, or disconnect a connected machine |
| `t` | Open an SSH terminal to the machine |
| `F2`, `r` or `e` | Edit the machine |
| `d` or `Delete` | Delete it from the list |
| `F5` | Read `~/.ssh/config` again |

## Containers (Docker, Podman)

Below this computer and below each connected machine, a **Docker** and a
**Podman** row list the containers, when the program is installed there
(`docker ps -a` / `podman ps -a`, read in the background when the machine is
expanded): running ones first, with their image. A row says so when Docker
does not answer (daemon stopped, permission denied).

| Key | Action |
|---|---|
| `Enter` or `t` | Open a terminal in the container (`docker exec -it`, bash or else sh; through SSH on a remote machine) |
| `c` | Start the container, or stop it when it runs |
| Right click | Start, stop, restart, open a terminal |
| `F5` | List the containers again |

Containers are not removed from here, and their files are not browsed yet.

## WSL distributions (Windows)

On Windows, a **WSL** row below this computer lists the installed WSL
distributions (Docker Desktop's own are left out), with their version,
whether they run, and the default one. `Enter` or `c` browses a
distribution's files (`\\wsl.localhost\<name>\`, which starts it), `t` opens a
terminal in it (`wsl.exe -d <name>`), and the right-click menu has
**Stop** for a running one (`wsl --terminate`). `F5` lists them again.
The breadcrumbs start with **WSL: <name>** while you browse one.

## The disks of a machine

Expand a connected machine to see its **Disks**: the file system `/`,
partitions, USB disks and network shares mounted there (listed with
`findmnt`, or `df` on macOS and BSD, in the background), with their
folder and free space. A machine not connected shows *Not connected —
Enter to connect*. `Enter` or `c` on a disk browses it, `t` opens an SSH
terminal in it; `F5` lists the disks again.

A disk outside the machine's **Remote Directory** (`/mnt/data` when it is
`~`) is opened through a second SFTP session of the same connection (no new
login): its files are written `machine:/mnt/data/...`, and everything works
there as in the remote directory. These disk sessions end when the machine
disconnects.

The folders of a connected machine are listed over SFTP, through the SSH
connection already open (no new login); text files are previewed and opened
in the editor the same way. A slow network
makes the listing or the file wait, not the whole window. Saving, new files
and folders, renames and deletes go over SFTP too. A remote machine has no
trash: deleting a file or a folder there (after a confirmation that says
so) is for good, and cannot be undone with `u`.

Images, PDFs, archives, documents, videos and sounds are copied over SFTP
into the cache folder (`~/.cache/lookinfiles/remote-files`) to be previewed,
once per version of the file (up to 64 MB each, 512 MB for a video or a
sound, 1 GB in all, the oldest removed first). A large video takes the time
of its copy before it plays. The pictures of a Markdown, HTML or Org
preview (a README's screenshots) are copied the same way, up to 16 MB each;
those of a notebook's Markdown cells are not shown yet.

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
**New File** and **New Folder** dialogs show `server:~/projects`, and so do the notebook **Save** dialog, the Emacs
`C-x C-w` prompt and the tab tooltips. Type paths the same way (`server:~/a`,
or `server:docs` from the **Remote Directory**); paths outside the remote
directory are refused.

Connecting opens one shared connection per machine, kept until you
disconnect or quit: files (SFTP), remote commands (Git, search, Deep Search,
Disk Usage, System Monitor), Jupyter kernels and language servers all go
through it, so a password is asked once. On Linux this is an OpenSSH master
connection (`ControlMaster`), and interactive SSH terminals open connections
of their own so a stalled terminal cannot block the rest (one that needs a
password asks in the terminal). On Windows, builds with `libssh` use one
built-in libssh connection for everything — interactive SSH terminals
included, with per-channel flow control and no `ssh.exe` (builds without
`libssh` fall back to `ssh.exe`, where each session connects on its own; an
SSH key with `ssh-agent` avoids repeated prompts). The
[Diagnostics tab](lookinfiles:diagnostics) shows which transport a machine
uses; when `libssh` is built in, the Preferences (`Ctrl+,`), **Remote
machines** page, choose between `libssh` and the `ssh` program (at the next
launch; the variable `LOOKINFILES_SSH_LIBSSH=1` or `0` wins over it).

Inside LookinFiles a machine's files are seen under an empty private folder
of `$XDG_RUNTIME_DIR/lookinfiles/remotes/` (usually `/run/user/1000/…`),
never shown: nothing is mounted there. Mounts that earlier versions made
with sshfs are unmounted at start-up. Machines added by hand are saved in
`~/.config/lookinfiles/remotes.xml`.

## Not available on a remote machine yet

Opening a file with another application is not available: Open With
Default App says so in the status bar. LaTeX documents are compiled on the
machine, as the other build tasks (below).

## Build and test tasks

`F7` / `Shift+F7` in a project of the machine run its task there, through
the machine's connection, in the task's folder on the machine: the build
files are read over SFTP, `make -j` uses the machine's processors, and the
tools must be installed there (a missing one ends with its "command not
found" in the Build tab). The output comes as it is written; the files the
messages name (the machine's folders) are opened where the machine is seen
here, and a file outside it (a system header) is named in the message
text only. **Stop** (`Ctrl+F7`) asks the machine to end the task and the
programs it started (SIGTERM, then SIGKILL). A task still running when the
application quits may go on on the machine.

## Language servers

The language servers (completion, diagnostics, go to definition, rename)
run on the machine, in the project there: the first server of the
language's list (or the one set in `lsp.ini`) installed on the machine is
started through ssh in a login shell, so the programs of `~/.local/bin` and
the like are found. The paths in what is said to it and what it answers
are written as the machine knows them. Nothing is installed on the machine:
a language without a server there just has no diagnostics.

## Deep Search

Deep Search (`Deep Search…` on a folder) runs on the machine too: `find`
lists the entries there (it needs GNU `find`, as on Linux), the criteria
are tested here, and for a content search `grep` reads the files that pass
the others, a few hundred at a time, each line found checked here. The
folder field shows `machine:path`, and takes it typed so.

## Disk Usage

Disk Usage reads a remote folder with one `find` run on the machine (GNU
`find`, as on Linux), which lists every entry with its size, blocks and
date; the tree is then added up here as for a local folder (hard links
once, other filesystems skipped when asked, exclusions). The folder being
listed shows while `find` runs; the folders of the breakdown come at the
end, all at once. In a repository, the Git overview (tracked, untracked,
ignored, history) comes from `git ls-files` run on the machine, the sizes
from what `find` listed.

## Git

In a repository on the machine, the explorer shows the Git badges of the
changed files and the status bar the branch, with `*` for uncommitted
changes and the commits ahead of and behind the upstream: `git status` runs
on the machine (it needs `git`) each time the folder is read again. A click
on the branch lists the branches there and switches to the one picked
(`git checkout` on the machine).

The Git tab shows History, Statistics, Repository and Stashes & Tags from a
copy of the repository's `.git` folder kept in
`~/.cache/lookinfiles/git-mirrors`: the first time, the whole folder comes
over in one `tar` stream run on the machine (a large history takes a while);
after that, only the files new or changed (the refs, the index, new packs)
each time the tab reads the repository again. The copy is only read, never
written. The Changes view runs `git status` and `git diff` on the machine
(the line counts with `git diff --numstat`), and Compare with HEAD reads
the file's last committed version from the copy.

## Search in files and Go to File

The search in files (`Ctrl+Shift+F`) and Go to File run on the machine:
the files are listed there (with `git ls-files` in a repository, else
`find`, leaving out hidden and build folders as on this computer), and
`grep` finds the lines that may match. Each line is then checked here, so
the options (match case, whole word, regular expression, letters in order)
give the same results as for local files. The machine needs `grep`, and
`git` for repositories; a regular expression uses `grep -P` when the
machine has it, else `grep -E`, which knows fewer forms (no `\d`, no
look-ahead). At most 32 MB of lines are read; the Search tab says when
there were more.

## Speed

Every file read goes over the network, so large folders and previews of
big files are slower than on a local disk.
