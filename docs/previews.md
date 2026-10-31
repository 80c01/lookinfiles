# Previews

LookinFiles's value is the preview panel: select a file and see its content at
once, looking as close as possible to what the application made for that
format would show. This page lists what is previewed, then gives the rules
every preview follows, the coverage today, and the checklist for adding a
format. Open work is in
[TODO.md](../TODO.md) ("Preview").

## What the preview panel shows

- Text and source code with syntax highlighting (GtkSourceView, Dark+ colours),
  and line numbers for source code.
- Markdown rendered like VS Code's preview: headings, lists, task lists
  (including in-progress `[/]` / `[-]` tasks), tables, quotes, GitHub alerts,
  highlighted code blocks and images.
- HTML and interactive Org-mode (folding, TODO cycling, checkboxes, links).
- Images (EXIF orientation, camera, exposure, date and GPS details, zoom with
  `+` / `-` / `0` or `Ctrl+wheel`), PDF documents (poppler), video and audio
  with player controls.
- Office documents, read without any external tool: Word `.docx` and
  `.doc` (97-2003) as Word shows them (fonts, sizes, colours, highlights,
  alignment, indents, spacing, list labels, tables as grids, pictures (not
  in `.doc`), page breaks, header and footer; colours adjusted to stay
  readable on the theme; Word 6 / 95: the text); OpenDocument text (`.odt`)
  with headings, styles, lists, tables, links and images;
  spreadsheets (`.xlsx`, `.ods`, `.xls` from Excel 97-2003 or 5.0 / 95) as
  grids with column letters, number, date and percent formats; presentations:
  `.pptx` and `.ppt` / `.pps` drawn slide by slide like PowerPoint
  (shapes, fills, pictures, tables, text styles from the layout, master and
  theme; speaker notes below; the first 20 slides drawn), `.odp` slide by
  slide; RTF. The exact page
  layout (page size, columns, floating positions) is not reproduced; long
  documents stop after 256 KB of text. Other OLE2
  compound files (`.msi`, `.vsd`, Works...) show their properties (title,
  author, dates, counts) and streams.
- Mail messages (`.eml`, `.emlx`, or any file starting with mail headers):
  the headers, the text with quotes and signature dimmed (or the HTML body
  with its inline images; remote content is never fetched), forwarded
  messages and the attachments; encoded headers and charsets decoded.
  Mailboxes (Thunderbird `Inbox` and other mbox files, `.mbox`, `.mbx`) as a
  list of messages, opened one by one with `PgDn` / `PgUp`; only the
  beginning and the end of very large ones are read. Maildir and MH folders
  show their newest messages (date, sender, subject, read / unread / replied)
  instead of the `cur` / `new` / `tmp` listing. Outlook messages (`.msg`)
  are shown the same way (text, HTML or RTF body, attachments), and so are
  the `winmail.dat` (TNEF) attachments Outlook adds to mails. Outlook data
  files (`.pst`, `.ost`) and Outlook Express folders (`.dbx`) show a card
  with their format, size and encryption.
- EPUB books (cover, metadata, contents, first chapters) and Jupyter
  notebooks (Markdown cells, highlighted code, text and image outputs).
- Data and text formats: CSV / TSV as aligned tables, JSON (also minified,
  JSONC and JSON Lines) pretty-printed and coloured, diffs and patches with a
  per-file summary, logs coloured by level and shown from their end, manual
  pages formatted like `man` (gzipped ones too), calendars (`.ics`), contacts
  (`.vcf`) and `.desktop` entries as cards.
- Archives (`.zip`, `.jar`, `.tar`, `.tar.gz`, `.a`, `.deb`...) listed with sizes and
  dates; a gzipped file shows its content. Certificates and keys (PEM, DER):
  subject, issuer, validity, alternative names, key, fingerprints (private
  keys are only named). Fonts: names, details and samples in the font.
- Directory overview: totals, file types, branch and git changes, then each entry with
  its icon, size, date and git status, in the explorer's sort order, and the
  folder's README rendered below (one level read in the background, never
  recursively).
- Summaries of notebooks, bookmark folders and machines.
- A **Git Commits** tab listing the repository history with each commit's diff,
  and a **Search** tab with the results of a search in files (`Ctrl+Shift+F`).
