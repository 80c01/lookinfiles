# Org-mode

[Org-mode](https://orgmode.org) is the outline and task format of GNU Emacs.
LookinFiles reads and edits `.org` files with the main Org commands, so
they stay compatible with Emacs.

There are two ways to work with an Org file:

- **In the preview**: fold headings, cycle TODO keywords, tick checkboxes
  and follow links without opening an editor (see
  [the preview panel](preview-panel.md#org-mode-files)).
- **In an editor tab**: `.org` files open in the **Org-mode** editor mode,
  which uses the Emacs keys and adds the Org commands below.

Org notes in the [notes view](notes.md) work the same way.

## How to read the keys

`Alt` is Emacs' Meta (`M-`). `C-c C-t` means `Ctrl+C`, then `Ctrl+T`. Keys
without `C-c` also work in Notepad and Vim tabs. `M-x org-…` runs a
command by its Emacs name (`org-todo`, `org-schedule`, `org-clock-in`...).

## Outline

| Key | Action |
|---|---|
| `Tab` | On a heading: folded → children → whole subtree |
| `Shift+Tab` | Whole file: overview → contents → everything |
| `Alt+Enter` | New heading (or new list item in a list) |
| `Ctrl+Enter` | New heading after the current subtree |
| `Alt+Left` / `Alt+Right` | Promote / demote the heading |
| `Alt+Shift+Left` / `Alt+Shift+Right` | Promote / demote the whole subtree |
| `Alt+Up` / `Alt+Down` | Move the subtree past its sibling |
| `C-c C-n` / `C-c C-p` | Next / previous heading |
| `C-c C-u` | Parent heading |
| `C-c *` | Turn a line into a heading, or back |

The file opens folded to its top-level headings, unless it has a
`#+STARTUP:` line: `content` shows all headings, `showall` shows
everything. Folds stay in place while you edit, and moving the cursor into
folded text (search, go to line) unfolds it.

## Tasks

| Key | Action |
|---|---|
| `Shift+Left` / `Shift+Right`, `C-c C-t` | Cycle the TODO keyword |
| `Alt+Shift+Enter` | New TODO heading |
| `Shift+Up` / `Shift+Down`, `C-c ,` | Change the priority `[#A]` |
| `C-c q` | Set the tags of the heading |
| `C-c / t` | Show only the open TODO entries |
| `C-c / /` | Show only the lines matching a regular expression |

The keywords are `TODO` and `DONE` unless the file declares its own:

```org
#+TODO: TODO NEXT | DONE CANCELLED
#+STARTUP: logdone
```

With `logdone`, finishing an entry adds `CLOSED: [date]`. A keyword
written `RUNNING(r!)` logs the time the entry entered that state; add
`#+STARTUP: logdrawer` to keep those lines in a `:LOGBOOK:` drawer.

## Checklists

```org
* Trip [1/3]
- [X] Book the train
- [ ] Pack
- [ ] Water the plants
```

`C-c C-c` or `Ctrl+Space` ticks the item under the cursor. Parents become
`[-]` when partly done, and the `[n/m]` and `[n%]` counters update. `C-c #`
updates every counter.

## Dates and clock

| Key | Action |
|---|---|
| `C-c C-s` | Schedule the entry |
| `C-c C-d` | Set a deadline |
| `C-c .` / `C-c !` | Insert an active `<…>` / inactive `[…]` date (`C-u` first: with the time) |
| `Shift+Up` / `Shift+Down` on a date | Change the part under the cursor |
| `C-c C-x C-i` / `C-c C-x C-o` | Clock in / clock out (in a `:LOGBOOK:` drawer) |

Dates can be typed as `today`, `+3`, `+2w`, `mon`, `2026-11-02 10:00`, or
`none` to remove one. A repeating date such as
`SCHEDULED: <2026-11-02 Mon +1w>` moves to its next occurrence when the
entry is marked done.

## Tables

```org
| Name  | Qty |
|-------+-----|
| Apple |   3 |
```

`Tab` and `Shift+Tab` move between cells, `Enter` goes to the next row
(added when needed), and the table realigns as you go. A line starting with
`|-` becomes a rule. `Alt+Arrows` move columns and rows,
`Alt+Shift+Arrows` delete or insert them.

## Links and blocks

- `C-c C-l` inserts a link, `C-c C-o` opens the link under the cursor
  (a URL, `file:`, `*Heading`, `#custom-id` or `<<target>>`).
- Type `<s` then `Tab` for a source block (`<e` example, `<q` quote, and
  the other Emacs templates).

The complete list is in the
[keyboard reference](../keyboard.md#org-mode-editing).
