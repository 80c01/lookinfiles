# TASK: preview every file, in a blink, as its native tool would

Lookin's value is the preview panel: select a file and see its content at
once, looking as close as possible to what the application made for that
format would show. This plan turns that goal into measurable rules, a shared
infrastructure, and a roadmap of formats ordered by value and effort.

It supersedes the "Preview" section of `TODO.md` (keep that section as a
pointer to this file).

---

## 1. Rules every preview follows

1. **In a blink.** Something useful is on screen within **50 ms** of the
   selection for files up to `PREVIEW_BYTES`, and within **150 ms** for
   rendered formats (PDF page, image, office document). Heavier work fills
   in afterwards; it never delays the first paint.
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

---

## 2. Coverage (updated 2026-10-30)

| Format | Level | Module |
|--------|-------|--------|
| Text and source code (syntax colours, line numbers, git bars; UTF-16 / Windows-1252 detected; large files load on scroll) | L3 | `viewer`, `text_style`, `text_encoding`, `text_chunks` |
| Markdown (Mermaid / math blocks labelled as source) | L3 | `markdown` |
| HTML | L3 | `html` |
| Org | L3 | `orgmode` |
| Images: zoom, EXIF details and orientation, animated GIF / WebP, RAW embedded previews | L3 | `image_preview`, `exif`, `animation` |
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
| Word `.docx`, OpenDocument `.odt`, RTF | L2 | `docx`, `odf`, `rtf` |
| Presentations `.pptx` / `.odp` | L2 | `pptx`, `odf` |
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
dump (13.7 % before this work). Most of the rest are TeX font metrics
(`.tfm`, `.vf`, `.pfb`, `.pfm`) and files without an extension.

### Design decisions that differ from the plan below

- **Text-based tables and cards instead of `GtkColumnView` / tree widgets.**
  Every structured preview is a `PreviewDoc` (styled text, spans, images)
  built in the worker thread and shown in the same text view as everything
  else. Search (`/`, `n`), selection, copy and the minimap keep working in
  every format, and the parsers are unit-tested on plain data. The JSON /
  XML tree toggle is therefore not done (see Remaining).
- **The registry is `preview_formats` (layer 1)** with the display in
  `preview_doc_view` (layer 2), instead of a single layer-2
  `preview_registry`.
- **Certificates are decoded with an own DER / X.509 parser**, not
  `GTlsCertificate`: it gives more details (key size, usages, fingerprints)
  and needs no TLS backend at run time.
- **Media details are read by `media_tags`** (ID3, FLAC, Ogg, MP4, WAV), not
  the GStreamer discoverer: no new dependency.

### Known issues

- **GTK warning "Trying to snapshot GtkGizmo ... without a current
  allocation"**: after a wide table preview followed by narrower ones, GTK
  sometimes draws the trough of the preview's own scrollbar before
  allocating it. Nothing is drawn wrong. It does not happen with plain text
  previews; clearing the buffer first, overlay scrollbars off, a forced
  resize and an `ALWAYS` scrollbar policy did not help. `make ui-test`
  tolerates exactly this message for now; the cause is still to be found.

### Remaining

- JSON / XML / YAML / TOML tree view with folding.
- Raise `docx` toward L3 (page width, fonts, headers).
- A placeholder (name, size, kind) when a structured preview takes longer
  than ~100 ms (none measured over the budget so far).
- Previews of TeX fonts (`.tfm`, `.pfb`) and Python bytecode (`.pyc`), the
  largest remaining L0 groups.

---

## 3. Phase 0 — Infrastructure (done)

- [x] **Preview format registry**: `preview_formats` (builders, one per
      `FileKind`), `preview_doc` (the document format), `preview_doc_view`
      (display). ELF, hex dump and docx moved onto it with no visible change.
- [x] **Shared building blocks**: summary card (`preview_doc_title()`,
      `preview_doc_field()`...), text tables (`PreviewTable`: aligned
      columns, numbers right-aligned, zebra rows, row numbers).
- [ ] Tree view (see Remaining).
- [x] **Zoom** for images and PDF pages (`+` / `-` / `0`, `Ctrl+wheel`).
- [x] **Fixture corpus**: `tests/fixtures/preview/` (most generated by small
      scripts; archives, certificates, catalogs built by the unit tests).
- [x] **Latency benchmark**: `make bench-preview`.
- [x] **UI test**: a second session in `make ui-test` shoots one preview per
      format (`tests/ui/formats.sh`).
- [x] **Coverage report**: `make preview-coverage DIR=...`.
- [x] **Detection** by content for every new kind (ZIP-based office formats
      and e-books, tar, gzip, bzip2, xz, zstd, 7z, RAR, ar, RPM, CAB, SQLite,
      fonts, PEM / DER, `.mo`, JSON without extension, diffs, `.TH` man
      pages, vCard / iCalendar, `.desktop`, RTF, RAW photos, UTF-16 text).