- Git change bars in the text preview and the editor, as in VS Code: lines
  added (green), modified (blue) or deleted (red marker) since the last commit;
  the editor updates them while typing. The minimap shows them along its
  left edge.
- A minimap that mirrors the displayed layout and scrolls the view (click,
  drag or mouse wheel). As in VS Code, a long file keeps a readable scale and
  the minimap scrolls along with the view.
- File types are detected from the content (an image or a `#!` script without
  extension, TypeScript `.ts` that is not a video); unknown binary files show a
  hex dump (offsets, bytes and an ASCII column) of their first 64 KB.
- Executables, shared libraries and object files (ELF) show their
  architecture, type, interpreter, linked libraries, soname, build ID,
  whether they are stripped, and their exported symbols; `m` shows the hex
  dump instead. Only the headers are read, even for a large library.
- `m` switches between the rendered and the raw view (text, or the hex dump
  of binary formats). Text files over 512 KB show their first 64 KB, and the
  next parts load as you scroll down (up to 8 MB). The encoding is detected
  (UTF-8, UTF-16, Windows-1252) and shown in the status bar.

## Rules every preview follows

1. **In a blink.** Something useful is on screen within **50 ms** of the
   selection for files up to `PREVIEW_BYTES`, and within **150 ms** for
   rendered formats (PDF page, image, office document). Heavier work fills
   in afterwards; it never delays the first paint. `make bench-preview`
   checks these budgets.
2. **Never block the UI thread.** Reading, parsing and decoding run in a
   `GTask` worker using `app->preview_cancellable`; moving to another row
   cancels it. The UI thread only builds widgets from a finished result.
3. **Bounded.** Never read a whole huge file: a byte limit per format
   (`PREVIEW_BYTES` for text), a row limit for tables, an entry limit for
   archives, a page limit for documents. When a limit is hit, say so in the
   preview ("first 1,000 of 48,210 rows").
4. **Content first, name second.** The kind comes from `file_kind_detect()`
   (magic numbers), the extension only breaks ties. A `.txt` that is a ZIP
   is previewed as a ZIP; a file without extension still gets its preview.
5. **Native fidelity, raw on demand.** The default view looks like the
   native tool (spreadsheet grid for CSV, page for PDF, rendered Markdown).
   `m` always toggles to the raw / text view.
6. **Always a fallback.** A parser that fails falls back to the next level
   down (see the fidelity ladder), never to an empty pane or an error only.
7. **No new hard dependency.** GTK / GLib / GIO only; anything else is an
   optional `pkg-config` dependency (`HAVE_*`, like poppler and VTE) with a
   working fallback when absent.
8. **Testable without a display.** Each format's parser is a layer 1
   foundation module with unit tests and fixtures; the widget part stays
   thin.

### Fidelity ladder

Each format is rated by the best level it reaches. The goal is to raise
every common format to L3 and never leave one below L1.

| Level | Shows | Example |
|-------|-------|---------|
| L0 | Hex dump | unknown binary |
| L1 | Summary: type, size, key metadata | ELF header, certificate fields |
| L2 | Structured content: text, table, tree, outline | archive listing, `.docx` as text |
| L3 | Rendered like the native tool | Markdown, PDF page, image, CSV grid |

## How it is built

- **Registry**: `preview_formats` (layer 1) has one builder per `FileKind`;
  each builds a `PreviewDoc` (`preview_doc`: styled text, spans, images) in
  the worker thread, shown by `preview_doc_view` (layer 2). The viewer has
  no format-specific branch.
- **Shared building blocks**: the summary card (`preview_doc_title()`,
  `preview_doc_field()`...) and text tables (`PreviewTable`: aligned
  columns, numbers right-aligned, zebra rows, row numbers).
- **Text instead of widgets**: tables, cards and listings are styled text in
  the same text view as everything else, not `GtkColumnView` or tree
  widgets. Search (`/`, `n`), selection, copy and the minimap keep working
  in every format, and the parsers are unit-tested on plain data.
- **Own parsers where they give more**: certificates use an own DER / X.509
  parser (more details, no TLS backend needed); media details come from
  `media_tags` (ID3, FLAC, Ogg, MP4, WAV), not GStreamer.
