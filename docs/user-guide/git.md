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

The bar at the bottom of the explorer shows the branch (⎇ `main`), and the preview of a folder
lists the branch and the changed files.

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
the commit, message, author and date of each commit. Select one to see its
full diff below.

The navigation keys of the global mode move in the list (`j` / `k` in Vim,
the arrows in every mode).

## Diff and patch files

A `.diff` or `.patch` file is previewed with a summary of the files it
changes and coloured lines; `m` shows the raw text.
