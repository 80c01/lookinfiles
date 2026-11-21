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

## The Git Commits tab

The **Git Commits** tab, next to the **Preview** tab, appears when the
current folder is in a repository. It lists the history, newest first, with
the commit, message (preceded by coloured badges for `HEAD`, local and remote
branches, and tags), author and date of each commit. Select one to see its
full diff below.

The field above the list filters the history: only the commits whose
message, author (name or email) or hash hold every word typed (any case)
stay, searched through the whole history, not just the commits shown. Beside
it, how many commits are listed (the last 100, or the first 100 that match).
`Ctrl+F`, `/` in Vim mode or `C-s` in Emacs mode go to it from the list;
`Enter` or `Down` go back to the list, `Esc` empties it.

A right click on a commit copies its hash, its short hash or its whole
message. `Ctrl+C` (`y` in Vim mode, `M-w` in Emacs mode) copies the hash of
the selected commit.

The navigation keys of the global mode move in the list (`j` / `k` in Vim,
the arrows in every mode).

## Diff and patch files

A `.diff` or `.patch` file is previewed with a summary of the files it
changes and coloured lines; `m` shows the raw text.