- **Tools**: fixtures in `tests/fixtures/preview/`; `make bench-preview`
  (latency); `make preview-coverage DIR=...` (share of files per level);
  one `shot` per format in `tests/ui/formats.sh` (part of `make ui-test`);
  `LOOKIN_DEBUG_PREVIEW=1` prints the time from selection to display.

## Coverage (updated 2026-10-30)

| Format | Level | Module |
|--------|-------|--------|
| Text and source code (syntax colours, line numbers, git bars; UTF-16 / Windows-1252 detected; large files load on scroll) | L3 | `viewer`, `text_style`, `text_encoding`, `text_chunks` |
| Markdown (Mermaid / math blocks labelled as source) | L3 | `markdown` |
| HTML | L3 | `html` |
| Org | L3 | `orgmode` |
| Images: zoom, EXIF details and orientation, animated GIF, RAW embedded previews | L3 | `image_preview`, `exif`, `animation`, `gif_decoder` |
| PDF (poppler, optional): progressive pages, PgDn / PgUp, zoom | L3 | `pdf` |
| Video / audio: tags, cover art, codecs, duration | L3 | `video`, `audio`, `media_tags` |
| CSV / TSV | L3 | `csv` |
| JSON, JSONC, JSON Lines | L3 | `json_view` |
| Diffs and patches | L3 | `diff_view` |
| Logs | L3 | `log_view` |
| Jupyter notebooks | L3 | `notebook` |
| Spreadsheets `.xlsx` / `.ods` | L3 | `spreadsheet` |
| SQLite (libsqlite3, optional) | L3 | `sqlite_view` |
| Fonts | L3 | `font_info` |
| Word `.docx`: fonts, colours, paragraph layout, lists, tables, header / footer | L3 | `docx`, `docx_styles`, `doc_writer` |
| OpenDocument `.odt`, RTF | L2 | `odf`, `rtf` |
| Word 97-2003 `.doc`: same as `.docx` but pictures (Word 6 / 95: text, L2) | L3 | `doc_reader`, `doc_writer` |
| Excel 97-2003 / 5.0 `.xls` | L3 | `xls_reader`, `spreadsheet` |
| PowerPoint `.pptx`: slides drawn with their layout, master and theme | L3 | `pptx`, `pptx_shapes`, `pptx_build`, `slide_render` |
| PowerPoint 97-2003 `.ppt` / `.pps`: slides drawn from their drawings | L3 | `ppt_reader`, `slide_render` |
| OpenDocument presentations `.odp` | L2 | `odf` |
| Other OLE2 compound files (properties, streams) | L1 | `ole_reader`, `ole_props`, `office97` |
| Mail messages `.eml` / `.emlx`, Outlook `.msg`, TNEF `winmail.dat` | L3 | `mail_message`, `mail_view`, `mail_msg`, `mail_tnef` |
| Mailboxes (mbox), Maildir and MH folders | L2 | `mail_box`, `mail_dir` |
| Outlook stores `.pst` / `.ost`, Outlook Express `.dbx` | L1 | `mail_store` |
| EPUB | L2–L3 | `epub` |
| Man pages (gzipped too) | L2 | `man_view` |
| Archives (ZIP, tar, gzip, ar; 7z, RAR, xz, ISO... with libarchive) | L2 | `archive_listing` |
| Certificates, keys, requests | L2 | `cert_view` |
| Calendars, contacts, `.desktop` | L2 | `card_view` |
| gettext catalogs `.po` / `.mo` | L2 | `gettext_view` |
| ELF executables / libraries / objects | L1 | `elf_info` |
| Directories | L2 | `dir_preview`, `dir_summary` |
| Any other binary | L0 | `hex_dump` |

`make preview-coverage DIR=/usr/share`: 2.3 % of 91,731 files end at the hex
dump (13.7 % before the preview work). Most of the rest are TeX font metrics
(`.tfm`, `.vf`, `.pfb`, `.pfm`) and files without an extension.

### Targets

- **Coverage**: under 2 % of the files of a real home directory at L0.
- **Fidelity**: L3 for every common format except archives (L2).
- **Speed**: p95 time to first paint per format within the budgets of
  rule 1.

