# Remote machines: what is left

Remote machines are read without sshfs: one OpenSSH master connection per
machine, files over SFTP (`core/vfs.h`), and commands run there through it
(`vfs_exec()`, `vfs_spawn()`). Search in files, Go to File, Deep Search,
Disk Usage, the Git tab, Git badges and the language servers all work on a
remote machine (see the user guide, `docs/user-guide/remote.md`). This page
lists what does not, or not fully, to come back to later.

## Not available yet

- **LaTeX builds.** They stay off for a remote machine's files
  (`latex_build_saved()` skips them). Idea: run `latexmk` / `pdflatex` on
  the machine (`vfs_spawn()` for the log as it comes), then bring the PDF
  and the `.log` back over SFTP for the preview and the error list. Needs a
  TeX distribution on the machine.
- **Open With Default App.** Refused with a status bar message
  (`files_menu.c`, `remote_path_refuse()`). Opening a cached copy was ruled
  out: the edits made in the other application would not go back to the
  machine.

## Known limits

- **Notebooks**: the pictures of the Markdown cells of a remote notebook
  are not shown. Those cells are rendered on the interface thread, where
  `image_file_local()` gives nothing for a remote file (never a wait for
  the network there).
- **Git tab** (`git_mirror`, a local copy of `.git`):
  - the Repository view shows the path of the copy's `.git/config` (in
    `~/.cache/lookinfiles/git-mirrors`), not the one on the machine;
  - a `.git` that is a file (a secondary worktree, a submodule) is not
    handled;
  - Git LFS objects are not copied.
- **GNU find**: Deep Search, Disk Usage and the copy of `.git` use
  `find -printf`, so they work on Linux machines, not on macOS or BSD ones
  (the Diagnostics tab says whether a machine's `find` is GNU find).
- **Waits on the interface thread**: a few calls still wait for the
  network there (`vfs_file_test()` while navigating, `vfs_remote_absolute()`
  when the first language server of a machine starts): short freezes on a
  slow or broken connection.
- **Windows without libssh**: builds with `libssh` share one SSH connection
  per machine for every channel (SFTP, commands, kernels, language servers
  and terminals; see [remote-libssh.md](remote-libssh.md)). Builds without
  `libssh` fall back to `ssh.exe`, where each session connects on its own
  and may ask for the password again (a key with ssh-agent avoids it).

## Beyond remote machines

- The explorer has no copy / paste nor drag and drop of files, even for
  local folders. A new feature, which would also carry files between this
  computer and a remote machine.
