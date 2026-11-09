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
| Markdown | Rendered like a web page: headings, lists, task lists, tables, code blocks, images |
| HTML, Org-mode | Rendered; Org-mode is interactive (see below) |
| Images | Fitted to the panel, with camera, exposure, date and GPS details |
| PDF | Pages |
| Video and audio | A player |
| Word, OpenDocument, RTF | The document, with its fonts, colours, lists, tables and pictures |
| Spreadsheets (`.xlsx`, `.ods`, `.xls`), CSV | A grid with column letters |
| Presentations (`.pptx`, `.ppt`, `.odp`) | The slides, one by one, with speaker notes |
| Mail (`.eml`, `.msg`) and mailboxes | The headers, the text, the attachments; remote images are never loaded |
| JSON, logs, diffs, man pages, calendars, contacts | Formatted and coloured |
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

- Markdown, HTML, Org, CSV, JSON, diffs, logs: rendered ↔ source text;
- images: fitted ↔ actual size;
- PDF: pages ↔ extracted text;
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
headings of a Markdown, HTML, Org, Word (`.docx`, `.doc`), OpenDocument or
EPUB file, or the bookmarks of a PDF (for the pages shown), beside the text,
nested by level; for source code, its functions, types... found by the
built-in scanner (no language server), grouped by kind (`s`: file order).
It follows the scrolling; a click or `Enter` scrolls to a heading or symbol,
`Esc` returns to the text, `q` hides it. Once turned on it appears in every
preview that has headings or symbols (kept for the next start; also in
Preferences, Editor page). In a large file, only the part first shown is
listed.

`e` opens the previewed text file in an editor tab; `t` (Vim) or `C-o`
(Emacs) opens it in the dedicated editor panel.

On the right, the **minimap** shows the whole document in small, with the
git change bars along its edge. Click or drag in it to scroll.

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

## Large files

Text files over 512 KB show their first 64 KB, and the next parts load as
you scroll down (up to 8 MB). Long documents stop after 256 KB of text, and
the preview says so when a limit is reached. Moving to another file cancels
a preview that is still loading.

## Source code

`Ctrl+Click` on a name in the preview of a source file opens the file where
that name is defined (see [code navigation](code-navigation.md)).

When the previewed file changes on disk, the preview reloads by itself.
