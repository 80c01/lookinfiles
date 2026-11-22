# Remote machines

The remote view browses the files of SSH machines: LookinFiles reads and
writes them over SFTP through one SSH connection per machine, so the
explorer, the preview and the editor work on them, with nothing to mount.
Open it with `Ctrl+Shift+R` (again
to go back to the files) or the server button at the top of the explorer.

## Requirements

- The OpenSSH client (`ssh`), and the SFTP subsystem on the machine (the
  default of OpenSSH servers). A password or a key passphrase, when ssh needs
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

Connecting opens one connection (OpenSSH multiplexing, not on Windows),
kept until you disconnect or quit: the files, the Jupyter kernels and the
System Monitor all go through it, so a password is asked once. The SSH
terminals have connections of their own; one that needs a password asks in
the terminal.

Inside LookinFiles a machine's files are seen under an empty private folder
of `$XDG_RUNTIME_DIR/lookinfiles/remotes/` (usually `/run/user/1000/…`),
never shown: nothing is mounted there. Mounts that earlier versions made
with sshfs are unmounted at start-up. Machines added by hand are saved in
`~/.config/lookinfiles/remotes.xml`.

## Not available on a remote machine yet

These run on this computer and need local files: the language servers (completion, diagnostics) and LaTeX builds. Opening a file
with another application is not available either: Open With Default App
says so in the status bar; the others stay off.

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
