/* Floating key-binding cheat sheet ('?' / top bar Help button). Keep KEY_HELP in sync with README.md. */
#include "help.h"
#include "icons.h"
#include "panels.h"
#include "app_events.h"
#include "nav_keys.h"

/* Modes in which an entry is shown (global mode, nav_keys.h); 0 = every mode */
#define NP (1u << NAV_STYLE_NOTEPAD)
#define VI (1u << NAV_STYLE_VIM)
#define EM (1u << NAV_STYLE_EMACS)

/* Cheat sheet entry: section header, key column, description, modes */
typedef struct {
    const gchar* section;
    const gchar* key;
    const gchar* description;
    guint modes;
} KeyHelpEntry;

static const KeyHelpEntry KEY_HELP[] = {
    /* Explorer panel */
    {"Explorer panel", "Down / Up", "Move down / up", 0},
    {"Explorer panel", "j / k", "Move down / up", VI},
    {"Explorer panel", "C-n / C-p", "Move down / up", EM},
    {"Explorer panel", "Home / End", "First / last entry", 0},
    {"Explorer panel", "gg / G", "First / last entry", VI},
    {"Explorer panel", "Ctrl+Home / Ctrl+End", "First / last entry", NP},
    {"Explorer panel", "M-< / M->", "First / last entry", EM},
    {"Explorer panel", "Page Down / Page Up", "Page down / up", 0},
    {"Explorer panel", "Ctrl+F / Ctrl+B", "Page down / up", VI},
    {"Explorer panel", "C-v / M-v", "Page down / up", EM},
    {"Explorer panel", "zz", "Center selected entry in the panel", VI},
    {"Explorer panel", "zt / zb", "Scroll selected entry to top / bottom of the panel", VI},
    {"Explorer panel", "C-l", "Scroll selected entry to the center, top, bottom in turn", EM},
    {"Explorer panel", "Space", "Expand / collapse directory", 0},
    {"Explorer panel", "Right", "Expand directory (or step into first child)", 0},
    {"Explorer panel", "l", "Expand directory (or step into first child)", VI},
    {"Explorer panel", "Left", "Collapse directory or jump to parent", 0},
    {"Explorer panel", "h", "Collapse directory or jump to parent", VI},
    {"Explorer panel", "C-f / C-b", "Expand directory (or step into first child) / collapse it or jump to parent", EM},
    {"Explorer panel", "/", "Search by substring (n / N: next / previous match)", VI},
    {"Explorer panel", "Ctrl+F", "Search by substring (Enter / F3: next match, Shift+Enter / Shift+F3: previous, Esc: close)", NP},
    {"Explorer panel", "F3 / Shift+F3", "Next / previous match of the last search", NP},
    {"Explorer panel", "C-s / C-r", "Search forward / backward as you type (again: next / previous match, RET: stop, C-g: back to the start)", EM},
    {"Explorer panel", "t", "Open the selected file in the dedicated editor panel (focus in the editor)", VI},
    {"Explorer panel", "C-o", "Show the selected file in the dedicated editor panel (focus stays in the explorer)", EM},
    {"Explorer panel", "Enter / Double-click", "Expand/collapse directory, open file in editor, or open bookmark (Ctrl+Enter to enter directory)", 0},
    {"Explorer panel", "Backspace / Alt+Up", "Go to the parent directory (or return to files view from bookmarks)", 0},
    {"Explorer panel", "Ctrl+D", "Bookmark selected entry or current directory (edit / remove if already bookmarked)", 0},
    {"Explorer panel", "Ctrl+A", "Switch between files and bookmarks views in Explorer", 0},
    {"Bookmarks view", "Enter / Right", "Open bookmark (directory, file or URL) / expand folder", 0},
    {"Bookmarks view", "l", "Open bookmark / expand folder", VI},
    {"Bookmarks view", "C-f", "Open bookmark / expand folder", EM},
    {"Bookmarks view", "a / Shift+A", "Add bookmark / new folder (in selected folder)", 0},
    {"Bookmarks view", "F2 / r / e", "Edit selected: name, location and folder", 0},
    {"Bookmarks view", "d / Delete", "Delete selected bookmark or folder", 0},
    {"Bookmarks view", "Shift+K / Shift+J", "Move selected item up / down", 0},
    {"Bookmarks view", "o", "Cycle sort: A-Z / Z-A", 0},
    {"Bookmarks view", "Right-click", "Bookmark context menu", 0},
    {"Notes view", "a / Shift+A", "Create new note (Plain Text, Rich, Org-mode) / new notebook", 0},
    {"Notes view", "F2 / r / e", "Edit note (title and format) or notebook (name and icon)", 0},
    {"Notes view", "i", "Change notebook icon", 0},
    {"Notes view", "m", "Move note to another notebook", 0},
    {"Notes view", "d / Delete", "Delete note (to Trash) or notebook", 0},
    {"Notes view", "Shift+R", "Restore note from Trash to original notebook", 0},
    {"Notes view", "Shift+K / Shift+J", "Move selected note or notebook up / down", 0},
    {"Notes view", "o", "Cycle sort: manual / A-Z / Z-A", 0},
    {"Notes view", "Right-click", "Context menu: 1-click format conversion (Text, Rich, Org), rename, move", 0},
    {"Remote view", "Enter / c", "Connect selected machine (SSHFS) and browse it in Explorer", 0},
    {"Remote view", "t", "Open SSH terminal to selected machine", 0},
    {"Remote view", "a", "Add remote machine", 0},
    {"Remote view", "F2 / r / e", "Edit selected machine", 0},
    {"Remote view", "d / Delete", "Delete selected machine", 0},
    {"Remote view", "F5", "Re-scan ~/.ssh/config", 0},
    {"Explorer panel", "Ctrl+U", "Switch between files and notes view in Explorer", 0},
    {"Explorer panel", "a / Shift+A", "New file / new folder in the selected directory", 0},
    {"Explorer panel", "F2 / r / e", "Rename selected file or folder", 0},
    {"Explorer panel", "d / Delete", "Move selected file or folder to the trash (asks first)", 0},
    {"Explorer panel", "Shift+H / Shift+L", "Back / forward in the folders shown in this workspace (also the mouse's back / forward buttons)", 0},
    {"Explorer panel", "Right-click", "Context menu: open, side panel, default app, terminal here, rename, trash, copy path, bookmark", 0},
    {"Explorer panel", "u", "Undo the last move to the trash or rename (also Undo in the status bar)", 0},
    {"Explorer panel", "i / I", "Toggle hidden files", 0},
    {"Explorer panel", "o", "Cycle sort: name, date, size, extension", 0},
    {"Explorer panel", "+ / - / 0", "Zoom the previewed image or PDF in / out / to the fitted size", 0},
    {"Explorer panel", "?", "Show / hide this help panel", 0},
    /* Preview panel */
    {"Preview panel", "Down / Up", "Scroll a line down / up", 0},
    {"Preview panel", "j / k", "Scroll a line down / up", VI},
    {"Preview panel", "C-n / C-p", "Scroll a line down / up", EM},
    {"Preview panel", "Ctrl+F / Ctrl+B", "Scroll a page down / up", VI},
    {"Preview panel", "C-v / M-v", "Scroll a page down / up", EM},
    {"Preview panel", "Home / End", "Jump to the start / end of the file", 0},
    {"Preview panel", "gg / G", "Jump to the start / end of the file", VI},
    {"Preview panel", "Ctrl+Home / Ctrl+End", "Jump to the start / end of the file", NP},
    {"Preview panel", "M-< / M->", "Jump to the start / end of the file", EM},
    {"Preview panel", "/", "Search in the file (n / N: next / previous match)", VI},
    {"Preview panel", "Ctrl+F", "Search in the file (Enter / F3: next match, Shift+Enter / Shift+F3: previous, Esc: close)", NP},
    {"Preview panel", "F3 / Shift+F3", "Next / previous match of the last search", NP},
    {"Preview panel", "C-s / C-r", "Search forward / backward as you type (again: next / previous match, RET: stop, C-g: back to the start)", EM},
    {"Preview panel", "t", "Open the previewed file in the dedicated editor panel", VI},
    {"Preview panel", "C-o", "Show the previewed file in the dedicated editor panel (focus stays in the viewer)", EM},
    {"Preview panel", "e", "Open the current file in the editor (text files only)", 0},
    {"Preview panel", "m", "Toggle rendered preview / raw view (documents, tables, JSON, diffs, logs, archives, certificates, images, PDF, video, audio)", 0},
    {"Preview panel", "+ / - / 0", "Zoom image or PDF in / out / to the fitted size (also Ctrl+wheel)", 0},
    {"Preview panel", "PgDn / PgUp", "PDF: next / previous page; mailbox: next / previous message", 0},
    {"Preview panel", "Space / p", "Play / pause active video or audio preview", 0},
    {"Org-mode", "Tab / Shift+Tab", "In Org view: Fold/unfold heading or cycle global visibility (Overview -> Contents -> Show All)", 0},
    {"Org-mode", "t / Space / Return", "In Org view: Cycle TODO keyword (#+TODO, default TODO -> DONE) / toggle checkbox", 0},
    {"Org-mode", "Click heading/todo/box", "Interactive click: toggle fold, cycle TODO, toggle checkbox, open link", 0},
    {"Editor tabs", "Ctrl+S", "Save the file open in the current editor tab", 0},
    {"Editor tabs", "Ctrl+W", "Close the current editor tab (asks when unsaved; a bullet marks changed tabs)", 0},
    {"Editor tabs", "Ctrl+Alt+Arrows / HJKL", "Move active editor tab right / left / down / up (Right from the main panel: dedicated panel; Left from it: dock back)", 0},
    {"Editor tabs", "Drag tab header", "Drop on a tab bar to move the tab there, near an editor edge to split that side", 0},
    {"Editor tabs", "Right-click tab header", "Context menu to change mode, show line numbers, move, split, or close the editor tab", 0},
    {"Editor tabs", "Ctrl+R / F2", "Rename current text buffer (F2 only in Emacs / Org-mode tabs)", 0},
    {"Editor tabs", "Ctrl+Alt+N", "Show or hide line numbers in the current editor tab (:set number / M-x display-line-numbers-mode)", 0},
    {"Org editor", "Tab / Shift+Tab", "Cycle heading / drawer / block folding, next table cell, <s block; global visibility", 0},
    {"Org editor", "Alt+Enter / Alt+Shift+Enter", "New heading or list item (TODO heading / checkbox item)", 0},
    {"Org editor", "Ctrl+Enter / Ctrl+Shift+Enter", "New heading (TODO heading) after the current subtree", 0},
    {"Org editor", "Alt+Left / Right (+Shift)", "Promote / demote heading (subtree), indent list item, move (insert / delete) table column", 0},
    {"Org editor", "Alt+Up / Down (+Shift)", "Move subtree, list item or table row (delete / insert table row)", 0},
    {"Org editor", "Shift+Left / Right", "Cycle TODO keyword or list bullet; previous / next day on a timestamp", 0},
    {"Org editor", "Shift+Up / Down", "Raise / lower priority; change the timestamp field under the cursor", 0},
    {"Org editor", "Enter / Ctrl+Space", "Next table row / toggle the checkbox", 0},
    {"Org editor", "C-c C-c / C-c C-t / C-c ,", "Context action (checkbox, table, tags) / cycle TODO / set priority", 0},
    {"Org editor", "C-c C-s / C-c C-d", "Schedule / deadline (today, +3, +2w, mon, 2026-11-02 10:00, none)", 0},
    {"Org editor", "C-c . / C-c ! / C-c q", "Insert active / inactive timestamp, set tags", 0},
    {"Org editor", "C-u C-c . / C-u C-c !", "Insert a timestamp with the time of day", 0},
    {"Org editor", "C-c C-x C-i / C-c C-x C-o", "Clock in / clock out (LOGBOOK drawer)", 0},
    {"Org editor", "C-c C-l / C-c C-o", "Insert link / open link (URL, file:, *Heading, #id)", 0},
    {"Org editor", "C-c C-n / p / f / b / u", "Next / previous visible heading, same level, parent", 0},
    {"Org editor", "C-c - / C-c * / C-c #", "List item or table rule / toggle heading / update cookies", 0},
    {"Org editor", "C-c / t / C-c / /", "Sparse tree of open TODO entries / of a regexp", 0},
    /* Editor Modes */
    {"Editor: Mode switching", "Right-click on the tab", "Switch the tab between Notepad, Emacs, and Vim modes (the menu shows the current one)", 0},
    {"Editor: Mode switching", "M-x org-mode / text-mode", "In Emacs minibuffer: turn Org-mode editing on / off (also for .org files)", 0},
    /* Editor: Notepad Mode */
    {"Editor: Notepad mode", "Ctrl+X / Ctrl+C / Ctrl+V", "Cut, copy, and paste text to system clipboard", 0},
    {"Editor: Notepad mode", "Ctrl+F / Ctrl+H", "Find / Replace words or phrases (F3 / Shift+F3: repeat find)", 0},
    {"Editor: Notepad mode", "Ctrl+L / E / R / J", "Align paragraph: Left, Center, Right, Justify", 0},
    {"Editor: Notepad mode", "Ctrl+Z / Ctrl+Y", "Undo / Redo text changes (also Ctrl+Shift+Z)", 0},
    {"Editor: Notepad mode", "Ctrl+S / Ctrl+Shift+S", "Save file / Save As with chosen file name prompt", 0},
    {"Editor: Notepad mode", "Ctrl+G", "Go to line number in document", 0},
    {"Editor: Notepad mode", "F12 / Ctrl+F12 / Ctrl+Click", "Go to the definition / declaration of the identifier (code files)", 0},
    {"Editor: Notepad mode", "Ctrl+Alt+- / Ctrl+Shift+-", "Back / forward through the definition jumps", 0},
    {"Editor: Notepad mode", "Ctrl+K Ctrl+I / Ctrl+Shift+Space", "Help card of the identifier / signature of the call (code files)", 0},
    {"Editor: Notepad mode", "Shift+F12 / Ctrl+Shift+O", "References (References tab) / symbols of the file", 0},
    /* Editor: Emacs Mode (Visual Clone) */
    {"Editor: Emacs mode", "C-x C-s / C-x C-w", "Save buffer / Write file (Save As with chosen file name in minibuffer)", 0},
    {"Editor: Emacs mode", "C-w / M-w / C-y", "Kill region (cut), kill ring save (copy), yank (paste)", 0},
    {"Editor: Emacs mode", "C-d / M-d / C-k", "Delete char, kill word forward, kill line", 0},
    {"Editor: Emacs mode", "C-SPC / C-@", "Set mark (select text region; twice or C-g cancels)", 0},
    {"Editor: Emacs mode", "C-s / C-r", "Incremental search: again for next / previous (wraps), DEL undo step, C-w add word, RET stop, C-g back", 0},
    {"Editor: Emacs mode", "M-%", "Query replace words or phrases in minibuffer", 0},
    {"Editor: Emacs mode", "M-q", "Fill paragraph (format and wrap text to 72 columns)", 0},
    {"Editor: Emacs mode", "M-u / M-l / M-c", "Uppercase, lowercase, capitalize word", 0},
    {"Editor: Emacs mode", "C-/ / C-Shift-_", "Undo / Redo text changes (also C-_ / M-_)", 0},
    {"Editor: Emacs mode", "C-f / C-b / C-n / C-p", "Move cursor forward / backward / next line / previous line", 0},
    {"Editor: Emacs mode", "M-f / M-b / C-a / C-e", "Move forward / backward word, line start / line end", 0},
    {"Editor: Emacs mode", "M-g g / M-g M-g", "Goto line prompt in minibuffer", 0},
    {"Editor: Emacs mode", "M-. / C-u M-. / M-, / C-M-,", "Go to the definition / declaration (code files), back, forward", 0},
    {"Editor: Emacs mode", "C-h .", "Help card of the identifier at point (signatures go to the echo area)", 0},
    {"Editor: Emacs mode", "M-? / M-g i", "References (References tab) / symbols of the buffer (imenu)", 0},
    {"Editor: Emacs mode", "M-x", "Execute command (e.g. set-justification-center, goto-line, save-buffer)", 0},
    {"Editor: Emacs mode", "Tab / C-g (minibuffer)", "Complete the command, tag or *Heading link (lists candidates) / quit the prompt", 0},
    /* Editor: Vim Mode */
    {"Editor: Vim mode", "[\"x][count] op motion", "Operators d c y > < = g~ gu gU g? gq with any motion or text object (dd, cc, yy... on lines)", 0},
    {"Editor: Vim mode", "h j k l w b e ge W B E", "Character, line and word motions (counts everywhere)", 0},
    {"Editor: Vim mode", "0 ^ $ g_ | f t F T ; ,", "Line positions and character finds (; , repeat)", 0},
    {"Editor: Vim mode", "gg G % { } ( ) H M L", "Document, bracket, paragraph, sentence and screen motions", 0},
    {"Editor: Vim mode", "iw aw ip ap i( a\" it ...", "Text objects: words, sentences, paragraphs, brackets ( [ { <, quotes, tags", 0},
    {"Editor: Vim mode", "i a I A o O s S C R", "Insert / Replace mode; Esc back to Normal ([count] repeats the text)", 0},
    {"Editor: Vim mode", "Insert: Ctrl+W / U / T / D", "Delete word / to line start, indent / unindent the line", 0},
    {"Editor: Vim mode", "Insert: Ctrl+R x / N / P / O", "Insert register x, complete a word, run one Normal command", 0},
    {"Editor: Vim mode", "x X D Y J gJ r ~ Ctrl+A/X", "Delete chars, delete / yank to line end, join, replace, case, add to number", 0},
    {"Editor: Vim mode", "p P gp gP / \"ap \"+p", "Put after / before; named registers a-z (A-Z append), 0-9, -, +, _", 0},
    {"Editor: Vim mode", "v V Ctrl+V gv o", "Visual char / line / block, reselect, other end; block I / A / c / $", 0},
    {"Editor: Vim mode", "u / Ctrl+R / .", "Undo / redo a whole command / repeat the last change", 0},
    {"Editor: Vim mode", "qx ... q / @x / @@", "Record a macro into register x / run it / run it again", 0},
    {"Editor: Vim mode", "ma / `a / 'a / Ctrl+O", "Set mark / jump to it (line) / back in the jump list (Tab: forward)", 0},
    {"Editor: Vim mode", "/ ? n N * # gd", "Search (Vim regex, incremental, highlighted; :noh hides), word under cursor", 0},
    {"Editor: Vim mode", "gd Ctrl+] / gD / :tag[!] x", "Code files: go to the definition / declaration; Ctrl+O / Tab go on across files", 0},
    {"Editor: Vim mode", "K", "Code files: help card of the identifier under the cursor", 0},
    {"Editor: Vim mode", "gr / :References / :Symbols", "Code files: references (References tab) / symbols of the file", 0},
    {"Editor: Vim mode", ":[range]s/pat/rep/gi", "Substitute (& \\1 \\u \\U \\r); :g/pat/cmd, :v, :&& , & repeat", 0},
    {"Editor: Vim mode", ":d :y :m :t :j :> :normal", "Line commands on ranges (%, 'a, '<,'>, /pat/, .+1, $)", 0},
    {"Editor: Vim mode", ":sort :retab :center :reg", "Sort lines (n i u !), retab, align, list registers / :marks", 0},
    {"Editor: Vim mode", ":set sw=2 sts=2 et ic hls nu", "Options (shiftwidth, softtabstop, expandtab, ignorecase, hlsearch, number, wrap...)", 0},
    {"Editor: Vim mode", ": Tab / Shift+Tab", "Complete a command or :set option, list the matches and cycle through them", 0},
    {"Editor: Vim mode", ":w / :wq / ZZ / :q!", "Save (:w file, :saveas), save and close, close discarding changes", 0},
    {"Editor: Vim mode", "zz zt zb Ctrl+E/Y/D/U/F/B", "Scroll the view (cursor line centered, top, bottom; lines, half pages, pages)", 0},
    /* Global */
    {"Global", "Alt+1 .. Alt+8", "Switch between virtual workspaces (or click/scroll top-bar minimap)", 0},
    {"Global", "Alt++ / Alt+-", "Add a virtual workspace with the default layout (+) or close the current workspace (-)", 0},
    {"Global", "Alt+R", "Recent folders and files: Enter opens a folder or shows a file (Ctrl+Enter edits it)", 0},
    {"Global", "Alt+L", "Workspace layouts: apply one from the list, save the panels (\"Default\" for new workspaces)", 0},
    {"Global", "Alt+T", "Themes and fonts: pick a theme (applied as you select it), set your fonts, save, edit or delete your themes", 0},
    {"Global", "Ctrl+= / Ctrl+- / Ctrl+0", "Larger / smaller fonts (interface and code), or back to the theme's sizes", 0},
    {"Global", "Ctrl+P", "Go to a file of the project: type part of its name (Enter shows it, Ctrl+Enter edits it; not in Vim editor tabs)", VI},
    {"Global", "C-x C-f", "Go to a file of the project: type part of its name (Enter shows it, Ctrl+Enter edits it)", EM},
    {"Global", "Ctrl+P", "Go to a file of the project: type part of its name (Enter shows it, Ctrl+Enter edits it)", NP},
    {"Global", "Ctrl+Shift+F", "Search the text in every file of the project (results in the Search tab, Enter opens)", 0},
    {"Global", "Ctrl+T", "Open a terminal pane right of the running ones (exit the shell to close it)", 0},
    {"Global", "Ctrl+Shift+T", "Open a new terminal pane below the running ones", 0},
    {"Global", "Alt+Left / Alt+Right", "Cycle the keyboard focus between panels", 0},
    {"Global", "Ctrl+W w / Ctrl+W W", "Focus the next / previous panel (also h / l; from Vim editor tabs too)", VI},
    {"Global", "Ctrl+W c", "Close the focused editor tab, or the current one on screen (from Vim editor tabs too; asks when unsaved)", VI},
    {"Global", "C-x o", "Focus the next panel (from Emacs and Org-mode editor tabs too)", EM},
    {"Global", "F6 / Shift+F6", "Focus the next / previous panel (from Notepad editor tabs too)", NP},
    {"Global", "gt / gT, Ctrl+PgDn / Ctrl+PgUp", "Next / previous tab of the focused notebook (from the explorer: the preview panel, the focus stays)", VI},
    {"Global", "C-x <right> / C-x <left>", "Next / previous tab of the focused notebook (from the explorer: the preview panel, the focus stays)", EM},
    {"Global", "Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+PgDn / Ctrl+PgUp", "Next / previous tab of the focused notebook (from the explorer: the preview panel, the focus stays)", NP},
    {"Global", "Git Commits / References / Search", "The navigation keys of the global mode move in the list (Enter opens)", 0},
    {"Global", "Status bar mode", "Global mode: only the Notepad, Vim or Emacs navigation keys in the explorer, viewer and their search bars (arrows, Home / End, PgUp / PgDn in all); mode of new editor tabs", 0},
    {"Global", "f", "Toggle fullscreen", 0},
    {"Global", "Ctrl+Shift+L", "Stack the terminal pane under the viewer, or put it back beside it (the explorer panel stays fixed)", 0},
    {"Global", "Ctrl+Shift+R", "Toggle Remote (SSH) machines view in sidebar", 0},
    {"Global", "Ctrl+Q", "Quit", 0},
};