- [x] `LOOKIN_DEBUG_PREVIEW=1` prints the time from selection to display.

## 4. Phase 1 — Quick wins, GLib only (done)

- [x] **CSV / TSV**: separator sniffing, RFC 4180 quoting, header detection.
- [x] **JSON**: pretty-printed and coloured, minified, JSONC / JSON5, JSON
      Lines; invalid JSON falls back to plain text.
- [x] **Diff / patch**: summary of changed files, coloured lines, hunk
      counts followed.
- [x] **Archives** `.zip`, `.jar`, `.tar`, `.tar.gz`, `.a`, `.deb`, gzip of a
      single file (its content shown with its own preview).
- [x] **Certificates** PEM / DER, chains, keys (named only), requests.
- [x] **Calendar, contacts, `.desktop`**.
- [x] **Logs**: levels and timestamps coloured, end of large logs, opened
      scrolled to the end.
- [x] **Image details**: EXIF summary and orientation.
- [x] **Fonts**: names, details, samples in the font (Pango ≥ 1.56).
- [x] **Man pages**, gzipped ones too.

## 5. Phase 2 — Documents (done but docx)

- [x] **Jupyter notebooks** (rendered as Markdown, images as data URIs).
- [x] **Spreadsheets** `.xlsx` / `.ods`: the first 3 sheets as grids, number
      / date / percent / currency formats.
- [x] **OpenDocument text** `.odt`.
- [x] **Presentations** `.pptx` / `.odp`.
- [x] **EPUB**.
- [x] **RTF**.
- [ ] Raise `docx` toward L3 (see Remaining).

## 6. Phase 3 — Optional libraries (done)

- [x] **SQLite** (`HAVE_SQLITE`): tables, row counts, columns, first rows;
      without the library, the header details.
- [x] **libarchive** (`HAVE_LIBARCHIVE`): 7z, RAR, xz, bzip2, zstd, lzip,
      ISO, cpio, RPM, CAB.
- [x] **Media metadata**: `media_tags` (no GStreamer dependency).
- [x] **HEIC / AVIF / JPEG XL / WebP without a loader**: the message names
      the package to install; RAW photos show their embedded JPEG.
- [x] **Markdown extras**: Mermaid, Graphviz, PlantUML and math blocks
      labelled as source.

## 7. Phase 4 — Fidelity polish

- [x] PDF: progressive rendering (3 pages first), `PgDn` / `PgUp`, zoom.
- [x] Animated GIF / WebP.
- [x] SVG rendered at the displayed size (the SVG loader rasterizes at the
      requested size: nothing to change).
- [x] Large text: the next chunks load on scroll (up to 8 MB).
- [x] Encoding detection (UTF-8 BOM, UTF-16 with or without BOM, UTF-32,
      Windows-1252), shown in the status bar.
- [ ] XML / YAML / TOML tree (see Remaining).
- [x] Remote machines: previews read through the SSHFS mount with the same
      bounded reads.

## 8. Not worth it

Old binary Office files (`.doc`, `.xls`, `.ppt`), exact Office page layout,
CAD files, Photoshop layers, 3D models, encrypted files. They fall to L1
(name, size, detected type) or L0 (hex dump).

---

## 9. Definition of done for a format

- [ ] Detected by content (magic number) and by extension; unit test in
      `tests/test_file_types.c`.
- [ ] Parser in a layer 1 module with `tests/test_<module>.c`, including
      truncated and corrupted fixtures (registered in the Makefile `TESTS`
      / `TEST_DEPS_*` and in `tools/check_layers.sh`).
- [ ] Bounded read, worker thread, cancellation honoured.
- [ ] Falls back one level down on parse failure.
- [ ] `m` toggles rendered / raw.
- [ ] Within the latency budget in `make bench-preview`.
- [ ] Fixture + `shot` in `make ui-test`; screenshot checked.
- [ ] File-type icon in `file_icon()` if new.
- [ ] README preview list and `CHANGELOG.md` updated; `make check` passes.
- [ ] One commit per format.

## 10. How progress is measured

- **Coverage**: share of files in a real home directory that end at L0
  (coverage report from Phase 0). Target: under 2 %.
- **Fidelity**: the table in section 2 kept up to date; target L3 for every
  format in Phases 1 and 2 except archives and presentations (L2).
- **Speed**: p95 time to first paint per format from `make bench-preview`,
  within the budgets of rule 1.

## Suggested order

1. Phase 0 registry + summary card + table view (unlocks everything else).
2. CSV, JSON, diff, archives (most common in a developer's tree).
3. Certificates, logs, image details, `.ipynb`.
4. Spreadsheets, `.odt`, presentations, EPUB.
5. SQLite and libarchive behind `HAVE_*`.
6. Polish: zoom, PDF navigation, chunked large text, encodings.
