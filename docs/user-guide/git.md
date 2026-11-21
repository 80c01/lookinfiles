# Git

When the current folder is inside a git repository, LookinFiles shows what
changed, without running anything yourself. The `git` program is not
needed: LookinFiles reads the repository with libgit2.

LookinFiles shows the state of the repository; to stage, commit, push or
switch branches, use git in the [terminal](terminal.md). The display
updates by itself after those commands.

## In the explorer

Files show a coloured letter after their name:

| Badge | Meaning |
|---|---|
| **M** | Modified |
| **A** | Added to the index |
| **D** | Deleted |
| **R** | Renamed |
| **U** | Untracked (new file not yet added) |
| **!** | Merge conflict |

A folder shows **M** when it holds changed files, **U** when it only holds
new ones.

The bar at the bottom of the explorer shows the branch (⎇ `main`), with `*` when
the repository has uncommitted modifications, and `↑` / `↓` with commit counts
when ahead or behind its upstream tracking branch (e.g. `⎇ main * ↑1 ↓2`). The
preview of a folder lists the branch and the changed files.

## Change bars

In the preview of a text file and in editor tabs, a bar in the margin marks
the lines changed since the last commit, as in VS Code:

- green: added lines;
- blue: modified lines;
- red marker: deleted lines.

Editor tabs update the bars as you type. The minimap shows them along its
left edge.

## The Git tab

The **Git** tab, next to the **Preview** tab, appears when the current folder
is in a repository. The buttons at its top (or `v`) switch between its
four views: **Changes**, **History**, **Statistics** and **Repository**. It
opens on **History**.

### Changes

What is not committed yet, in three groups:

- **Staged**: what the next commit will hold (`git add`);
- **Changes**: what changed in the files since then, not staged;
- **Untracked**: the new files git does not follow yet.

A file staged and then changed again is in both of the first two groups.
Each file shows its status letter (the badges of the explorer), its folder
and the lines it adds and removes (`+12 −3`). Select a file to see its patch
below: what the commit will add for a staged file, what changed since it was
staged for the others, the whole file for an untracked one. `Enter` or a
double click previews the file. The button says how many changes there
are, from the other views too (counted from the status only, as fast as
`git status`); the list is read again each time the view is shown and when
the folder changes.

### History

The history, newest first, with the commit, message (preceded by coloured
badges for `HEAD`, local and remote branches, and tags), author and date of
each commit (relative, in full in its tooltip). Select one to see its full
diff below, under a line giving its hash, subject, author and date. The
newest 100 commits are read first; scrolling to the end of the list (or
moving the selection to its last commit) reads the next 100.

**Git History** in the right-click menu of a file or folder of the explorer
shows only the commits that changed it (renames are not followed): its path
stands beside the filter field, and its cross brings every commit back.

The field above the list filters the history: only the commits whose
message, author (name or email) or hash hold every word typed (any case)
stay, searched through the whole history, not just the commits shown. Beside
it, how many commits are listed (the next ones that match are read in the
same way).
`Ctrl+F`, `/` in Vim mode or `C-s` in Emacs mode go to it from the list;
`Enter` or `Down` go back to the list, `Esc` empties it.

A right click on a commit copies its hash, its short hash or its whole
message. `Ctrl+C` (`y` in Vim mode, `M-w` in Emacs mode) copies the hash of
the selected commit.

The navigation keys of the global mode move in the list (`j` / `k` in Vim,
the arrows in every mode).

### Statistics

How many commits, by how many authors, from the first month to the last,
then:

- **The last twelve months**: a square per day, a column per week (Monday on
  top), darker with more commits;
- **By day of the week and hour**: a circle per hour of each day, larger
  with more commits, at the time of day of each author (in their own time
  zone);
- **By month**: a bar per month since the first commit (per year past six
  years);
- **Authors**: their commits, their share and the months of their first and
  last commits, most commits first (the first 20).

Each square, circle and bar gives its count in its tooltip. The whole
history is counted when the view is shown, and again after a commit.

### Repository

How the repository is set up:

- **Identity**: the name and email your commits are made with, and where
  each comes from (**System**, **Global**: your `~/.gitconfig`,
  **Repository**: its `.git/config`); **Not set** when missing; whether
  commits are signed, and with which key.
- **Remotes**: each remote and its URL (the push URL too when it differs).
- **Branches**: each local branch (the checked-out one in bold), its
  upstream and how many commits are to push (`↑`) or to pull (`↓`), **up to
  date**, **no upstream**, or **upstream gone** when its remote branch was
  deleted.
- **Configuration**: every setting with its level; a setting set again at
  a closer level is struck through (not in effect). The field above it
  filters the settings (name or value).

Passwords and tokens are never shown: in URLs (`https://me:•••@host/...`)
and in settings that hold them (tokens, passwords, extra HTTP headers).
The view only shows the setup; **Edit .git/config** and **Edit Global
Config** open those files in an editor tab. It is read again each time it
is shown.

## Diff and patch files

A `.diff` or `.patch` file is previewed with a summary of the files it
changes and coloured lines; `m` shows the raw text.