/* Fill the cheat sheet with the keys of the global mode */
static void fill_help_panel(AppData* app, GtkWidget* panel) {
    GtkWidget* scroll = g_object_get_data(G_OBJECT(panel), "help-scroll");
    GtkWidget* title_label = g_object_get_data(G_OBJECT(panel), "help-title");
    NavStyle style = app_nav_style(app);
    gchar* title = g_markup_printf_escaped("<b>⌨  Keyboard Shortcuts</b> (%s mode)", nav_style_name(style));
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
        if (current_section == NULL ||
            g_strcmp0(current_section, KEY_HELP[i].section) != 0) {
            current_section = KEY_HELP[i].section;
            /* Colours and fonts from the theme (.help-section, .help-key) */
            GtkWidget* section_label = gtk_label_new(current_section);
            gtk_widget_add_css_class(section_label, "help-section");
            gtk_widget_set_halign(section_label, GTK_ALIGN_START);
            if (row > 0) {
                gtk_widget_set_margin_top(section_label, 10);
            }
            gtk_grid_attach(grid, section_label, 0, row++, 2, 1);
        }
        GtkWidget* key_label = gtk_label_new(KEY_HELP[i].key);
        gtk_widget_add_css_class(key_label, "help-key");
        gtk_widget_set_halign(key_label, GTK_ALIGN_START);
        gtk_widget_set_valign(key_label, GTK_ALIGN_START);

        GtkWidget* desc_label = gtk_label_new(KEY_HELP[i].description);
        gtk_label_set_wrap(GTK_LABEL(desc_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(desc_label), 0.0f);
        gtk_widget_set_hexpand(desc_label, TRUE);
        gtk_widget_set_halign(desc_label, GTK_ALIGN_FILL);
        gtk_widget_set_valign(desc_label, GTK_ALIGN_START);

        gtk_grid_attach(grid, key_label, 0, row, 1, 1);
        gtk_grid_attach(grid, desc_label, 1, row, 1, 1);
        row++;
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
        fill_help_panel(app, app->help_window); /* the global mode may have changed */
    }
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->help_window, !vis);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    if (vis) {
        if (app->left_vbox == NULL || gtk_widget_get_visible(app->left_vbox)) {
            gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        } else if (app->viewer_box != NULL && gtk_widget_get_visible(app->viewer_box)) {
            gtk_widget_grab_focus(app->text_view);
        }
    }
}

