# UI conventions

Details behind the UI rules of [AGENTS.md](../AGENTS.md).

## Colours, text tags and fonts

- **Colours come from the palette** (`palette.h`): `palette_color(PAL_...)`
  in C, `@role` names in CSS added with `theme_add_css()`. Never write a
  colour literal elsewhere (`make check` rejects `#rrggbb` outside
  `palette.c`); pick the role that matches the meaning (e.g. `fg_subtle`
  for metadata, `bg_block` for code blocks) or add a role.
- **Text tags with colours or fonts** are made with `text_tag_create()` /
  `text_tag_ensure()` and role values (`"@fg_subtle"`), never with
  `palette_color()`: otherwise they keep the old colours after a theme
  change. Labels take colours from CSS classes rather than markup where
  they live long (e.g. the help panel).
- **Fonts come from the font roles** (`fonts.h`): `@font_*` / `@size_*`
  in CSS, `fonts_family(FONT_CODE)` and `FONT_SCALE_*` in text tags.
  File contents (code and plain text, preview and every editor mode) and
  the terminal use the code font; the interface and rendered documents the
  UI font. `make check` rejects font families and pixel sizes elsewhere.
- Text interpolated into Pango markup must be escaped: use
  `g_markup_printf_escaped()`, never `g_strdup_printf()`, with names, titles,
  hosts, key labels or any user-provided string.
- The binary `lookinfiles` and `build/` are generated — never edit them.


## UI conventions (keep views consistent)

The explorer views (files, bookmarks, notes, remote) share one vocabulary.
Check these before adding or changing an action, a tooltip or a dialog:

- **Same action, same key, in every view**: `a` add, `Shift+A` new folder /
  notebook, `F2 / r / e` edit or rename, `d / Delete` delete (with
  confirmation, or move to a trash), `Shift+K / Shift+J` move up / down,
  `o` cycle the sort of the current view, `Enter` open. A key that has no
  meaning in a view does nothing there (never acts on another view).
- **Tooltips**: Title Case action, then the keys in parentheses, primary key
  first: `Edit Selected (F2 / r / e)`, `Delete Selected (d / Delete)`.
- **Context menus**: same labels as tooltips, `…` (single character) when a
  dialog follows, key hint written `Shift+A`, `Delete`, `F2`.
- **No external windows**: every interaction stays inside the application
  window. Prompts, confirmations and messages are `AppDialog`s (drawn over
  the window); panels such as the help cheat sheet are overlays or panes;
  menus are `GtkPopover`s anchored to a widget of the window. Never open a
  top-level window (`gtk_window_new()`, `GtkAlertDialog`, `GtkFileDialog`,
  file choosers...): `make check` rejects it outside `main.c`.
- **Dialogs**: every button has a mnemonic (`_Cancel`, `_Save`); the confirm
  button is a verb (`_Create`, `_Rename`, `_Move to Trash`), never `OK`;
  information-only messages have a single `_Close` button; destructive
  confirmations default to Cancel.
- **Empty states**: an empty container shows `(empty)`; an empty view shows
  `No <items> yet — press a to add one`.
- **Icons**: buttons and fixed tab headers use GTK theme icons through
  `icons.c` (`icon_button_new()`, `icon_button_set_content()`,
  `icon_label_new()`), never emoji or text symbols; use `edit-delete` for a
  permanent delete and `user-trash` for a move to trash. Exception: the
  option toggles of Search in Files show text (`Aa`, `ab`, `.*`, `a…z`), as
  in other editors. Files show the VS Code (Seti)
  file-type icons wherever they are listed (`file_type_image_set()`, or
  `file_type_paintable()` inside text), coloured by the `icon_*` palette
  roles. Every explorer view looks the same: the other rows get a glyph in
  the same style (`glyph_image_set()`: folder, notebook, trash, server, URL
  link, missing target), set with `explorer_row_set_glyph()`; files and
  notes set `explorer_row_set_icon_name()`. Labels hold the bare name. The
  only emoji left in the tree is the icon a user chose for a notebook. The CSS colours `label`
  and `image` nodes together (symbolic icons follow the text colour).
  The icons are bundled in the binary (`icons/`, compiled as a GResource by
  the Makefile, licence in `icons/COPYING`) so they render the same with any
  icon theme. To add one: copy a *current* Adwaita symbolic SVG (no
  `transform`, enforced by `make check`) as
  `icons/scalable/actions/lookin-<name>-symbolic.svg`, list it in
  `icons/lookin.gresource.xml`, and if `<name>` is not built into libgtk
  add a `FALLBACKS` entry in `icons.c` ending with a built-in icon (used when
  SVG cannot be loaded). The file-type icons `lookin-ft-*` are generated:
  run `tools/gen_file_icons.py` on the VS Code Seti theme files (see its
  header) instead of editing them. Check with `make ui-test`, `UI_ICON_THEME=hicolor
  make ui-test` and `LOOKIN_DEBUG_ICONS=1 ./lookin`.
- **Preview text views** all use `set_view_text_padding()` (10 px sides).
- **Names**: `Org-mode`, `Markdown`, `Plain Text`, `Canvas`; view titles EXPLORER,
  BOOKMARKS, NOTES, REMOTE.
- Keep `help.c` (`KEY_HELP`) and `docs/keyboard.md` in sync.

