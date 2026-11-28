/* Floating key-binding cheat sheet ('?' / top bar Help button). Keep KEY_HELP in sync with README.md. */
#include "chrome/help.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "widgets/panels.h"
#include "core/app_events.h"
#include "explorer/nav_keys.h"
#include "games/games.h"
#include "chrome/user_guide_search.h"

/* Modes in which an entry is shown (global mode, nav_keys.h); 0 = every mode */
#define NP (1u << NAV_STYLE_NOTEPAD)
#define VI (1u << NAV_STYLE_VIM)
#define EM (1u << NAV_STYLE_EMACS)

/* Cheat sheet entry: section header, key column, description, modes.
 * Sections and descriptions are translated; keys only when they hold words
 * (mouse actions: NC_("key", ...)), key names stay as written. */
typedef struct {
    const gchar* section;
    const gchar* key;
    const gchar* description;
    guint modes;
} KeyHelpEntry;

static const KeyHelpEntry KEY_HELP[] = {
    /* Explorer panel */
    {N_("Explorer panel"), "Down / Up", N_("Move down / up"), 0},
    {N_("Explorer panel"), "j / k", N_("Move down / up"), VI},
    {N_("Explorer panel"), "C-n / C-p", N_("Move down / up"), EM},
    {N_("Explorer panel"), "Home / End", N_("First / last entry"), 0},
    {N_("Explorer panel"), "gg / G", N_("First / last entry"), VI},
    {N_("Explorer panel"), "Ctrl+Home / Ctrl+End", N_("First / last entry"), NP},
    {N_("Explorer panel"), "M-< / M->", N_("First / last entry"), EM},
    {N_("Explorer panel"), "Page Down / Page Up", N_("Page down / up"), 0},
    {N_("Explorer panel"), "Ctrl+F / Ctrl+B", N_("Page down / up"), VI},
    {N_("Explorer panel"), "C-v / M-v", N_("Page down / up"), EM},
    {N_("Explorer panel"), "zz", N_("Center selected entry in the panel"), VI},
    {N_("Explorer panel"), "zt / zb", N_("Scroll selected entry to top / bottom of the panel"), VI},
    {N_("Explorer panel"), "C-l", N_("Scroll selected entry to the center, top, bottom in turn"), EM},
    {N_("Explorer panel"), "Space", N_("Expand / collapse directory"), 0},
    {N_("Explorer panel"), "Right", N_("Expand directory or archive (or step into first child)"), 0},
    {N_("Explorer panel"), "l", N_("Expand directory (or step into first child)"), VI},
    {N_("Explorer panel"), "Left", N_("Collapse directory or jump to parent"), 0},
    {N_("Explorer panel"), "h", N_("Collapse directory or jump to parent"), VI},
    {N_("Explorer panel"), "C-f / C-b", N_("Expand directory (or step into first child) / collapse it or jump to parent"), EM},
    {N_("Explorer panel"), "/", N_("Search by substring (n / N: next / previous match)"), VI},
    {N_("Explorer panel"), "Ctrl+F", N_("Search by substring (Enter / F3: next match, Shift+Enter / Shift+F3: previous, Esc: close)"), NP},
    {N_("Explorer panel"), "F3 / Shift+F3", N_("Next / previous match of the last search"), NP},
    {N_("Explorer panel"), "C-s / C-r", N_("Search forward / backward as you type (again: next / previous match, RET: stop, C-g: back to the start)"), EM},
    {N_("Explorer panel"), "t", N_("Open the selected file in the dedicated editor panel (focus in the editor)"), VI},
    {N_("Explorer panel"), "C-o", N_("Show the selected file in the dedicated editor panel (focus stays in the explorer)"), EM},
    {N_("Explorer panel"), NC_("key", "Enter / Double-click"), N_("Expand/collapse directory, open file in editor (a video in a Video tab, a Jupyter notebook in a notebook tab), or open bookmark (Ctrl+Enter to enter directory)"), 0},
    {N_("Explorer panel"), "Backspace / Alt+Up", N_("Go to the parent directory (or return to files view from bookmarks)"), 0},
    {N_("Explorer panel"), "Ctrl+D", N_("Bookmark selected entry or current directory (edit / remove if already bookmarked)"), 0},
    {N_("Explorer panel"), "Ctrl+A", N_("Switch between files and bookmarks views in Explorer"), 0},
    {N_("Bookmarks view"), "Enter / Right", N_("Open bookmark (directory, file or URL) / expand folder"), 0},
    {N_("Bookmarks view"), "l", N_("Open bookmark / expand folder"), VI},
    {N_("Bookmarks view"), "C-f", N_("Open bookmark / expand folder"), EM},
    {N_("Bookmarks view"), "a / Shift+A", N_("Add bookmark / new folder (in selected folder)"), 0},
    {N_("Bookmarks view"), "F2 / r", N_("Edit selected: name, location and folder (folder: name, icon and parent)"), 0},
    {N_("Bookmarks view"), "d / Delete", N_("Delete selected bookmark or folder"), 0},
    {N_("Bookmarks view"), "Shift+K / Shift+J", N_("Move selected item up / down"), 0},
    {N_("Bookmarks view"), "o", N_("Cycle sort: A-Z / Z-A"), 0},
    {N_("Bookmarks view"), NC_("key", "Right-click"), N_("Bookmark context menu"), 0},
    {N_("Notes view"), "a / Shift+A", N_("Create new note (Markdown, Org-mode, Plain Text, Canvas) / new notebook"), 0},
    {N_("Notes view"), "Enter", N_("Edit the note (editor tab, or the Canvas tab for a Canvas note)"), 0},
    {N_("Notes view"), "F2 / r", N_("Edit note (title, format and notebook) or notebook (name and icon)"), 0},
    {N_("Notes view"), "m", N_("Move note to another notebook (edit dialog, on its notebook)"), 0},
    {N_("Notes view"), "d / Delete", N_("Delete note (to Trash) or notebook"), 0},
    {N_("Notes view"), "Shift+R", N_("Restore note from Trash to original notebook"), 0},
    {N_("Notes view"), "Shift+K / Shift+J", N_("Move selected note or notebook up / down"), 0},
    {N_("Notes view"), "o", N_("Cycle sort: manual / A-Z / Z-A"), 0},
    {N_("Notes view"), NC_("key", "Right-click"), N_("Context menu: 1-click format conversion (Text, Markdown, Org), Canvas export to Markdown, rename, move"), 0},
    {N_("Canvas tab"), "V / R / D / O / A / L", N_("Tool: select, rectangle, diamond, ellipse, arrow, line (not while typing)"), 0},
    {N_("Canvas tab"), "P / T / B / E", N_("Tool: pencil, text box, table, eraser; Esc: back to select"), 0},
    {N_("Canvas tab"), NC_("key", "Drag with a shape tool"), N_("Draw the shape (Shift: square, circle, 45-degree line); arrow ends on a block follow it"), 0},
    {N_("Canvas tab"), NC_("key", "Double-click / Ctrl+Enter"), N_("New text box (at the click, or below the selected block)"), 0},
    {N_("Canvas tab"), NC_("key", "Double-click a shape"), N_("Type text inside it (also Enter on a selected shape; Esc when done)"), 0},
    {N_("Canvas tab"), "Ctrl+V", N_("Paste copied blocks, or a picture as a new block (text outside a text box: new text box)"), 0},
    {N_("Canvas tab"), NC_("key", "Right-click"), N_("Context menu: Paste blocks, a picture or text at the click"), 0},
    {N_("Canvas tab"), "Ctrl+A", N_("Select every block (not the locked ones; while typing: the text)"), 0},
    {N_("Canvas tab"), "Ctrl+C / Ctrl+X / Ctrl+D", N_("Copy / cut / duplicate the selected blocks"), 0},
    {N_("Canvas tab"), "Ctrl+] / Ctrl+[", N_("Bring forward / send backward (Shift: to the front / back)"), 0},
    {N_("Canvas tab"), "Ctrl+G / Ctrl+Shift+G", N_("Group the selected blocks (selected and moved together) / ungroup them"), 0},
    {N_("Canvas tab"), "Ctrl+Shift+K", N_("Lock / unlock the selected blocks (kept in place)"), 0},
    {N_("Canvas tab"), NC_("key", "Drag bar / corner"), N_("Move / resize a block (a picture or shape moves from anywhere on it; a shape resizes by any corner or side; Shift: shape keeps its proportions)"), 0},
    {N_("Canvas tab"), NC_("key", "Drag round handle"), N_("Rotate the selected shape around its centre (Shift: 15-degree steps)"), 0},
    {N_("Canvas tab"), NC_("key", "Drag line point / middle"), N_("Move a point of the selected arrow or line / add one there (a curve)"), 0},
    {N_("Canvas tab"), NC_("key", "Double-click point / Delete"), N_("Remove the point of the selected line (clicked last; not an end)"), 0},
    {N_("Canvas tab"), NC_("key", "Drag on the page / Shift+click"), N_("Select the blocks in the box / add or remove a block (Shift+drag: add)"), 0},
    {N_("Canvas tab"), "Ctrl+B / Ctrl+I / Ctrl+U", N_("Bold / italic / underline (selection, or the text typed next)"), 0},
    {N_("Canvas tab"), "Ctrl+Shift+S / Ctrl+E / Ctrl+Alt+1", N_("Strikethrough / code / heading"), 0},
    {N_("Canvas tab"), "Ctrl+Space / Ctrl+Shift+C", N_("Toggle checkbox under cursor (- [ ] / - [X])"), 0},
    {N_("Canvas tab"), "Esc", N_("In a text box: select the block; on a block: deselect"), 0},
    {N_("Canvas tab"), "Tab / Shift+Tab", N_("Next / previous block"), 0},
    {N_("Canvas tab"), NC_("key", "Arrows / Shift+Arrows"), N_("Move the selected blocks"), 0},
    {N_("Canvas tab"), "Enter / Delete", N_("Edit the selected block / delete the selected blocks"), 0},
    {N_("Canvas tab"), NC_("key", "Ctrl+Wheel / Ctrl+= / Ctrl+-"), N_("Zoom the page in / out (10% to 400%)"), 0},
    {N_("Canvas tab"), "Ctrl+0", N_("Zoom back to 100% (instead of the font sizes)"), 0},
    {N_("Canvas tab"), NC_("key", "Middle drag / Space+drag"), N_("Move the view (Space when not typing)"), 0},
    {N_("Canvas tab"), "Ctrl+Z / Ctrl+Shift+Z", N_("Undo / redo on the page (also Ctrl+Y); while typing: the text"), 0},
    {N_("Canvas tab"), "Ctrl+W", N_("Close the Canvas tab (saved automatically)"), 0},
    {N_("Canvas tab"), NC_("key", "Ctrl+Alt+Arrow / right-click tab"), N_("Move the Canvas tab to another pane, as an editor tab"), 0},
    {N_("Video tab"), "Space / k", N_("Play / pause"), 0},
    {N_("Video tab"), "Left / Right", N_("Seek 5 seconds back / forward"), 0},
    {N_("Video tab"), "Home", N_("Back to the start"), 0},
    {N_("Video tab"), "a", N_("Next audio track (language)"), 0},
    {N_("Video tab"), "s", N_("Next subtitles (then off)"), 0},
    {N_("Video tab"), "Ctrl+W", N_("Close the Video tab"), 0},
    {N_("Video tab"), NC_("key", "Ctrl+Alt+Arrow / right-click tab"), N_("Move the Video tab to another pane, as an editor tab"), 0},
    {N_("Jupyter notebook tab"), "Shift+Enter", N_("Run the cell and go to the next one"), 0},
    {N_("Jupyter notebook tab"), "Ctrl+Enter", N_("Run the cell (in the notebook's Jupyter kernel; a Markdown cell is rendered)"), 0},
    {N_("Jupyter notebook tab"), NC_("key", "Enter / Double-click"), N_("Edit a rendered Markdown cell"), 0},
    {N_("Jupyter notebook tab"), "Ctrl+Up / Ctrl+Down", N_("Previous / next cell"), 0},
    {N_("Jupyter notebook tab"), "Ctrl+Shift+Up / Ctrl+Shift+Down", N_("Select the cells up / down (also Shift+click on a cell's head; moving, deleting, running and the type take them all; Esc drops the selection)"), 0},
    {N_("Jupyter notebook tab"), "Tab / Shift+Tab", N_("On a rendered Markdown cell with headings: fold or unfold its section / fold every section, or unfold them all"), 0},
    {N_("Jupyter notebook tab"), "Ctrl+K Ctrl+O", N_("Show / hide the tagbar: headings of the Markdown cells beside the cells (Enter goes, c collapses / expands all, Esc back, q hides)"), 0},
    {N_("Jupyter notebook tab"), "Alt+Up / Alt+Down", N_("Move the cell up / down"), 0},
    {N_("Jupyter notebook tab"), "Alt+Enter / Alt+Shift+Enter", N_("Insert a code cell below / above"), 0},
    {N_("Jupyter notebook tab"), "Alt+Delete", N_("Delete the cell or the selected cells (Undo in the bar puts them back)"), 0},
    {N_("Jupyter notebook tab"), "Ctrl+Shift+C / Ctrl+Shift+X / Ctrl+Shift+V", N_("Copy / cut the cell or the selected cells / paste the cells copied below"), 0},
    {N_("Jupyter notebook tab"), "Ctrl+Shift+M", N_("Merge the selected cells, or the cell with the next one (Undo in the bar splits them again)"), 0},
    {N_("Jupyter notebook tab"), "Ctrl+S", N_("Save now (also saved by itself after each change)"), 0},
    {N_("Jupyter notebook tab"), "Ctrl+W", N_("Close the notebook tab (saved first)"), 0},
    {N_("Jupyter notebook tab"), NC_("key", "Ctrl+Alt+Arrow / right-click tab"), N_("Move the notebook tab to another pane, as an editor tab"), 0},
    {N_("Help tab"), "F1", N_("Open the user guide (also a click on the top bar's Help button)"), 0},
    {N_("Help tab"), "Tab / Shift+Tab", N_("Select the next / previous link"), 0},
    {N_("Help tab"), NC_("key", "Enter / Click"), N_("Follow the selected / clicked link"), 0},
    {N_("Help tab"), "Shift+H / Shift+L", N_("Back / forward in the pages visited (also the mouse's back / forward buttons)"), 0},
    {N_("Help tab"), "Ctrl+W", N_("Close the Help tab"), NP},
    {N_("Help tab"), "Ctrl+W c", N_("Close the Help tab"), VI},
    {N_("System Monitor tab"), NC_("key", "Click the top bar's CPU chart"), N_("Open the System Monitor (click again: close it)"), 0},
    {N_("System Monitor tab"), NC_("key", "Click a column header"), N_("Sort the processes by that column (again: reverse)"), 0},
    {N_("System Monitor tab"), "Ctrl+F", N_("Filter the processes: only those whose name, user or PID hold every word and that meet every condition such as cpu>10 or mem>100M (Enter / Down: back to the list, Esc: empty the field, again: back to the list)"), NP},
    {N_("System Monitor tab"), "/", N_("Filter the processes: only those whose name, user or PID hold every word and that meet every condition such as cpu>10 or mem>100M (Enter / Down: back to the list, Esc: empty the field, again: back to the list)"), VI},
    {N_("System Monitor tab"), "C-s", N_("Filter the processes: only those whose name, user or PID hold every word and that meet every condition such as cpu>10 or mem>100M (RET / Down: back to the list, Esc or C-g: empty the field, again: back to the list)"), EM},
    {N_("System Monitor tab"), "Ctrl+W", N_("Close the System Monitor tab"), NP},
    {N_("System Monitor tab"), "Ctrl+W c", N_("Close the System Monitor tab"), VI},
    {N_("Diagnostics tab"), NC_("key", "+ menu of the tab bars"), N_("Open the Diagnostics tab: what this computer is missing and how to install it"), 0},
    {N_("Diagnostics tab"), "Ctrl+W", N_("Close the Diagnostics tab"), NP},
    {N_("Diagnostics tab"), "Ctrl+W c", N_("Close the Diagnostics tab"), VI},
    {N_("Git tab"), "v", N_("Next view: Changes (uncommitted files and their patch), History (the commits), Statistics, Repository (identity, remotes, branches, configuration), Stashes & Tags"), 0},
    {N_("Git tab"), "Enter", N_("Changes: preview the selected file, fold or unfold the selected group (also a click)"), 0},
    {N_("Git tab"), "Left / Right", N_("Changes, Stashes & Tags: fold / unfold the group (Left on an entry: its group)"), NP},
    {N_("Git tab"), "h / l", N_("Changes, Stashes & Tags: fold / unfold the group (h on an entry: its group)"), VI},
    {N_("Git tab"), "C-b / C-f", N_("Changes, Stashes & Tags: fold / unfold the group (C-b on an entry: its group)"), EM},
    {N_("Git tab"), "Ctrl+F", N_("Filter the commits: only those whose message, author or hash hold every word, in the whole history (Enter / Down: back to the list, Esc: empty the field, again: back to the list)"), NP},
    {N_("Git tab"), "/", N_("Filter the commits: only those whose message, author or hash hold every word, in the whole history (Enter / Down: back to the list, Esc: empty the field, again: back to the list)"), VI},
    {N_("Git tab"), "C-s", N_("Filter the commits: only those whose message, author or hash hold every word, in the whole history (RET / Down: back to the list, Esc or C-g: empty the field, again: back to the list)"), EM},
    {N_("Git tab"), "a", N_("History: the commits of every branch and tag, or of the current branch only (the All Branches button)"), 0},
    {N_("Git tab"), "Ctrl+C", N_("Copy the hash of the selected commit (right click: hash, short hash or message)"), NP},
    {N_("Git tab"), "y", N_("Copy the hash of the selected commit (right click: hash, short hash or message)"), VI},
    {N_("Git tab"), "M-w", N_("Copy the hash of the selected commit (right click: hash, short hash or message)"), EM},
    {N_("Git tab"), "Ctrl+C", N_("Stashes & Tags: copy the name of the selected stash or tag (right click: name, hash or message)"), NP},
    {N_("Git tab"), "y", N_("Stashes & Tags: copy the name of the selected stash or tag (right click: name, hash or message)"), VI},
    {N_("Git tab"), "M-w", N_("Stashes & Tags: copy the name of the selected stash or tag (right click: name, hash or message)"), EM},
    {N_("References, Search, Problems and Build tabs"), "Ctrl+W", N_("Close the tab (also its cross or a middle click)"), NP},
    {N_("References, Search, Problems and Build tabs"), "Ctrl+W c", N_("Close the tab (also its cross or a middle click)"), VI},
    {N_("References, Search, Problems and Build tabs"), "Ctrl+Alt+-", N_("References: back to where the lookup started (also from the reference opened)"), NP},
    {N_("References, Search, Problems and Build tabs"), "Ctrl+O", N_("References: back to where the lookup started (also from the reference opened)"), VI},
    {N_("References, Search, Problems and Build tabs"), "M-,", N_("References: back to where the lookup started (also from the reference opened)"), EM},
    {N_("References, Search, Problems and Build tabs"), NC_("key", "Drag / right-click tab"), N_("Move the tab to another pane, as an editor tab"), 0},
    {N_("Disk Usage tab"), "Shift+S", N_("Disk usage of the selected folder (from the explorer; also its context menu)"), 0},
    {N_("Disk Usage tab"), "Enter / Right", N_("Open the selected folder; on a file, show it in the explorer"), 0},
    {N_("Disk Usage tab"), "l", N_("Open the selected folder"), VI},
    {N_("Disk Usage tab"), "C-f", N_("Open the selected folder"), EM},
    {N_("Disk Usage tab"), "Backspace / Alt+Up / Left", N_("Back to the parent folder"), 0},
    {N_("Disk Usage tab"), "h", N_("Back to the parent folder"), VI},
    {N_("Disk Usage tab"), "C-b", N_("Back to the parent folder"), EM},
    {N_("Disk Usage tab"), "F5", N_("Read the folder again"), 0},
    {N_("Disk Usage tab"), "v", N_("Next view under the list: treemap, wheel, histograms"), 0},
    {N_("Disk Usage tab"), "Escape", N_("Stop the reading (what was read stays)"), 0},
    {N_("Disk Usage tab"), "Ctrl+W", N_("Close the Disk Usage tab"), NP},
    {N_("Disk Usage tab"), "Ctrl+W c", N_("Close the Disk Usage tab"), VI},
    {N_("Deep Search tab"), "Shift+F", N_("Search the selected folder by name, extensions, type, size, date and content (from the explorer; also its context menu)"), 0},
    {N_("Deep Search tab"), "Enter", N_("In a field: search; on a result: show it in the explorer"), 0},
    {N_("Deep Search tab"), "Ctrl+Enter", N_("Open the selected file in an editor tab, at its first match"), 0},
    {N_("Deep Search tab"), "Space", N_("Preview the selected result, at its first match (Escape in the preview: back to the results)"), 0},
    {N_("Deep Search tab"), NC_("key", "Ctrl+Alt+Arrow / right-click tab"), N_("Move the Deep Search tab to another pane, as an editor tab (beside the preview, the result moved to is previewed)"), 0},
    {N_("Deep Search tab"), "F5", N_("Search again"), 0},
    {N_("Deep Search tab"), "Escape", N_("Stop the search (what was found stays)"), 0},
    {N_("Deep Search tab"), "Alt+M / Alt+W / Alt+R / Alt+L", N_("Content: match case / whole word / regular expression / letters in order"), 0},
    {N_("Deep Search tab"), "Down", N_("From a field: to the results"), 0},
    {N_("Deep Search tab"), "Ctrl+Space", N_("In the folder: suggest folders (also as you type; Tab completes, Enter takes one)"), 0},
    {N_("Deep Search tab"), "Left / Right, Home / End", N_("In a chart: previous / next bar, first / last bar"), 0},
    {N_("Deep Search tab"), "Enter / Space", N_("On a bar of a chart: list only its files (again: every result)"), 0},
    {N_("Deep Search tab"), "Ctrl+W", N_("Close the Deep Search tab"), NP},
    {N_("Deep Search tab"), "Ctrl+W c", N_("Close the Deep Search tab"), VI},
    {N_("Browser tab"), "Ctrl+Shift+B", N_("Open an empty browser tab (also Help links, web bookmarks, right click on an HTML file)"), 0},
    {N_("Browser tab"), "Ctrl+L", N_("Go to the address bar: an address, a file path or words to search, then Enter"), 0},
    {N_("Browser tab"), "Esc", N_("Address bar: back to the page; page: stop loading"), 0},
    {N_("Browser tab"), "F5", N_("Reload the page"), 0},
    {N_("Browser tab"), NC_("key", "Mouse back / forward"), N_("Back / forward in the pages visited (also the arrows of the tab)"), 0},
    {N_("Browser tab"), "Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+PgDn / Ctrl+PgUp", N_("Next / previous tab of its pane"), 0},
    {N_("Browser tab"), "F6 / Shift+F6", N_("Focus the next / previous panel"), 0},
    {N_("Browser tab"), "Ctrl+W", N_("Close the tab (also its cross, or a middle click on the tab)"), 0},
    {N_("Browser tab"), NC_("key", "Ctrl+Alt+Arrow / right-click tab"), N_("Move the browser tab to another pane, as an editor tab"), 0},
    {N_("Applications tab"), "Up / Down", N_("Previous / next application"), 0},
    {N_("Applications tab"), "PgUp / PgDn", N_("Five applications up / down"), 0},
    {N_("Applications tab"), NC_("key", "Enter / Click"), N_("Open the application in place of the Applications tab"), 0},
    {N_("Applications tab"), "Esc", N_("Close the Applications tab"), 0},
    {N_("Applications tab"), "Ctrl+W", N_("Close the Applications tab (also a middle click on the tab)"), NP},
    {N_("Applications tab"), "Ctrl+W c", N_("Close the Applications tab (also a middle click on the tab)"), VI},
    {N_("Diff tab"), "Alt+Down / Alt+Up", N_("Next / previous change hunk (also ]c / [c in Vim, M-n / M-p in Emacs)"), 0},
    {N_("Diff tab"), NC_("key", "Enter / Double-click"), N_("Open the compared file in an editor tab at the current line"), 0},
    {N_("Diff tab"), "F5", N_("Refresh the comparison from disk / HEAD"), 0},
    {N_("Diff tab"), "Ctrl+W", N_("Close the Diff tab (also a middle click on the tab)"), NP},
    {N_("Diff tab"), "Ctrl+W c", N_("Close the Diff tab (also a middle click on the tab)"), VI},
    {N_("Diff tab"), NC_("key", "Ctrl+Alt+Arrow / right-click tab"), N_("Move the Diff tab to another pane, as an editor tab"), 0},
    {N_("Machines view"), "Enter", N_("Connect selected machine and browse it in Explorer (this computer: back to its files)"), 0},
    {N_("Machines view"), "c", N_("Connect selected machine without leaving this view, or disconnect it when it is connected (this computer: back to its files)"), 0},
    {N_("Machines view"), "t", N_("Open SSH terminal to selected machine (this computer: a local terminal; a disk: a terminal in it)"), 0},
    {N_("Machines view"), "Enter / t, c", N_("On a container: open a terminal in it / start it, or stop it when it runs"), 0},
    {N_("Machines view"), "Enter / c / t", N_("On a WSL distribution (Windows): browse its files / open a terminal in it"), 0},
    {N_("Machines view"), "Enter / c", N_("On a disk: browse it (mounted first if needed) / mount, unmount or eject it (a remote machine's disk: browse it)"), 0},
    {N_("Machines view"), "a / Shift+A", N_("Add remote machine (inside the selected folder) / new folder"), 0},
    {N_("Machines view"), "F2 / r", N_("Edit selected machine (also its folder) or folder (name and icon)"), 0},
    {N_("Machines view"), "d / Delete", N_("Delete selected machine, or folder (its machines move to the top level)"), 0},
    {N_("Machines view"), "F5", N_("Re-scan ~/.ssh/config"), 0},
    {N_("Explorer panel"), "Ctrl+U", N_("Switch between files and notes view in Explorer"), 0},
    {N_("Explorer panel"), "a / Shift+A", N_("New file / new folder in the selected directory"), 0},
    {N_("Explorer panel"), "F2 / r", N_("Rename selected file or folder"), 0},
    {N_("Explorer panel"), "e", N_("Open the selected text file in the editor (as Enter does)"), 0},
    {N_("Explorer panel"), "d / Delete", N_("Move selected file or folder to the trash (asks first)"), 0},
    {N_("Explorer panel"), "Shift+H / Shift+L", N_("Back / forward in the folders shown in this workspace (also the mouse's back / forward buttons and the top bar arrows; right-click an arrow: the list)"), 0},
    {N_("Explorer panel"), NC_("key", "Right-click"), N_("Context menu: open, side panel, default app, terminal here, disk usage, rename, trash, copy name or path, bookmark"), 0},
    {N_("Explorer panel"), "u", N_("Undo the last move to the trash or rename (also Undo in the explorer bar)"), 0},
    {N_("Explorer panel"), "i / I", N_("Toggle hidden files"), 0},
    {N_("Explorer panel"), "o", N_("Cycle sort: name, date, size, extension"), 0},
    {N_("Explorer panel"), "Shift+S", N_("Disk usage of the selected folder, in the Disk Usage tab"), 0},
    {N_("Explorer panel"), "Shift+F", N_("Deep search in the selected folder, in the Deep Search tab"), 0},
    {N_("Explorer panel"), "+ / - / 0", N_("Zoom the previewed image, PDF or slides in / out / to the fitted size"), 0},
    {N_("Explorer panel"), "?", N_("Show / hide this help panel"), 0},
    /* Preview panel */
    {N_("Preview panel"), "Down / Up", N_("Scroll a line down / up"), 0},
    {N_("Preview panel"), "j / k", N_("Scroll a line down / up"), VI},
    {N_("Preview panel"), "C-n / C-p", N_("Scroll a line down / up"), EM},
    {N_("Preview panel"), "Ctrl+F / Ctrl+B", N_("Scroll a page down / up"), VI},
    {N_("Preview panel"), "C-v / M-v", N_("Scroll a page down / up"), EM},
    {N_("Preview panel"), "Home / End", N_("Jump to the start / end of the file"), 0},
    {N_("Preview panel"), "gg / G", N_("Jump to the start / end of the file"), VI},
    {N_("Preview panel"), "Ctrl+Home / Ctrl+End", N_("Jump to the start / end of the file"), NP},
    {N_("Preview panel"), "M-< / M->", N_("Jump to the start / end of the file"), EM},
    {N_("Preview panel"), "/", N_("Search in the file (n / N: next / previous match)"), VI},
    {N_("Preview panel"), "Ctrl+F", N_("Search in the file (Enter / F3: next match, Shift+Enter / Shift+F3: previous, Esc: close)"), NP},
    {N_("Preview panel"), "F3 / Shift+F3", N_("Next / previous match of the last search"), NP},
    {N_("Preview panel"), "C-s / C-r", N_("Search forward / backward as you type (again: next / previous match, RET: stop, C-g: back to the start)"), EM},
    {N_("Preview panel"), "t", N_("Open the previewed file in the dedicated editor panel"), VI},
    {N_("Preview panel"), "C-o", N_("Show the previewed file in the dedicated editor panel (focus stays in the viewer)"), EM},
    {N_("Preview panel"), "e", N_("Open the current file in the editor (text files only)"), 0},
    {N_("Preview panel"), "m", N_("Toggle rendered preview / raw view (documents, tables, data tree / text, diffs, logs, archives, certificates, images, PDF, video, audio)"), 0},
    {N_("Preview panel"), "o / Ctrl+K Ctrl+O", N_("Show / hide the tagbar: headings of a Markdown, HTML, Org, Word, OpenDocument or EPUB preview, the bookmarks of a PDF, or the symbols of source code, beside the text (Enter goes, s sorts symbols, c collapses / expands all, Esc back, q hides; in it, the moving and search keys of the mode)"), 0},
    {N_("Preview panel"), "+ / - / 0", N_("Zoom image, PDF or slides in / out / to the fitted size (also Ctrl+wheel)"), 0},
    {N_("Preview panel"), NC_("key", "Right-click"), N_("On an image: Copy Image to the clipboard"), 0},
    {N_("Preview panel"), "PgDn / PgUp", N_("PDF: next / previous page; mailbox: next / previous message"), 0},
    {N_("Preview panel"), NC_("key", "Ctrl+Click"), N_("PDF of a LaTeX document: open the source at the line of that place (SyncTeX)"), 0},
    {N_("Preview panel"), "Space / p", N_("Play / pause active video or audio preview"), 0},
    {N_("Preview panel"), "Left / Right", N_("Video: seek 5 seconds back / forward (also in the explorer)"), 0},
    {N_("Org-mode"), "Tab / Shift+Tab", N_("In Org view: Fold/unfold heading or cycle global visibility (Overview -> Contents -> Show All)"), 0},
    {N_("Preview panel"), "Tab / Shift+Tab", N_("JSON, XML, TOML, YAML: fold or open the block at the cursor / cycle the level (outer members, two levels, all open)"), 0},
    {N_("Preview panel"), NC_("key", "Click a ▾ / ▸ twisty"), N_("Data tree (JSON, XML...): fold or open the block"), 0},
    {N_("Preview panel"), "y", N_("JSON, XML, TOML, YAML: copy the path of the value at the cursor (shown in the preview bar)"), 0},
    {N_("Org-mode"), "t / Space / Return", N_("In Org view: Cycle TODO keyword (#+TODO, default TODO -> DONE) / toggle checkbox"), 0},
    {N_("Org-mode"), NC_("key", "Click heading/todo/box"), N_("Interactive click: toggle fold, cycle TODO, toggle checkbox, open link"), 0},
    {N_("Editor tabs"), "Ctrl+S", N_("Save the file open in the current editor tab"), 0},
    {N_("Editor tabs"), "Ctrl+W", N_("Close the current editor tab (asks when unsaved; a bullet marks changed tabs)"), 0},
    {N_("Editor tabs"), "Ctrl+Alt+Arrows / HJKL", N_("Move active editor tab right / left / down / up (Right from the main panel: dedicated panel; Left from it: dock back; the other way round when the panels are swapped)"), 0},
    {N_("Editor tabs"), NC_("key", "Drag tab header"), N_("Drop on a tab bar to move the tab there, near an editor edge to split that side"), 0},
    {N_("Editor tabs"), NC_("key", "Shift+drag tab header"), N_("Drop on another editor to swap places with the tab shown there"), 0},
    {N_("Editor tabs"), NC_("key", "Right-click tab header"), N_("Context menu to change mode, show line numbers, wrap lines, move, split, or close the editor tab"), 0},
    {N_("Editor tabs"), NC_("key", "Middle-click tab header"), N_("Close the tab (editor, References and Search tabs; asks first when there are unsaved changes)"), 0},
    {N_("Editor tabs"), "Ctrl+R / F2", N_("Rename current text buffer (F2 only in Emacs / Org-mode tabs)"), 0},
    {N_("Editor tabs"), "Ctrl+Alt+N", N_("Show or hide line numbers in the current editor tab (:set number / M-x display-line-numbers-mode)"), 0},
    {N_("Org editor"), "Tab / Shift+Tab", N_("Cycle heading / drawer / block folding, next table cell, <s block; global visibility"), 0},
    {N_("Org editor"), "Alt+Enter / Alt+Shift+Enter", N_("New heading or list item (TODO heading / checkbox item)"), 0},
    {N_("Org editor"), "Ctrl+Enter / Ctrl+Shift+Enter", N_("New heading (TODO heading) after the current subtree"), 0},
    {N_("Org editor"), "Alt+Left / Right (+Shift)", N_("Promote / demote heading (subtree), indent list item, move (insert / delete) table column"), 0},
    {N_("Org editor"), "Alt+Up / Down (+Shift)", N_("Move subtree, list item or table row (delete / insert table row)"), 0},
    {N_("Org editor"), "Shift+Left / Right", N_("Cycle TODO keyword or list bullet; previous / next day on a timestamp"), 0},
    {N_("Org editor"), "Shift+Up / Down", N_("Raise / lower priority; change the timestamp field under the cursor"), 0},
    {N_("Org editor"), "Enter / Ctrl+Space", N_("Next table row / toggle the checkbox"), 0},
    {N_("Org editor"), "C-c C-c / C-c C-t / C-c ,", N_("Context action (checkbox, table, tags) / cycle TODO / set priority"), 0},
    {N_("Org editor"), "C-c C-s / C-c C-d", N_("Schedule / deadline (today, +3, +2w, mon, 2026-11-02 10:00, none)"), 0},
    {N_("Org editor"), "C-c . / C-c ! / C-c q", N_("Insert active / inactive timestamp, set tags"), 0},
    {N_("Org editor"), "C-u C-c . / C-u C-c !", N_("Insert a timestamp with the time of day"), 0},
    {N_("Org editor"), "C-c C-x C-i / C-c C-x C-o", N_("Clock in / clock out (LOGBOOK drawer)"), 0},
    {N_("Org editor"), "C-c C-l / C-c C-o", N_("Insert link / open link (URL, file:, *Heading, #id)"), 0},
    {N_("Org editor"), "C-c C-n / p / f / b / u", N_("Next / previous visible heading, same level, parent"), 0},
    {N_("Org editor"), "C-c - / C-c * / C-c #", N_("List item or table rule / toggle heading / update cookies"), 0},
    {N_("Org editor"), "C-c / t / C-c / /", N_("Sparse tree of open TODO entries / of a regexp"), 0},
    /* Editor Modes */
    {N_("Editor: Mode switching"), NC_("key", "Right-click on the tab"), N_("Switch the tab between Notepad, Emacs, and Vim modes (the menu shows the current one)"), 0},
    {N_("Editor: Mode switching"), "M-x org-mode / text-mode", N_("In Emacs minibuffer: turn Org-mode editing on / off (also for .org files)"), 0},
    /* Editor: Notepad Mode */
    {N_("Editor: Notepad mode"), "Ctrl+X / Ctrl+C / Ctrl+V", N_("Cut, copy, and paste text to system clipboard"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+F / Ctrl+H", N_("Find / Replace, case ignored (Enter / F3: next, Shift+Enter / Shift+F3: previous, Esc: close)"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+L / E / R / J", N_("Align paragraph: Left, Center, Right, Justify"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+Z / Ctrl+Y", N_("Undo / Redo text changes (also Ctrl+Shift+Z)"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+S / Ctrl+Shift+S", N_("Save file / Save As with chosen file name prompt"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+G", N_("Go to line number in document"), 0},
    {N_("Editor: Notepad mode"), "F12 / Ctrl+F12 / Ctrl+Click", N_("Go to the definition / declaration of the identifier (code files)"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+Alt+- / Ctrl+Shift+-", N_("Back / forward through the definition jumps"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+K Ctrl+I / Ctrl+Shift+Space", N_("Help card of the identifier / signature of the call (code files)"), 0},
    {N_("Editor: Notepad mode"), "Shift+F12 / Ctrl+Shift+O / Ctrl+Alt+O", N_("References (References tab) / symbols of the file / symbols of the project"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+Shift+M / F8 / Shift+F8", N_("Problems panel (LSP diagnostics) / next / previous diagnostic in the file (F8: the Build tab's messages while it is open)"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+Space / Shift+F2 / Ctrl+K Ctrl+R", N_("Autocompletion popover (LSP or buffer words) / rename symbol across files (LSP)"), 0},
    {N_("Editor: Notepad mode"), "Ctrl+K Ctrl+O", N_("Show / hide the tagbar: symbols or Markdown headings beside the text (Enter goes, s sorts, c collapses / expands all, Esc back, q hides; in it, the moving and search keys of the mode)"), 0},
    /* Editor: Emacs Mode (Visual Clone) */
    {N_("Editor: Emacs mode"), "C-x C-s / C-x C-w", N_("Save buffer / Write file (Save As with chosen file name in minibuffer)"), 0},
    {N_("Editor: Emacs mode"), "C-x 2 / C-x 3", N_("Split view: the file in a second window below / to the right (own point, shared text and undo)"), 0},
    {N_("Editor: Emacs mode"), "C-x 0 / C-x 1", N_("Delete this window / delete the other windows of the file (the other windows keep the changes)"), 0},
    {N_("Editor: Emacs mode"), "C-w / M-w / C-y", N_("Kill region (cut), kill ring save (copy), yank (paste)"), 0},
    {N_("Editor: Emacs mode"), "C-d / M-d / C-k", N_("Delete char, kill word forward, kill line"), 0},
    {N_("Editor: Emacs mode"), "C-SPC / C-@", N_("Set mark (select text region; twice or C-g cancels)"), 0},
    {N_("Editor: Emacs mode"), "C-s / C-r", N_("Incremental search: again for next / previous (wraps), DEL undo step, C-w add word, RET stop, C-g back"), 0},
    {N_("Editor: Emacs mode"), "M-%", N_("Query replace words or phrases in minibuffer"), 0},
    {N_("Editor: Emacs mode"), "M-q", N_("Fill paragraph (format and wrap text to 72 columns)"), 0},
    {N_("Editor: Emacs mode"), "M-u / M-l / M-c", N_("Uppercase, lowercase, capitalize word"), 0},
    {N_("Editor: Emacs mode"), "C-/ / C-Shift-_", N_("Undo / Redo text changes (also C-_ / M-_)"), 0},
    {N_("Editor: Emacs mode"), "C-f / C-b / C-n / C-p", N_("Move cursor forward / backward / next line / previous line"), 0},
    {N_("Editor: Emacs mode"), "M-f / M-b / C-a / C-e", N_("Move forward / backward word, line start / line end"), 0},
    {N_("Editor: Emacs mode"), "M-g g / M-g M-g", N_("Goto line prompt in minibuffer"), 0},
    {N_("Editor: Emacs mode"), "M-. / C-u M-. / M-, / C-M-,", N_("Go to the definition / declaration (code files), back, forward"), 0},
    {N_("Editor: Emacs mode"), "C-h .", N_("Help card of the identifier at point (signatures go to the echo area)"), 0},
    {N_("Editor: Emacs mode"), "M-? / M-g i / M-g s", N_("References (References tab) / symbols of the buffer (imenu) / symbols of the project"), 0},
    {N_("Editor: Emacs mode"), "M-g n / M-g p / C-M-i / M-/", N_("Next / previous diagnostic in the buffer / autocompletion at point"), 0},
    {N_("Editor: Emacs mode"), "M-x imenu-list", N_("Show / hide the tagbar: symbols or Markdown headings beside the text (RET goes, s sorts, c collapses / expands all, Esc back, q hides; in it, the moving and search keys of the mode)"), 0},
    {N_("Editor: Emacs mode"), "M-x", N_("Execute command (e.g. flymake-show-buffer-diagnostics, lsp-rename, revert-buffer, auto-revert-mode)"), 0},
    {N_("Editor: Emacs mode"), "Tab / C-g (minibuffer)", N_("Complete the command, tag or *Heading link (lists candidates) / quit the prompt"), 0},
    /* Editor: Vim Mode */
    {N_("Editor: Vim mode"), "[\"x][count] op motion", N_("Operators d c y > < = g~ gu gU g? gq with any motion or text object (dd, cc, yy... on lines)"), 0},
    {N_("Editor: Vim mode"), "h j k l w b e ge W B E", N_("Character, line and word motions (counts everywhere)"), 0},
    {N_("Editor: Vim mode"), "0 ^ $ g_ | f t F T ; ,", N_("Line positions and character finds (; , repeat)"), 0},
    {N_("Editor: Vim mode"), "gg G % { } ( ) H M L", N_("Document, bracket, paragraph, sentence and screen motions"), 0},
    {N_("Editor: Vim mode"), "iw aw ip ap i( a\" it ...", N_("Text objects: words, sentences, paragraphs, brackets ( [ { <, quotes, tags"), 0},
    {N_("Editor: Vim mode"), "i a I A o O s S C R", N_("Insert / Replace mode; Esc back to Normal ([count] repeats the text)"), 0},
    {N_("Editor: Vim mode"), "Insert: Ctrl+W / U / T / D", N_("Delete word / to line start, indent / unindent the line"), 0},
    {N_("Editor: Vim mode"), "Insert: Ctrl+R x / N / P / O", N_("Insert register x, complete a word, run one Normal command"), 0},
    {N_("Editor: Vim mode"), "x X D Y J gJ r ~ Ctrl+A/X", N_("Delete chars, delete / yank to line end, join, replace, case, add to number"), 0},
    {N_("Editor: Vim mode"), "p P gp gP / \"ap \"+p", N_("Put after / before; named registers a-z (A-Z append), 0-9, -, +, _"), 0},
    {N_("Editor: Vim mode"), "v V Ctrl+V gv o", N_("Visual char / line / block, reselect, other end; block I / A / c / $"), 0},
    {N_("Editor: Vim mode"), "u / Ctrl+R / .", N_("Undo / redo a whole command / repeat the last change"), 0},
    {N_("Editor: Vim mode"), "qx ... q / @x / @@", N_("Record a macro into register x / run it / run it again"), 0},
    {N_("Editor: Vim mode"), "ma / `a / 'a / Ctrl+O", N_("Set mark / jump to it (line) / back in the jump list (Tab: forward)"), 0},
    {N_("Editor: Vim mode"), "/ ? n N * # gd", N_("Search (Vim regex, incremental, highlighted; :noh hides), word under cursor"), 0},
    {N_("Editor: Vim mode"), "gd Ctrl+] / gD / :tag[!] x", N_("Code files: go to the definition / declaration; Ctrl+O / Tab go on across files"), 0},
    {N_("Editor: Vim mode"), "K", N_("Code files: help card of the identifier under the cursor"), 0},
    {N_("Editor: Vim mode"), "gr / :References / :Symbols / :WSymbols", N_("Code files: references (References tab) / symbols of the file / symbols of the project"), 0},
    {N_("Editor: Vim mode"), ":Problems / ]d / [d / :Rename", N_("Code files: Problems panel / next / previous diagnostic / rename symbol across files (LSP)"), 0},
    {N_("Editor: Vim mode"), "Insert: Ctrl+Space", N_("Autocompletion popover (LSP or buffer words)"), 0},
    {N_("Editor: Vim mode"), ":TagbarToggle / :Tagbar", N_("Show / hide the tagbar: symbols or Markdown headings beside the text (Enter goes, s sorts, c collapses / expands all, Esc back, q hides; in it, the moving and search keys of the mode)"), 0},
    {N_("Editor: Vim mode"), ":[range]s/pat/rep/gi", N_("Substitute (& \\1 \\u \\U \\r); :g/pat/cmd, :v, :&& , & repeat"), 0},
    {N_("Editor: Vim mode"), ":d :y :pu :r! :m :t :j :> :normal", N_("Line commands on ranges (%, 'a, '<,'>, /pat/, .+1, $)"), 0},
    {N_("Editor: Vim mode"), ":sort :retab :center :reg", N_("Sort lines (n i u !), retab, align, list registers / :marks"), 0},
    {N_("Editor: Vim mode"), ":set sw=2 sts=2 et ic hls nu ar", N_("Options (shiftwidth, softtabstop, expandtab, ignorecase, hlsearch, number, wrap, autoread, colorcolumn...)"), 0},
    {N_("Editor: Vim mode"), ": Tab / Shift+Tab", N_("Complete a command or :set option, list the matches and cycle through them"), 0},
    {N_("Editor: Vim mode"), ":w[!] / :e[!] / :checktime / :q!", N_("Save (:w! overwrites external changes, :saveas), reload (:e!), check disk, close discarding changes"), 0},
    {N_("Editor: Vim mode"), "Ctrl+W s / Ctrl+W v / :sp / :vs", N_("Split view: the file in a second view below / to the right (own cursor, shared text and undo)"), 0},
    {N_("Editor: Vim mode"), "Ctrl+W o / :only / Ctrl+W q / :clo", N_("Close the other views of the file / close this view (the other views keep the changes)"), 0},
    {N_("Editor: Vim mode"), "zz zt zb Ctrl+E/Y/D/U/F/B", N_("Scroll the view (cursor line centered, top, bottom; lines, half pages, pages)"), 0},
    /* LaTeX (editor_latex.c) */
    {N_("Editor: LaTeX"), "Enter", N_("At the end of a line with an open \\begin{name}: add the line inside and \\end{name}"), 0},
    {N_("Editor: LaTeX"), NC_("key", "Type in a name"), N_("Typing in the name of a \\begin or an \\end renames the other one too"), 0},
    {N_("Editor: LaTeX"), "Ctrl+Shift+\\ / C-M-a, C-M-e / %", N_("Go to the matching \\begin / \\end (Notepad / Emacs / Vim)"), 0},
    {N_("Editor: LaTeX"), "Ctrl+K Ctrl+E / C-c C-e / :Env", N_("Environment around the selected lines, or a new one (asks its name)"), 0},
    {N_("Editor: LaTeX"), "Ctrl+B / Ctrl+I", N_("Notepad: \\textbf{} / \\textit{} around the selection, or with the cursor inside"), 0},
    {N_("Editor: LaTeX"), "C-c C-f C-b / C-i / C-e / C-t", N_("Emacs: \\textbf / \\textit / \\emph / \\texttt around the region"), 0},
    {N_("Editor: LaTeX"), "Ctrl+K Ctrl+W / C-c C-m", N_("Any command around the selection (asks its name)"), 0},
    {N_("Editor: LaTeX"), "C-c ]", N_("Emacs: close the environment left open around the cursor"), 0},
    /* Global */
    {N_("Global"), "Alt+1 .. Alt+8", N_("Switch between virtual workspaces (or click/scroll top-bar minimap)"), 0},
    {N_("Global"), "Alt++ / Alt+-", N_("Add a virtual workspace with the default layout (+) or close the current workspace (-)"), 0},
    {N_("Global"), "Alt+R", N_("Recent folders and files: Enter opens a folder or shows a file (Ctrl+Enter edits it)"), 0},
    {N_("Global"), "Alt+L", N_("Workspace layouts: preview while moving (Alt+L: next), Enter applies, Escape restores; Edit Layouts… saves (not in Emacs editor tabs)"), 0},
    {N_("Global"), "Alt+Shift+Left / Alt+Shift+Right", N_("Previous / next layout in this workspace's layout history (also the arrows around the top-bar layouts button)"), 0},
    {N_("Global"), NC_("key", "Double-click a split border"), N_("Split it in half (the explorer's border: its default width, 260 pixels); pointing at it shows a bar to collapse either side or put it at 1/3, 1/2, 2/3 (the explorer: default width, fit to the names, 1/5, 1/2)"), 0},
    {N_("Global"), NC_("key", "Click a collapsed split border"), N_("Expand the collapsed panel back (its border shows as a coloured strip)"), 0},
    {N_("Global"), "Ctrl+,", N_("Preferences: appearance, editor (mode, line numbers, wrap of new tabs, column line), window size, Auto layout panels, language servers"), 0},
    {N_("Global"), "Alt+T", N_("Preferences on Appearance: pick a theme (applied as you select it), set your fonts, save, edit or delete your themes"), 0},
    {N_("Global"), "Ctrl+= / Ctrl+- / Ctrl+0", N_("Larger / smaller fonts (interface and code), or back to the theme's sizes"), 0},
    {N_("Global"), "Ctrl+P", N_("Go to a file of the project: type part of its name (Enter shows it, Ctrl+Enter edits it; not in Vim editor tabs)"), VI},
    {N_("Global"), "C-x C-f", N_("Go to a file of the project: type part of its name (Enter shows it, Ctrl+Enter edits it)"), EM},
    {N_("Global"), "Ctrl+P", N_("Go to a file of the project: type part of its name (Enter shows it, Ctrl+Enter edits it)"), NP},
    {N_("Global"), "Ctrl+Alt+O", N_("Symbols of the project of the current folder, every language (Alt+K / Alt+Shift+K: next / previous kind; Enter opens the file at the symbol; editor tabs: the project of their file)"), 0},
    {N_("Global"), "Alt+K / Alt+Shift+K", N_("(In the symbols of the file or of the project) Next / previous kind that has matches (functions, macros, types...)"), 0},
    {N_("Global"), "M-g s", N_("Symbols of the project of the current folder (Enter opens the file at the symbol)"), EM},
    {N_("Global"), "Ctrl+Shift+F", N_("Search the text in every file of the project, on the Notes page in the notes (results in the Search tab, Enter opens; not in Emacs editor tabs)"), 0},
    {N_("Global"), "Alt+M / Alt+W / Alt+R / Alt+L", N_("In Search in Files: match case / whole word / regular expression / letters in order"), 0},
    {N_("Global"), "Alt+N / Alt+X / Alt+I", N_("In Search in Files: files to include / to exclude (*.c, src/) / include the files git ignores"), 0},
    {N_("Global"), "F7", N_("Run the project's task chosen last (at first make, cargo build, npm run build...): output and messages in the Build tab (Enter opens one)"), 0},
    {N_("Global"), "Shift+F7", N_("Choose the task to run among the project's (Makefile, Cargo.toml, package.json, go.mod...)"), 0},
    {N_("Global"), "Ctrl+F7", N_("Stop the running task"), 0},
    {N_("Global"), "F8 / Shift+F8", N_("While the Build tab is open: next / previous error or warning of the task, opened in an editor tab"), 0},
    {N_("Global"), "Ctrl+T", N_("Open a terminal in a new tab (exit the shell to close it; drag tabs to move or split; not in Vim Insert)"), 0},
    {N_("Global"), "Ctrl+Shift+T", N_("Open a terminal in a new pane below the focused one"), 0},
    {N_("Global"), "Ctrl+PgDn / Ctrl+PgUp", N_("In a terminal: next / previous tab of its pane"), 0},
    {N_("Global"), "Ctrl+Shift+C / Ctrl+Shift+V", N_("In a terminal: copy the selected text / paste the clipboard"), 0},
    {N_("Global"), "Alt+Left / Alt+Right", N_("Cycle the keyboard focus between panels (a tagbar on screen comes after its text)"), 0},
    {N_("Global"), "Ctrl+W w / Ctrl+W W", N_("Focus the next / previous panel (also h / l; from Vim editor tabs too)"), VI},
    {N_("Global"), "Ctrl+W c", N_("Close the focused editor tab, or the current one on screen (from Vim editor tabs too; asks when unsaved)"), VI},
    {N_("Global"), "C-x o", N_("Focus the next panel (from Emacs and Org-mode editor tabs too)"), EM},
    {N_("Global"), "F6 / Shift+F6", N_("Focus the next / previous panel (from Notepad editor tabs too)"), NP},
    {N_("Global"), "gt / gT, Ctrl+PgDn / Ctrl+PgUp", N_("Next / previous tab of the focused notebook (from the explorer: the preview panel, the focus stays)"), VI},
    {N_("Global"), "C-x <right> / C-x <left>", N_("Next / previous tab of the focused notebook (from the explorer: the preview panel, the focus stays)"), EM},
    {N_("Global"), "Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+PgDn / Ctrl+PgUp", N_("Next / previous tab of the focused notebook (from the explorer: the preview panel, the focus stays)"), NP},
    {N_("Global"), NC_("key", "Git / References / Search"), N_("The navigation keys of the global mode move in the list (Enter opens)"), 0},
    {N_("Global"), NC_("key", "Top bar mode"), N_("Global mode: only the Notepad, Vim or Emacs navigation keys in the explorer, viewer and their search bars (arrows, Home / End, PgUp / PgDn in all); mode of new editor tabs"), 0},
    {N_("Global"), "f", N_("Toggle fullscreen"), 0},
    {N_("Global"), "Ctrl+Shift+B", N_("Open an empty browser tab in the preview panel (needs WebKitGTK 6.0, or WebView2 on Windows; not in Emacs editor tabs)"), 0},
    {N_("Global"), "Ctrl+Shift+L", N_("Stack the terminal pane under the viewer, or put it back beside it (the explorer panel stays fixed)"), 0},
    {N_("Global"), "Ctrl+Shift+R", N_("Toggle Remote (SSH) machines view in sidebar"), 0},
    {N_("Global"), "F1", N_("User guide in a Help tab (also a click on the top bar's Help button; right click: this cheat sheet)"), 0},
    {N_("Help tab"), "Ctrl+F", N_("Search every page of the guide (Enter: the sections that hold every word, Esc: back to the page)"), NP},
    {N_("Help tab"), "/", N_("Search every page of the guide (Enter: the sections that hold every word, Esc: back to the page)"), VI},
    {N_("Help tab"), "C-s / C-r", N_("Search every page of the guide (RET: the sections that hold every word, Esc: back to the page)"), EM},
    {N_("Help tab"), "F3 / Shift+F3", N_("Next / previous searched word in the page"), NP},
    {N_("Help tab"), "n / N", N_("Next / previous searched word in the page"), VI},
    {N_("Help panel"), "Ctrl+F", N_("Filter the shortcuts: only those that hold every word (Enter / Esc: back to the list)"), NP},
    {N_("Help panel"), "/", N_("Filter the shortcuts: only those that hold every word (Enter / Esc: back to the list)"), VI},
    {N_("Help panel"), "C-s", N_("Filter the shortcuts: only those that hold every word (RET / Esc: back to the list)"), EM},
    {N_("Help panel"), "Up Up Down Down Left Right Left Right A B Enter", N_("Unlock Game Mode in this workspace"), 0},
    {N_("Global"), "Ctrl+Q", N_("Quit"), 0},
};

/* Fill the cheat sheet with the keys of the global mode that match the
 * filter field (every word in the section, key or description) */
static void fill_help_panel(AppData* app, GtkWidget* panel) {
    GtkWidget* scroll = g_object_get_data(G_OBJECT(panel), "help-scroll");
    GtkWidget* title_label = g_object_get_data(G_OBJECT(panel), "help-title");
    GtkWidget* filter = g_object_get_data(G_OBJECT(panel), "help-filter");
    const gchar* query = filter != NULL ? gtk_editable_get_text(GTK_EDITABLE(filter)) : "";
    NavStyle style = app_nav_style(app);
    /* TRANSLATORS: %s is Notepad, Vim or Emacs */
    gchar* mode = g_strdup_printf(_("(%s mode)"), nav_style_name(style));
    gchar* title = g_markup_printf_escaped("<b>%s</b> %s", _("Keyboard Shortcuts"), mode);
    g_free(mode);
    gtk_label_set_markup(GTK_LABEL(title_label), title);
    g_free(title);

    GtkGrid* grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 14);
    gtk_widget_set_margin_top(GTK_WIDGET(grid), 12);
    gtk_widget_set_margin_bottom(GTK_WIDGET(grid), 14);
    gtk_widget_set_margin_start(GTK_WIDGET(grid), 14);
    gtk_widget_set_margin_end(GTK_WIDGET(grid), 14);

    const gchar* current_section = NULL;
    gint row = 0;
    for (guint i = 0; i < G_N_ELEMENTS(KEY_HELP); i++) {
        if (KEY_HELP[i].modes != 0 && (KEY_HELP[i].modes & (1u << style)) == 0) {
            continue;
        }
        const gchar* key = g_dpgettext2(GETTEXT_PACKAGE, "key", KEY_HELP[i].key);
        gchar* text = g_strjoin(" ", _(KEY_HELP[i].section), key, _(KEY_HELP[i].description), NULL);
        gboolean shown = user_guide_matches_all(text, query);
        g_free(text);
        if (!shown) {
            continue;
        }
        if (current_section == NULL ||
            g_strcmp0(current_section, KEY_HELP[i].section) != 0) {
            current_section = KEY_HELP[i].section;
            /* Colours and fonts from the theme (.help-section, .help-key) */
            GtkWidget* section_label = gtk_label_new(_(current_section));
            gtk_widget_add_css_class(section_label, "help-section");
            gtk_widget_set_halign(section_label, GTK_ALIGN_START);
            if (row > 0) {
                gtk_widget_set_margin_top(section_label, 10);
            }
            gtk_grid_attach(grid, section_label, 0, row++, 2, 1);
        }
        GtkWidget* key_label = gtk_label_new(key);
        gtk_widget_add_css_class(key_label, "help-key");
        /* A long key sequence wraps instead of squeezing the descriptions */
        gtk_label_set_wrap(GTK_LABEL(key_label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(key_label), 18);
        gtk_label_set_xalign(GTK_LABEL(key_label), 0.0f);
        gtk_widget_set_halign(key_label, GTK_ALIGN_START);
        gtk_widget_set_valign(key_label, GTK_ALIGN_START);

        GtkWidget* desc_label = gtk_label_new(_(KEY_HELP[i].description));
        gtk_label_set_wrap(GTK_LABEL(desc_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(desc_label), 0.0f);
        gtk_widget_set_hexpand(desc_label, TRUE);
        gtk_widget_set_halign(desc_label, GTK_ALIGN_FILL);
        gtk_widget_set_valign(desc_label, GTK_ALIGN_START);

        gtk_grid_attach(grid, key_label, 0, row, 1, 1);
        gtk_grid_attach(grid, desc_label, 1, row, 1, 1);
        row++;
    }
    if (row == 0) {
        GtkWidget* none = gtk_label_new(_("No shortcut holds every word of the filter"));
        gtk_label_set_wrap(GTK_LABEL(none), TRUE);
        gtk_widget_add_css_class(none, "dim-label");
        gtk_widget_set_halign(none, GTK_ALIGN_START);
        gtk_grid_attach(grid, none, 0, 0, 2, 1);
    }

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(grid));
}

void toggle_help_panel(AppData* app) {
    if (app == NULL || app->help_window == NULL) {
        return;
    }
    if (app->main_paned != NULL && app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        gint cur_pos = gtk_paned_get_position(GTK_PANED(app->main_paned));
        gint total_w = get_workspace_total_width(app);
        if (total_w > 60 && cur_pos > 0 && cur_pos < total_w) {
            app->workspaces[app->current_ws].main_frac = CLAMP((gdouble)cur_pos / total_w, 0.05, 0.95);
        }
    }
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    gboolean vis = gtk_widget_get_visible(app->help_window);
    if (!vis) {
        GtkWidget* filter = g_object_get_data(G_OBJECT(app->help_window), "help-filter");
        if (filter != NULL) {
            g_signal_handlers_block_matched(filter, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, app);
            gtk_editable_set_text(GTK_EDITABLE(filter), ""); /* every key again */
            g_signal_handlers_unblock_matched(filter, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, app);
        }
        fill_help_panel(app, app->help_window); /* the global mode may have changed */
    }
    app_layout_hold(app);
    gtk_widget_set_visible(app->help_window, !vis);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    if (vis) {
        if (app->left_vbox == NULL || gtk_widget_get_visible(app->left_vbox)) {
            gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        } else if (app->viewer_box != NULL && gtk_widget_get_visible(app->viewer_box)) {
            gtk_widget_grab_focus(app->text_view);
        }
    } else {
        gtk_widget_grab_focus(app->help_window);
    }
}

static void on_help_close_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_help_panel((AppData*)data);
}

/* The filter field holds the keyboard focus */
static gboolean filter_focused(GtkWidget* panel) {
    GtkWidget* filter = g_object_get_data(G_OBJECT(panel), "help-filter");
    GtkRoot* root = gtk_widget_get_root(panel);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    return filter != NULL && focus != NULL && (focus == filter || gtk_widget_is_ancestor(focus, filter));
}

static void on_filter_changed(GtkSearchEntry* entry, gpointer data) {
    (void)entry;
    AppData* app = data;
    fill_help_panel(app, app->help_window);
}

/* In the help panel, ? or Escape closes it and gives keyboard focus back to
 * the explorer panel; the search key of the global mode goes to the filter
 * field, where Escape or Enter goes back to the panel */
gboolean on_help_key_press(GtkEventControllerKey* controller, guint keyval,
                           guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    AppData* app = (AppData*)data;
    if (filter_focused(app->help_window)) {
        if (keyval == GDK_KEY_Escape || keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
            gtk_widget_grab_focus(app->help_window);
            return TRUE;
        }
        return FALSE; /* typed into the field */
    }
    NavAction action = nav_keys_lookup(app_nav_style(app), NAV_PREFIX_NONE, keyval, state, NULL);
    if (action == NAV_SEARCH || action == NAV_SEARCH_BACKWARD) {
        GtkWidget* filter = g_object_get_data(G_OBJECT(app->help_window), "help-filter");
        gtk_widget_grab_focus(filter);
        gtk_editable_select_region(GTK_EDITABLE(filter), 0, -1);
        return TRUE;
    }
    if (keyval == GDK_KEY_question || keyval == GDK_KEY_Escape) {
        toggle_help_panel(app);
        return TRUE;
    }
    if (games_check_help_key(app, keyval, state)) {
        return TRUE;
    }
    return FALSE;
}

/* Build internal help panel toggled by '?' or the '? Help' top bar button */
void create_help_panel(GtkWindow* parent, AppData* app) {
    (void)parent;
    GtkWidget* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(panel, "help-panel");
    gtk_widget_set_focusable(panel, TRUE);
    gtk_widget_set_vexpand(panel, TRUE);
    gtk_widget_set_hexpand(panel, FALSE);
    gtk_widget_set_size_request(panel, 400, -1);
    gtk_widget_set_visible(panel, FALSE);

    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(header_box, "help-header");
    GtkWidget* title_label = gtk_label_new(NULL);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(title_label, TRUE);

    GtkWidget* close_btn = icon_button_new("window-close", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(close_btn), FALSE);
    gtk_widget_set_focusable(close_btn, FALSE);
    gtk_widget_add_css_class(close_btn, "help-close-btn");
    gtk_widget_set_tooltip_text(close_btn, _("Close Help Panel (?)"));
    g_signal_connect(close_btn, "clicked",
                     G_CALLBACK(on_help_close_clicked), app);

    GtkWidget* title_icon = icon_label_new("input-keyboard", NULL);
    gtk_widget_set_margin_end(title_icon, 6);
    gtk_box_append(GTK_BOX(header_box), title_icon);
    gtk_box_append(GTK_BOX(header_box), title_label);
    gtk_box_append(GTK_BOX(header_box), close_btn);
    gtk_box_append(GTK_BOX(panel), header_box);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    /* Filter: only the keys whose section, key or description hold every word */
    GtkWidget* filter = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(filter), _("Filter shortcuts"));
    gtk_widget_set_tooltip_text(filter, _("Show only the shortcuts that hold every word (the search key of your mode: "
                                          "Ctrl+F, / or C-s)"));
    gtk_widget_set_margin_top(filter, 8);
    gtk_widget_set_margin_start(filter, 14);
    gtk_widget_set_margin_end(filter, 14);
    g_signal_connect(filter, "search-changed", G_CALLBACK(on_filter_changed), app);
    gtk_box_append(GTK_BOX(panel), filter);

    g_object_set_data(G_OBJECT(panel), "help-scroll", scroll);
    g_object_set_data(G_OBJECT(panel), "help-title", title_label);
    g_object_set_data(G_OBJECT(panel), "help-filter", filter);
    fill_help_panel(app, panel);
    gtk_box_append(GTK_BOX(panel), scroll);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_help_key_press), app);
    gtk_widget_add_controller(panel, key_ctrl);

    app->help_window = panel;
}
