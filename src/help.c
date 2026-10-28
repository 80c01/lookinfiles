/* Floating key-binding cheat sheet ('?' / top bar Help button). Keep KEY_HELP in sync with README.md. */
#include "help.h"
#include "workspace.h"
#include "ui.h"
#include "file_list.h"

/* Cheat sheet entry: section header, key column, description */
typedef struct {
    const gchar* section;
    const gchar* key;
    const gchar* description;
} KeyHelpEntry;

static const KeyHelpEntry KEY_HELP[] = {
    /* Explorer panel */
    {"Explorer panel", "j / k", "Move down / up"},
    {"Explorer panel", "gg / G", "First / last entry"},
    {"Explorer panel", "zz", "Center selected entry in the panel"},
    {"Explorer panel", "zt / zb", "Scroll selected entry to top / bottom of the panel"},
    {"Explorer panel", "Ctrl+F / Ctrl+B", "Page down / up"},
    {"Explorer panel", "Space / l / Right", "Expand / collapse directory (or step into first child)"},
    {"Explorer panel", "h / Left", "Collapse directory or jump to parent"},
    {"Explorer panel", "/", "Search by substring (n / N: next / previous match)"},
    {"Explorer panel", "Enter / Double-click", "Expand/collapse directory, open file in editor, or open bookmark (Ctrl+Enter to enter directory)"},
    {"Explorer panel", "Backspace", "Go to the parent directory (or return to files view from bookmarks)"},
    {"Explorer panel", "Ctrl+D", "Bookmark selected entry or current directory (edit / remove if already bookmarked)"},
    {"Explorer panel", "Ctrl+A", "Switch between files and bookmarks views in Explorer"},
    {"Bookmarks view", "Enter / l", "Open bookmark (directory, file or URL) / expand folder"},
    {"Bookmarks view", "a / Shift+A", "Add bookmark / new folder (in selected folder)"},
    {"Bookmarks view", "F2 / r / e", "Edit selected: name, location and folder"},
    {"Bookmarks view", "d / Delete", "Delete selected bookmark or folder"},
    {"Bookmarks view", "Shift+K / Shift+J", "Move selected item up / down"},
    {"Bookmarks view", "Right-click", "Bookmark context menu"},
    {"Notes view", "a / Shift+A", "Create new note (Plain Text, Rich, Orgmode) / new notebook"},
    {"Notes view", "F2 / r / e", "Edit note (title and format) or notebook (name and icon)"},
    {"Notes view", "i", "Change notebook icon"},
    {"Notes view", "m", "Move note to another notebook"},
    {"Notes view", "d / Delete", "Delete note (to Trash) or notebook"},
    {"Notes view", "Shift+R", "Restore note from Trash to original notebook"},
    {"Notes view", "Shift+K / Shift+J", "Move selected note or notebook up / down"},
    {"Notes view", "Right-click", "Context menu: 1-click format conversion (Text, Rich, Org), rename, move"},
    {"Explorer panel", "Ctrl+U", "Switch between files and notes view in Explorer"},
    {"Explorer panel", "i / I", "Toggle hidden files"},
    {"Explorer panel", "o", "Cycle sort: name, date, size, extension"},
    {"Explorer panel", "?", "Show / hide this help panel"},
    /* Viewer panel */
    {"Right panel (viewer)", "j / k", "Scroll a line down / up"},
    {"Right panel (viewer)", "Ctrl+F / Ctrl+B", "Scroll a page down / up"},
    {"Right panel (viewer)", "gg / G", "Jump to the start / end of the file"},
    {"Right panel (viewer)", "/", "Search in the file (n / N: next / previous match)"},
    {"Right panel (viewer)", "e", "Open the current file in the editor (text files only)"},
    {"Right panel (viewer)", "m", "Toggle rendered preview / raw text view (Markdown, HTML, Org, PDF, images, video, audio)"},
    {"Right panel (viewer)", "Space / p", "Play / pause active video or audio preview"},
    {"Org-mode", "Tab / Shift+Tab", "In Org view: Fold/unfold heading or cycle global visibility (Overview -> Contents -> Show All)"},
    {"Org-mode", "t / Space / Return", "In Org view: Cycle TODO state / toggle checkbox [ ] <-> [X]"},
    {"Org-mode", "Click heading/todo/box", "Interactive click: toggle fold, cycle TODO, toggle checkbox, open link"},
    {"Editor tabs", "Ctrl+S", "Save the file open in the current editor tab"},
    {"Editor tabs", "Ctrl+W", "Close the current editor tab (asks when unsaved; a bullet marks changed tabs)"},
    {"Editor tabs", "Ctrl+E", "Move active editor tab to dedicated panel right of preview, or dock back"},
    {"Editor tabs", "Ctrl+Alt+Arrows / HJKL", "Move active editor tab right / left / down / up (split panels)"},
    {"Editor tabs", "Tab / Shift+Tab", "In Org editor: toggle heading fold or cycle global visibility"},
    {"Editor tabs", "Shift+Right / Left", "In Org editor: cycle TODO keyword (TODO -> NEXT -> DONE -> none)"},
    {"Editor tabs", "Alt+Enter", "In Org editor: insert new heading at current level (M-RET)"},
    {"Editor tabs", "Ctrl+Enter / Space", "In Org editor: toggle checkbox [ ] <-> [X] and update progress statistics"},
    {"Editor tabs", "Right-click tab header", "Context menu to change mode, move, split, or close the editor tab"},
    {"Editor tabs", "Ctrl+R / F2", "Rename current text buffer"},
    /* Editor Modes */
    {"Editor: Mode switching", "Status bar / Tab menu", "Switch between Notepad, Emacs, and Vim editor modes"},
    {"Editor: Mode switching", ":set mode=vim/emacs/notepad", "In Vim command line: switch active editor mode"},
    {"Editor: Mode switching", "M-x vim-mode / notepad-mode", "In Emacs minibuffer: switch active editor mode"},
    /* Editor: Notepad Mode */
    {"Editor: Notepad mode", "Ctrl+X / Ctrl+C / Ctrl+V", "Cut, copy, and paste text to system clipboard"},
    {"Editor: Notepad mode", "Ctrl+F / Ctrl+H", "Find / Replace words or phrases (F3 / Shift+F3: repeat find)"},
    {"Editor: Notepad mode", "Ctrl+L / E / R / J", "Align paragraph: Left, Center, Right, Justify"},
    {"Editor: Notepad mode", "Ctrl+Z / Ctrl+Y", "Undo / Redo text changes (also Ctrl+Shift+Z)"},
    {"Editor: Notepad mode", "Ctrl+S / Ctrl+Shift+S", "Save file / Save As with chosen file name prompt"},
    {"Editor: Notepad mode", "Ctrl+G", "Go to line number in document"},
    /* Editor: Emacs Mode (Visual Clone) */
    {"Editor: Emacs mode", "C-x C-s / C-x C-w", "Save buffer / Write file (Save As with chosen file name in minibuffer)"},
    {"Editor: Emacs mode", "C-w / M-w / C-y", "Kill region (cut), kill ring save (copy), yank (paste)"},
    {"Editor: Emacs mode", "C-d / M-d / C-k", "Delete char, kill word forward, kill line"},
    {"Editor: Emacs mode", "C-SPC / C-@", "Set mark (select text region; C-g cancels)"},
    {"Editor: Emacs mode", "C-s / C-r", "Incremental search forward / backward (I-search in minibuffer)"},
    {"Editor: Emacs mode", "M-%", "Query replace words or phrases in minibuffer"},
    {"Editor: Emacs mode", "M-q", "Fill paragraph (format and wrap text to 72 columns)"},
    {"Editor: Emacs mode", "M-u / M-l / M-c", "Uppercase, lowercase, capitalize word"},
    {"Editor: Emacs mode", "C-/ / C-Shift-_", "Undo / Redo text changes (also C-_ / M-_)"},
    {"Editor: Emacs mode", "C-f / C-b / C-n / C-p", "Move cursor forward / backward / next line / previous line"},
    {"Editor: Emacs mode", "M-f / M-b / C-a / C-e", "Move forward / backward word, line start / line end"},
    {"Editor: Emacs mode", "M-g g / M-g M-g", "Goto line prompt in minibuffer"},
    {"Editor: Emacs mode", "M-x", "Execute command (e.g. set-justification-center, goto-line, save-buffer)"},
    /* Editor: Vim Mode (Visual Clone) */
    {"Editor: Vim mode", "i / a / o / O / s / S / C", "Enter Insert mode (insert, append, open line below/above, change)"},
    {"Editor: Vim mode", "Esc / Ctrl+[", "Return to Normal mode (switches to block cursor)"},
    {"Editor: Vim mode", "x / dd / dw / d$ / D / d0", "Delete char, line, word, or to line end/start"},
    {"Editor: Vim mode", "yy / Y / yw", "Yank (copy) line, word to clipboard"},
    {"Editor: Vim mode", "p / P", "Put (paste) clipboard text after / before cursor"},
    {"Editor: Vim mode", "v / V", "Visual / Visual Line mode (select text; d cut, y yank, c change, > indent)"},
    {"Editor: Vim mode", "h / j / k / l", "Move cursor left / down / up / right"},
    {"Editor: Vim mode", "w / b / e / 0 / ^ / $", "Next word, prev word, word end, line start, non-blank, line end"},
    {"Editor: Vim mode", "gg / G / [count]G", "Go to start / end / line [count] of document"},
    {"Editor: Vim mode", "/pattern / ?pattern", "Search forward / backward (n / N: repeat match; *: word under cursor)"},
    {"Editor: Vim mode", ":%s/old/new/g", "Substitute/replace words or phrases in entire buffer"},
    {"Editor: Vim mode", "== / >> / << / gq", "Auto-indent line, indent, unindent, format paragraph (80 cols)"},
    {"Editor: Vim mode", ":center / :right / :left", "Align text lines (optional width e.g. :center 60)"},
    {"Editor: Vim mode", "u / Ctrl+R", "Undo / Redo changes in Normal mode"},
    {"Editor: Vim mode", ":w / :w <file> / :saveas", "Save document / Save As with chosen file name"},
    {"Editor: Vim mode", "ZZ / :wq / :q / :q!", "Save and quit, quit, quit discarding changes"},
    /* Global */
    {"Global", "Alt+1 .. Alt+8", "Switch between virtual workspaces (or click/scroll top-bar minimap)"},
    {"Global", "Alt++ / Alt+-", "Add a new virtual workspace (+) or close the current workspace (-)"},
    {"Global", "Ctrl+T", "Open a terminal pane right of the running ones (exit the shell to close it)"},
    {"Global", "Ctrl+Shift+T", "Open a new terminal pane below the running ones"},
    {"Global", "Alt+Left / Alt+Right", "Cycle the keyboard focus between panels"},
    {"Global", "f", "Toggle fullscreen (Ctrl+F keeps its paging role)"},
    {"Global", "Ctrl+Shift+L", "Stack the terminal pane under the viewer, or put it back beside it (the explorer panel stays fixed)"},
    {"Global", "Ctrl+Shift+R", "Toggle Remote (SSH) machines view in sidebar"},
    {"Global", "Ctrl+Q", "Quit"},
};

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
    workspace_save_current(app);
    gboolean vis = gtk_widget_get_visible(app->help_window);
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->help_window, !vis);
    update_path_label(app);
    workspace_apply_positions(app);
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
    gtk_label_set_markup(GTK_LABEL(title_label),
                         "<b>⌨  Keyboard Shortcuts</b>");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(title_label, TRUE);

    GtkWidget* close_btn = gtk_button_new_with_label("✕");
    gtk_button_set_has_frame(GTK_BUTTON(close_btn), FALSE);
    gtk_widget_set_focusable(close_btn, FALSE);
    gtk_widget_add_css_class(close_btn, "help-close-btn");
    gtk_widget_set_tooltip_text(close_btn, "Close help panel (?)");
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
        if (current_section == NULL ||
            g_strcmp0(current_section, KEY_HELP[i].section) != 0) {
            current_section = KEY_HELP[i].section;
            gchar* header =
                g_strdup_printf("<span foreground=\"#4fc1ff\" weight=\"bold\">%s</span>",
                                current_section);
            GtkWidget* section_label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(section_label), header);
            gtk_widget_set_halign(section_label, GTK_ALIGN_START);
            if (row > 0) {
                gtk_widget_set_margin_top(section_label, 10);
            }
            g_free(header);
            gtk_grid_attach(grid, section_label, 0, row++, 2, 1);
        }
        GtkWidget* key_label = gtk_label_new(NULL);
        gchar* key_markup =
            g_strdup_printf("<span foreground=\"#dcdcaa\"><tt><b>%s</b></tt></span>",
                            KEY_HELP[i].key);
        gtk_label_set_markup(GTK_LABEL(key_label), key_markup);
        g_free(key_markup);
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
    gtk_box_append(GTK_BOX(panel), scroll);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_help_key_press), app);
    gtk_widget_add_controller(panel, key_ctrl);

    app->help_window = panel;
}