## Format notes

- **Old Office files** (`.doc`, `.xls`, `.ppt`): OLE2 compound files read by
  `ole_reader` (FAT, DIFAT, mini stream; loops and out-of-range sectors
  rejected). One `FILE_KIND_OLE`; the format comes from the stream names
  (`WordDocument`, `Workbook` / `Book`, `PowerPoint Document`), the
  extension only breaks ties. The property sets (`ole_props`) give the
  summary card. Encrypted files show the card and "Encrypted"; a parse
  failure or another compound file (`.msi`, `.vsd`...) shows the card and
  the stream list. `.doc` formatting comes from the style sheet (STSH, base
  styles and their UPXs), the font table and the CHPX / PAPX FKPs; the
  header and footer from the header stories (PlcfHdd). Lists are shown as
  bullets (numbering formats are not read). LibreOffice cannot write Word 95 / Excel 5, so those paths are
  tested with files built in the tests (`tests/ole_builder.h`).
- **Slides** (`.pptx`, `.ppt`): drawn with cairo and Pango into one
  picture per slide in the worker thread (`slide_render`); the speaker notes
  stay text below each picture, so they can be searched. Charts and
  diagrams show as labelled boxes, metafile pictures (EMF, WMF) are not
  drawn, and `.ppt` text keeps the text type the file gives it (LibreOffice
  exports titles as plain text boxes). LibreOffice also writes its default
  drawing style (blue fill and outline) on exported `.ppt` text boxes
  without showing it: those two values are skipped on text boxes.
- **Mail**: `mail_message` parses RFC 5322 / MIME (encoded words, any
  charset, base64 / quoted-printable, RFC 2231 names; large parts listed,
  not decoded). Remote content is never fetched. mbox files can be
  gigabytes: only their beginning and end are read; `PgDn` / `PgUp` step
  from the list into each message. Maildir / MH folders get the mailbox
  table instead of the directory listing. `.msg` goes through `ole_reader`
  (compressed RTF bodies through `rtf`); encrypted ones are recognized by
  their `IPM.Note.SMIME` class. TNEF dates are taken as UTC when the MAPI
  submit time is missing.
- **Outlook stores** (`.pst`, `.ost`, `.dbx`): libpff is not available, so
  they stay at L1 (format, ANSI or Unicode, size with a truncation check,
  encryption, message count of a `.dbx` folder); the card suggests
  exporting with `readpst` / `undbx`.

### Out of scope

Exact Office page layout, CAD files, Photoshop layers, 3D models, encrypted
files (they fall to L1 or L0); Word for DOS / Word 2, Excel 2–4 (not
compound files); images and charts embedded in Office files; sending or
fetching mail, decrypting S/MIME or PGP (shown as "Encrypted message" with
the sender and subject), mail client databases (previewed as SQLite).

## Known issues

- **GTK warning "Trying to snapshot GtkGizmo ... without a current
  allocation"**: after a wide table preview followed by narrower ones, GTK
  sometimes draws the trough of the preview's own scrollbar before
  allocating it. Nothing is drawn wrong. It does not happen with plain text
  previews; clearing the buffer first, overlay scrollbars off, a forced
  resize and an `ALWAYS` scrollbar policy did not help. `make ui-test`
  tolerates exactly this message for now; the cause is still to be found.

## Adding a format: definition of done

- [ ] Detected by content (magic number) and by extension; unit test in
      `tests/test_file_types.c`.
- [ ] Parser in a layer 1 module with `tests/test_<module>.c`, including
      truncated and corrupted fixtures (registered in the Makefile `TESTS`
      / `TEST_DEPS_*` and in `tools/check_layers.sh`).
- [ ] One case in `builder_for()` (`preview_formats`).
- [ ] Bounded read, worker thread, cancellation honoured.
- [ ] Falls back one level down on parse failure.
- [ ] `m` toggles rendered / raw.
- [ ] Within the latency budget in `make bench-preview`.
- [ ] Fixture + `shot` in `tests/ui/formats.sh`; screenshot checked.
- [ ] File-type icon in `file_icon()` if new.
- [ ] Coverage table and preview list above, `CHANGELOG.md` updated;
      `make check` passes.
- [ ] One commit per format.
