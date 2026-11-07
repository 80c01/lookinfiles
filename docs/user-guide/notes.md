# Notes

The notes view is a notebook manager inside LookinFiles: notes sorted into
notebooks, with a trash. Open it with `Ctrl+U` (again to go back to the
files) or the notes button at the top of the explorer.

A first notebook, **General**, holds a welcome note.

## Note formats

| Format | File | Edited in |
|---|---|---|
| Markdown | `.md` | An editor tab; the preview shows it rendered |
| Org-mode | `.org` | An Org-mode editor tab (see [Org-mode](org-mode.md)) |
| Plain Text | `.txt` | An editor tab |
| Canvas | a `.canvas` folder | The **Canvas** tab (see below) |

The right-click menu converts a note to another text format, and exports a
Canvas note to a Markdown note.

## Working with notes

| Key | Action |
|---|---|
| `a` | New note in the selected notebook (asks for a title and a format) |
| `Shift+A` | New notebook |
| `Enter` | Edit the note |
| `F2`, `r` or `e` | Edit the note's title, format and notebook, or the notebook's name and icon |
| `i` | Change the notebook's icon |
| `m` | Move the note to another notebook |
| `d` or `Delete` | Move the note to the trash, or delete the notebook |
| `Shift+R` | Restore the selected note from the trash to its notebook |
| `Shift+K` / `Shift+J` | Move the note or notebook up / down |
| `o` | Sort: manual order, A–Z, Z–A |

Selecting a note shows it in the preview; selecting a notebook shows a
summary. The **Trash** at the bottom of the tree keeps deleted notes until
you empty it (right-click the Trash, **Empty Trash…**).

## Opening a note from the command line

```bash
lookinfiles -n Ideas            # the note "Ideas", in any notebook
lookinfiles -n Work/Meeting.org # "Meeting" in the notebook "Work", Org-mode
```

A note that does not exist is created, with its notebook. The extension
(`.md`, `.org`, `.txt`) sets the format of a new note.

## Canvas notes

A Canvas note is a freeform workspace of text boxes, hand-drawn shapes,
tables and pictures, placed where you want them. It opens in a tab of the
editor panel, named after the canvas, and is saved automatically. Several
canvases can be open at once, each in its own tab; drag a tab like an editor
tab to another pane, to the edge of a pane to split it, or to the preview
panel. A canvas tab belongs to the workspace it was opened in: it hides with
the workspace and comes back with it (opening it from another workspace
moves it there).

### Tools and blocks

A floating tool bar sits at the top of the page. Pick a tool, then press
and drag on the page; the letter in brackets picks it from the keyboard
(when you are not typing in a block).

| Tool | Use |
|---|---|
| Select (`V`) | Select, move and resize blocks; drag on the empty page to select the blocks inside the box (`Shift`: add them) |
| Rectangle (`R`), Diamond (`D`), Ellipse (`O`) | Drag to draw the shape (`Shift`: square, circle); a click makes one of the default size; double click it (or `Enter` when it is selected) to type text inside, centred |
| Arrow (`A`), Line (`L`) | Drag from the start to the end (`Shift`: multiple of 45 degrees); an end drawn on a block is bound to it and follows it when it moves (moving the arrow alone frees it) |
| Pencil (`P`) | Draw freehand; the pencil stays chosen for the next stroke |
| Text box (`T`) | Click where the text goes (double click or `Ctrl+Enter` also work) |
| Table (`B`) | Click where the table goes: a 3 × 3 grid of editable cells with `+ Row`, `- Row`, `+ Col`, `- Col` controls |
| Eraser (`E`) | Click or drag over blocks to delete them |

After drawing a shape, the tool goes back to Select (`Esc` also does).
The menu at the end of the bar holds **Snap to Grid**, the alignment
commands, the drawing order, **Lock / Unlock**, **Crop Selected Picture**,
and **Export as PNG** / **Export as SVG**: a picture of the blocks (theme
colours, PNG at twice the page scale) written next to the note as
`<title>.png` or `<title>.svg`.

Pictures come from `Ctrl+V` with an image copied, or image files dropped
onto the page; double click a picture to crop it.

### Properties panel and format bar