static void on_help_close_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_help_panel((AppData*)data);
}

/* In the help panel, ? or Escape closes it and gives keyboard focus back to the explorer panel */
gboolean on_help_key_press(GtkEventControllerKey* controller, guint keyval,
                           guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    (void)state;
    AppData* app = (AppData*)data;
    if (keyval == GDK_KEY_question || keyval == GDK_KEY_Escape) {
        toggle_help_panel(app);
        return TRUE;
    }
    return FALSE;
}

/* Build internal help panel toggled by '?' or the '? Help' top bar button */
void create_help_panel(GtkWindow* parent, AppData* app) {
    (void)parent;
    GtkWidget* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(panel, "help-panel");
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
    gtk_widget_set_tooltip_text(close_btn, "Close Help Panel (?)");
    g_signal_connect(close_btn, "clicked",
                     G_CALLBACK(on_help_close_clicked), app);

    gtk_box_append(GTK_BOX(header_box), title_label);
    gtk_box_append(GTK_BOX(header_box), close_btn);
    gtk_box_append(GTK_BOX(panel), header_box);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    g_object_set_data(G_OBJECT(panel), "help-scroll", scroll);
    g_object_set_data(G_OBJECT(panel), "help-title", title_label);
    fill_help_panel(app, panel);
    gtk_box_append(GTK_BOX(panel), scroll);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_help_key_press), app);
    gtk_widget_add_controller(panel, key_ctrl);

    app->help_window = panel;
}
