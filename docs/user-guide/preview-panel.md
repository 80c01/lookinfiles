# The preview panel

Select a file in the explorer and the **Preview** tab shows it at once,
looking as close as possible to what the program made for that format would
show. Nothing to install: the formats are read by LookinFiles itself (PDF
pages need poppler, see [Getting started](getting-started.md#installing)).

The full list of formats and their limits is in
[Supported previews](../previews.md). This page explains how to use the
panel.

## What you see

| File | Preview |
|---|---|
| Text and source code | Syntax highlighting, line numbers for code, git change bars |
| Markdown | Rendered like a web page: headings, lists, task lists, tables, code blocks, images, TeX formulas (`$...$`, `$$...$$`, `\(...\)`, `\[...\]`, math environments, ```` ```math ```` blocks) |
| HTML, Org-mode | Rendered; Org-mode is interactive (see below) |
| Images | Fitted to the panel, with camera, exposure, date and GPS details |
| PDF | Pages |
| Video and audio | A player |
| Word, OpenDocument text | The pages, as printed: page size and margins, header and footer, fonts, colours, lists, tables and pictures (`+` / `-` zoom) |
| RTF | The document, with its fonts, colours, lists and tables |
| Spreadsheets (`.xlsx`, `.ods`, `.xls`), CSV | A grid with column letters |
| Presentations (`.pptx`, `.ppt`, `.odp`) | The slides, one by one, with speaker notes |
| Mail (`.eml`, `.msg`) and mailboxes | The headers, the text, the attachments; remote images are never loaded |
| JSON, XML, TOML, YAML, logs, diffs, man pages, calendars, contacts | Formatted and coloured |
| Archives (`.zip`, `.tar.gz`, `.deb`...) | The list of files, with sizes and dates |
| Certificates and keys | Subject, issuer, validity, fingerprints |
| Fonts | Names, details and samples |
| Programs and libraries (ELF) | Architecture, linked libraries, exported symbols |
| SQLite databases | Tables and views with their columns and first rows (opened read-only) |
| Anything else | A hex dump of the first 64 KB |
| A folder | Totals, file types, git changes, every entry, and its README rendered below |

The type comes from the content of the file, not only from its name: a
script without an extension or a `.txt` that is really a ZIP gets the right
preview. The bar at the bottom of the preview shows the name, the type, the
size and the detected text encoding.

## Rendered and raw views

`m` switches between the rendered view and the raw one:

- JSON, XML: tree ↔ indented text;
- TOML, YAML: tree ↔ as written;
- Markdown, HTML, Org, CSV, XML, TOML, YAML, diffs, logs: rendered ↔ source text;
- images: fitted ↔ actual size;
- PDF, Word, OpenDocument text: pages ↔ text;
- video and audio: player ↔ file details;
- documents, archives, certificates, programs: summary ↔ hex dump.

## Moving in the preview

Click in the preview or press `Alt+Right` to give it the focus. Then:

| Action | Every mode | Vim | Emacs |
|---|---|---|---|
| Scroll a line | `Down` / `Up` | `j` / `k` | `C-n` / `C-p` |
| Scroll a page | `Page Down` / `Page Up` | `Ctrl+F` / `Ctrl+B` | `C-v` / `M-v` |
| Start / end | `Home` / `End` | `gg` / `G` | `M-<` / `M->` |
| Search in the content | `Ctrl+F` (Notepad) | `/`, `n` / `N` | `C-s` / `C-r` |

`o` (or `Ctrl+K Ctrl+O`, as in the editor) shows the **tagbar**: the
headings of a Markdown, HTML, Org, Word (`.docx`, `.doc`: in their text
view, `m`), OpenDocument or EPUB file, or the bookmarks of a PDF (for the pages shown), beside the text,
nested by level; for source code, its functions, types... found by the
built-in scanner (no language server), grouped by kind (`s`: file order).
It follows the scrolling; a click or `Enter` scrolls to a heading or symbol,
`Esc` returns to the text, `q` hides it. Once turned on it appears in every
preview of the workspace that has headings or symbols; each workspace keeps
its own choice, and new workspaces and the next start follow the last one
made with `o` (also in Preferences, Editor page, which sets every
workspace). **Show / Hide Tagbar** in the right-click menu of the Preview
tab does it for the current workspace only, not kept. In a large file, only
the part first shown is listed.

The right-click menu of the Preview tab copies the previewed file's name
(**Copy Name**), its path from the root of its git repository (**Copy Git
Relative Path**, only inside a repository) or its absolute path (**Copy
Path**).

`e` opens the previewed text file in an editor tab; `t` (Vim) or `C-o`
(Emacs) opens it in the dedicated editor panel.

On the right, the **minimap** shows the whole document in small, with the
git change bars along its edge. Click or drag in it to scroll. To hide it
(and get the scrollbar back), choose **Hide Minimap** in the right-click
menu of the Preview tab (the current workspace only, not kept), or untick
**Show the minimap** in the Preferences, Editor page (every minimap and
workspace, kept).

## Images and PDF

- `+` / `-` zoom in and out, `0` goes back to the fitted size;
  `Ctrl+wheel` zooms too.
- Right-click an image and choose **Copy Image** to put the whole image
  in the clipboard.
- In a PDF, `Page Down` / `Page Up` go to the next and previous page.

## Video and audio

`Space` or `p` plays and pauses, `Left` / `Right` seek 5 seconds back /
forward in a video (also from the explorer, on the video shown). The player
also has its own controls.

`Enter` on a video opens it in a **Video** tab of the editor panel instead,
where it plays: drag the tab like an editor tab (another pane, a split, the
preview panel). In the tab, `Space` plays and pauses, `Left` / `Right` seek
5 seconds, `Home` goes back to the start, `a` / `s` switch to the next audio
track / subtitles, `Ctrl+W` closes it.

When a video has several audio tracks (languages) or subtitles, the player
shows **Audio** and **Subtitles** buttons (under the title in the preview, in
the bottom bar of a Video tab) to choose them. Subtitle files beside the video
with the same name (`Movie.srt`, `Movie.fr.srt`; `.srt`, `.ass`, `.ssa`,
`.vtt`) are listed with those of the file, and the first one is shown from the
start. This needs a build with GStreamer (`make deps`).

## Mailboxes

A mailbox (Thunderbird `Inbox`, `.mbox`...) first shows the list of its
messages; `Page Down` / `Page Up` step from the list into each message and
back. Maildir and MH folders show their newest messages instead of their
`cur` / `new` / `tmp` subfolders.

## Org-mode files

The Org-mode preview can be used without opening an editor:

| Key or mouse | Action |
|---|---|
| `Tab` / click on a heading | Fold or unfold the heading |
| `Shift+Tab` | Cycle the visibility of the whole file |
| `t` / click on the keyword | Cycle the TODO keyword (`TODO` → `DONE` → none, or the file's `#+TODO:` keywords) |
| `Space`, `Enter` / click | Tick or untick a checkbox (parents and `[n/m]` counters follow) |
| Click on a link | Open it |

The changes are saved to the file. See [Org-mode](org-mode.md) for editing.

## JSON, XML, TOML and YAML files

JSON is shown as a tree of keys and values in two columns: each key
indented by its depth, the value coloured by its type, and each object or
array with its number of keys or items and a ▾ / ▸ twisty that a click
folds or opens (`m` shows the JSON indented and coloured instead). XML is
shown the same way: the attributes (`@id`), elements (`entry[2]`), texts
(`#text`), CDATA sections and comments of each element under it, an element
holding only a text as a value (`m`: the XML indented and coloured). YAML
and TOML are trees too (`m`: as written and coloured, with their comments;
a TOML table holds the tables under it, an array of tables its entries).
Their
outer keys, elements or tables are in the tagbar (`o`). `Tab` folds or
opens the object, array, element, table, mapping or sequence at the cursor
(on a line of values or text: the one around it), highlighted with the
cursor line, and a
folded one shows how many keys, items or elements it has; a TOML table
folds with the tables under it (`[servers.alpha]` under `[servers]`).
`Shift+Tab` shows the outer members only, then two levels, then everything.
A document too long to show open starts folded below the deepest level that
fits. The folds of each file are kept until LookinFiles quits.

The bar at the bottom shows the path of the value at the cursor, such as
`$.servers[2].host` in JSON, `/feed/entry[2]/title` in XML,
`servers.alpha.ip` in TOML or `jobs.build.steps[1].run` in YAML (`[1].kind`
in the second document of a file of several); `y` (or **Copy Value Path** in the
right-click menu of the Preview tab) copies it. A file over 512 KB shows its first 64 KB, the
values open there ending with `…`; **Load Whole File** shows all of it. A
file that is not valid JSON, XML or TOML, or YAML with tabs in its
indentation or complex `?` keys, is shown as text.

## Large files

Text files over 512 KB show their first 64 KB, and the next parts load as
you scroll down (up to 8 MB). Long documents stop after 256 KB of text, and
the preview says so when a limit is reached. Moving to another file cancels
a preview that is still loading.

## Source code

`Ctrl+Click` on a name in the preview of a source file opens the file where
that name is defined (see [code navigation](code-navigation.md)).

When the previewed file changes on disk, the preview reloads by itself,
staying on the line it showed. When the file is open in an editor beside
the preview, the preview follows the editor's cursor: its line (in the
rendered Markdown and Org too) is shown at the height of the cursor when it
moves. Saving leaves the preview where it is.

## System Monitor

On Linux and Windows, the top bar shows a small chart of the CPU use over
the last minute, with its last value; its tooltip adds the load average
(Linux) and the memory in use. A click on it opens the **System Monitor** tab in the preview
panel (another click closes it):

- **CPU** (and a bar per core), **Memory** (and swap), **Load Average**,
  **Network** (received and sent) and **Disk I/O** (read and written), each
  with a chart of the last two minutes, updated every second;
- the mounted **Filesystems**, with how full each one is;
- the **Processes**, by CPU use at first: click a column header to sort by
  it. The CPU of a process counts 100% for one full core.

On Windows there is no load average card and no process state; the
filesystems are the fixed and removable drives, and the processes of other
users (services) show no CPU, memory or user unless the application runs
as administrator.

The tab only shows: it never stops or changes a process. The processes and
filesystems are read only while the tab is on screen. `Ctrl+W` or a middle
click on the tab closes it.

## Disk Usage

`Shift+S` in the explorer (or **Disk Usage** in the right-click menu of a
folder) opens the **Disk Usage** tab in the preview panel: how much room
the folder and everything below it take. On a file, it is the file's
folder.

The folder is read in the background: a bar shows how far the reading is
(the folders done out of all of them, or, for the top of a disk, the part
of its used space read so far), with the files, folders and bytes counted
so far; the folders already read are listed as they come. `Escape` (or the
**Stop** button) stops the reading and keeps what was read; `F5` reads the
folder again.

Then the list shows the folders and the largest files of the folder, the
largest first, each with its share of the folder, its size, its number of
files and its last change (click a column header to sort by it). `Enter`
opens a folder, `Backspace` comes back; `Enter` on a file shows it in the
explorer. A right click on a row also copies its path.

Beside the list, once the folder is read, its statistics: sizes (and the
room taken on the disk), files, folders, average file size, empty files
and folders, links, the oldest and newest change, the deepest folder; then
the room taken by kind of file (documents, images, videos, audio,
archives, text and code, programs), by last change (less than a day, a
week, a month, a year, 1 to 3 years, more) and by extension, the largest
files of the whole folder (a click shows one in the explorer), and the
entries that could not be read.

Symbolic links are not followed, a file with several hard links counts
once, and other disks mounted inside the folder are left out. The summary
under the title gives the room really taken on the disk too, and the
number of entries that could not be read. `Ctrl+W` or a middle click on
the tab closes it and stops the reading.