- **Shapes**: a panel on the left (while a drawing tool is chosen, or a
  shape is selected) sets the stroke colour, fill (none, hatched, solid;
  closed shapes only), stroke width, and hand-drawn or clean style. The
  choice applies to the selected shapes and to the next ones drawn.
- **Text**: while a text box is edited or selected, a bar floats just above
  it (below it near the top of the page) and follows it: paragraph style
  (headings, body, quote), bold, italic, underline, strikethrough, text
  colour, more styles (highlight, code, fonts, sizes), lists, task
  checkboxes and indent. Bold, italic, underline and strikethrough show as
  pressed when the text at the cursor (or the whole selection) has them.

### Canvas actions and shortcuts

| Action | How |
|---|---|
| Move a block | Drag the handle bar above it (or drag a picture or a shape directly) |
| Resize a block | Drag its bottom-right corner (pictures keep proportions) |
| Rotate a shape | Select a rectangle, a diamond or an ellipse alone and drag the round handle above it: it turns around its centre with its text (`Shift`: steps of 15 degrees); arrows bound to it stay on its outline |
| Edit an arrow or a line | Click its stroke to select it: a handle shows on each point. Drag an end anywhere (dropped on a block, it binds to it), drag the faint handle in the middle of a segment to add a point and bend it into a curve, drag a point to reshape it (`Shift`: multiple of 45 degrees), double click a point between the ends to remove it (or click it, filled once picked, then press `Delete`) |
| Select several blocks | Drag on the empty page around them, or `Shift+click` each one (again to take it out); moving one moves them all |
| Snap & Smart Guides | Magnetic snapping to nearby block edges and grid (**Snap to Grid** in the tool bar menu) |
| Align blocks | Select two blocks or more, then the tool bar menu: left, centers, right, top, middles, bottom, distribute |
| Predefined styles | Paragraph style menu of the format bar: Heading 1, Heading 2, Body text, Quote (`Ctrl+Alt+1`, `2`, `0`, `Q`) |
| Text styling | `Ctrl+B` (bold), `Ctrl+I` (italic), `Ctrl+U` (underline), `Ctrl+Shift+S` (strikethrough), highlight, monospace, font family, text color |
| Lists & Indents | Bullet list, numbered list, indent more / indent less in the format bar; Enter auto-continues lists |
| Interactive Checkboxes | Checkbox button or type `- [ ] `; click `[ ]` / `[X]` or press `Ctrl+Space` (or `Ctrl+Shift+C`) to toggle |
| Select block / stop editing | `Esc` |
| Next / previous block | `Tab` / `Shift+Tab` |
| Move selected blocks | Arrow keys (`Shift` for larger steps) |
| Edit selected text box, or the text of a shape | `Enter` (a shape: also a double click) |
| Delete selected blocks | `Delete` or `Backspace` |
| Drawing order | `Ctrl+]` / `Ctrl+[` one step up / down, `Ctrl+Shift+]` / `Ctrl+Shift+[` to the front / back (also in the tool bar menu); moving a block keeps its place in the order |
| Lock blocks | `Ctrl+Shift+K` (or the tool bar menu) on the selected blocks: they can no longer be moved, resized, deleted or caught by a selection box (a dashed border when selected; text can still be edited); again to unlock |
| Copy, cut, paste, duplicate | `Ctrl+C`, `Ctrl+X`, `Ctrl+V` (at the pointer, also into another Canvas note, pictures included), `Ctrl+D`; arrows copied with their blocks stay bound to the copies |
| Zoom | `Ctrl` + mouse wheel (around the pointer) or `Ctrl+=` / `Ctrl+-`; `Ctrl+0`, or a click on the scale above the page, goes back to 100 % |
| Move the view | Drag with the middle button, or hold `Space` and drag (when not typing in a block) |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` (or `Ctrl+Y`): blocks added, deleted, moved, resized, rotated, restyled, aligned or cropped; while typing in a text box or a table cell, they undo its text |
| Close Canvas tab | `Ctrl+W`, or middle click on the tab |
| Move Canvas tab | `Ctrl+Alt+Arrow`, the tab menu (right click: Move Right / Left / Down / Up, Close Tab), or drag the tab |

## Where notes are kept

Each notebook is a folder in `~/.config/lookinfiles/notes/`, and each note
a normal file in it, so you can back them up, sync them or open them with
other programs.
