/* The Preferences dialog (see preferences.h). */
#include "chrome/preferences.h"
#include "core/i18n.h"
#include "core/app_events.h"
#include "core/app_paths.h"
#include "widgets/color_column.h"
#include "widgets/diff_gutter.h"
#include "widgets/dialog.h"
#include "editor/editor.h"
#include "editor/editor_tagbar.h"
#include "preview/preview_tagbar.h"
#include "explorer/nav_keys.h"
#include "search/search.h"
#include "chrome/themes.h"

#include <glib/gstdio.h>

/* Responses of the page buttons other than the Appearance ones (themes.h) */
enum { RESPONSE_EDIT_LSP = 20, RESPONSE_CURRENT_SIZE = 21 };

/* Section shown by preferences_show(app, NULL) */
static gchar* last_section = NULL;

typedef struct {
    AppData* app;
    AppDialog* dialog;
    GtkStack* stack;
    ThemesPage* themes;
    GtkWidget* mode;          /* GtkDropDown: Notepad, Vim, Emacs */
    GtkWidget* line_numbers;  /* GtkCheckButton */
    GtkWidget* wrap;          /* GtkCheckButton */
    GtkWidget* tagbar;        /* GtkCheckButton */
    GtkWidget* preview_tagbar; /* GtkCheckButton */
    GtkWidget* ruler;         /* GtkCheckButton: vertical line at a column */
    GtkWidget* ruler_column;  /* GtkSpinButton */
    GtkWidget* width;         /* GtkSpinButton */
    GtkWidget* height;
    GtkWidget* auto_terminal; /* GtkDropDown: as it is, shown, hidden (PANEL_CHOICES) */
    GtkWidget* auto_preview;
} Preferences;

/* Drop-down order of the panel rules of the Auto layout */
static const SettingsPanel PANEL_CHOICES[] = {SETTINGS_PANEL_KEEP, SETTINGS_PANEL_SHOW, SETTINGS_PANEL_HIDE};

/* Drop-down order of the editor modes */
static const SettingsEditor MODES[] = {SETTINGS_EDITOR_NOTEPAD, SETTINGS_EDITOR_VIM, SETTINGS_EDITOR_EMACS};

EditorMode preferences_editor_mode(SettingsEditor editor) {
    switch (editor) {
    case SETTINGS_EDITOR_VIM: return EDITOR_MODE_VIM;
    case SETTINGS_EDITOR_EMACS: return EDITOR_MODE_EMACS;
    default: return EDITOR_MODE_NOTEPAD;
    }
}

static SettingsEditor settings_editor_of(EditorMode mode) {
    switch (mode) {
    case EDITOR_MODE_VIM: return SETTINGS_EDITOR_VIM;
    case EDITOR_MODE_EMACS:
    case EDITOR_MODE_ORGMODE: return SETTINGS_EDITOR_EMACS;
    default: return SETTINGS_EDITOR_NOTEPAD;
    }
}

void preferences_apply_startup(AppData* app, const Settings* settings) {
    app->settings = *settings;
}

/* ------------------------------------------------------------------------- */
/* Pages                                                                     */
/* ------------------------------------------------------------------------- */

static GtkWidget* page_box(void) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    return box;
}

static GtkWidget* hint_label(const gchar* text) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 56);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "dim-label");
    return label;
}

static void on_page_button_clicked(GtkButton* button, gpointer data) {
    gint response = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response"));
    app_dialog_response(APP_DIALOG(data), response);
}

static GtkWidget* response_button(Preferences* p, const gchar* label, gint response, const gchar* tooltip) {
    GtkWidget* button = gtk_button_new_with_mnemonic(label);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(button), "response", GINT_TO_POINTER(response));
    g_signal_connect(button, "clicked", G_CALLBACK(on_page_button_clicked), p->dialog);
    return button;
}

static GtkWidget* editor_page(Preferences* p) {
    GtkWidget* box = page_box();
    gtk_box_append(GTK_BOX(box), hint_label(_("For editor tabs opened from now on; the tabs already open keep "
                                            "theirs, except for the column line and the tagbar. Org files always open "
                                            "in Org-mode.")));

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* label = gtk_label_new_with_mnemonic(_("_Mode"));
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    static const char* const NAMES[] = {"Notepad", "Vim", "Emacs", NULL};
    p->mode = gtk_drop_down_new_from_strings(NAMES);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), p->mode);
    gtk_widget_set_tooltip_text(p->mode, _("Keys of new editor tabs, and of the explorer and preview "
                                         "(the global mode of the status bar; -e on the command line)"));
    SettingsEditor current = settings_editor_of(p->app->default_editor_mode);
    for (guint i = 0; i < G_N_ELEMENTS(MODES); i++) {
        if (MODES[i] == current) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(p->mode), i);
        }
    }
    gtk_box_append(GTK_BOX(row), label);
    gtk_box_append(GTK_BOX(row), p->mode);
    gtk_box_append(GTK_BOX(box), row);

    p->line_numbers = gtk_check_button_new_with_mnemonic(_("Show _line numbers"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(p->line_numbers), p->app->settings.line_numbers);
    gtk_widget_set_tooltip_text(p->line_numbers, _("A tab can still show or hide them from its right-click menu"));
    gtk_box_append(GTK_BOX(box), p->line_numbers);

    p->wrap = gtk_check_button_new_with_mnemonic(_("_Wrap long lines"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(p->wrap), p->app->settings.wrap);
    gtk_widget_set_tooltip_text(p->wrap, _("Long lines continue on the next line instead of scrolling sideways"));
    gtk_box_append(GTK_BOX(box), p->wrap);

    p->tagbar = gtk_check_button_new_with_mnemonic(_("Show the _tagbar"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(p->tagbar), p->app->settings.tagbar);
    gtk_widget_set_tooltip_text(p->tagbar, _("Symbols of code files and headings of Markdown files beside the text, "
                                           "in every tab, the tabs already open too (Notepad: Ctrl+K Ctrl+O, "
                                           "Emacs: M-x imenu-list, Vim: :TagbarToggle)"));
    gtk_box_append(GTK_BOX(box), p->tagbar);

    p->preview_tagbar = gtk_check_button_new_with_mnemonic(_("Show the tagbar in the _preview"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(p->preview_tagbar), p->app->settings.preview_tagbar);
    gtk_widget_set_tooltip_text(p->preview_tagbar, _("Headings of Markdown, HTML, Org, Word, OpenDocument and EPUB previews, PDF bookmarks, beside "
                                                   "the text (in the preview: o or Ctrl+K Ctrl+O)"));
    gtk_box_append(GTK_BOX(box), p->preview_tagbar);

    /* Vim's colorcolumn: the line at the left edge of that column */
    GtkWidget* ruler_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    p->ruler = gtk_check_button_new_with_mnemonic(_("Show a vertical line at _column"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(p->ruler), p->app->settings.color_column > 0);
    gtk_widget_set_tooltip_text(p->ruler, _("A line before this column in every editor tab, the tabs already "
                                          "open too, and in code previews (Vim: :set colorcolumn=81)"));
    gtk_widget_set_hexpand(p->ruler, TRUE);
    p->ruler_column = gtk_spin_button_new_with_range(1, SETTINGS_MAX_COLUMN, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->ruler_column),
                              p->app->settings.color_column > 0 ? p->app->settings.color_column : 81);
    gtk_widget_set_tooltip_text(p->ruler_column, _("81: the line follows the 80th character"));
    g_object_bind_property(p->ruler, "active", p->ruler_column, "sensitive", G_BINDING_SYNC_CREATE);
    gtk_box_append(GTK_BOX(ruler_row), p->ruler);
    gtk_box_append(GTK_BOX(ruler_row), p->ruler_column);
    gtk_box_append(GTK_BOX(box), ruler_row);
    return box;
}

/* The column line of the code preview and of every editor tab, in every
 * workspace (the preview shows line numbers for code only) */
static void apply_color_column(AppData* app) {
    if (app->text_view != NULL && diff_gutter_get_line_numbers(app->text_view)) {
        color_column_set(app->text_view, app->settings.color_column);
    }
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* tab = l->data;
        color_column_set(tab->view, app->settings.color_column);
    }
    for (gint i = 0; i < app->num_workspaces; i++) {
        if (i == app->current_ws) continue;
        for (GList* l = app->workspaces[i].editors; l != NULL; l = l->next) {
            EditorTab* tab = l->data;
            color_column_set(tab->view, app->settings.color_column);
        }
    }
}

static GtkWidget* size_spin(gint min, gint value) {
    GtkWidget* spin = gtk_spin_button_new_with_range(min, SETTINGS_MAX_SIZE, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    return spin;
}

static GtkWidget* window_page(Preferences* p, const Settings* s) {
    GtkWidget* box = page_box();
    gtk_box_append(GTK_BOX(box), hint_label(_("Size of the window at the next launch, in pixels "
                                            "(--size on the command line wins).")));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    p->width = size_spin(SETTINGS_MIN_WIDTH, s->width);
    p->height = size_spin(SETTINGS_MIN_HEIGHT, s->height);
    GtkWidget* wl = gtk_label_new_with_mnemonic(_("Wi_dth"));
    GtkWidget* hl = gtk_label_new_with_mnemonic(_("_Height"));
    gtk_label_set_mnemonic_widget(GTK_LABEL(wl), p->width);
    gtk_label_set_mnemonic_widget(GTK_LABEL(hl), p->height);
    gtk_label_set_xalign(GTK_LABEL(wl), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(hl), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), wl, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), p->width, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), hl, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), p->height, 1, 1, 1, 1);
    gtk_box_append(GTK_BOX(box), grid);
    gtk_box_append(GTK_BOX(box), response_button(p, _("Use the _current size"), RESPONSE_CURRENT_SIZE,
                                                 _("Fill in the size the window has now")));
    return box;
}

/* A labelled drop-down choosing what the Auto layout does with a panel */
static GtkWidget* panel_choice(GtkWidget* grid, gint row, const gchar* label_text, const gchar* tooltip,
                               SettingsPanel value) {
    const char* const names[] = {_("As it is"), _("Shown"), _("Hidden"), NULL};
    GtkWidget* drop = gtk_drop_down_new_from_strings(names);
    for (guint i = 0; i < G_N_ELEMENTS(PANEL_CHOICES); i++) {
        if (PANEL_CHOICES[i] == value) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(drop), i);
        }
    }
    gtk_widget_set_tooltip_text(drop, tooltip);
    GtkWidget* label = gtk_label_new_with_mnemonic(label_text);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), drop);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), drop, 1, row, 1, 1);
    return drop;
}

static SettingsPanel panel_selected(GtkWidget* drop) {
    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(drop));
    return (i < G_N_ELEMENTS(PANEL_CHOICES)) ? PANEL_CHOICES[i] : SETTINGS_PANEL_KEEP;
}

static GtkWidget* layouts_page(Preferences* p, const Settings* s) {
    GtkWidget* box = page_box();
    gtk_box_append(GTK_BOX(box), hint_label(_("The Auto layout (Alt+L) puts the open files in a grid in the "
                                            "editor panel; it can also show or hide the terminal and the "
                                            "preview.")));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    p->auto_terminal = panel_choice(grid, 0, _("_Terminal"), _("Terminal panel with the Auto layout"),
                                    s->auto_terminal);
    p->auto_preview = panel_choice(grid, 1, _("_Preview"), _("Preview panel with the Auto layout"),
                                   s->auto_preview);
    gtk_box_append(GTK_BOX(box), grid);
    return box;
}

static gchar* lsp_ini_path(void) {
    gchar* dir = app_config_dir();
    gchar* path = g_build_filename(dir, "lsp.ini", NULL);
    g_free(dir);
    return path;
}

static GtkWidget* code_page(Preferences* p) {
    GtkWidget* box = page_box();
    gchar* path = lsp_ini_path();
    gchar* text = g_strdup_printf(_("Code navigation starts the language server installed for the file's "
                                  "language (clangd, pylsp, gopls, rust-analyzer...). %s replaces a "
                                  "server's command or turns it off; it is read at the next launch."),
                                  path);
    g_free(path);
    gtk_box_append(GTK_BOX(box), hint_label(text));
    g_free(text);
    gtk_box_append(GTK_BOX(box), response_button(p, _("Edit _lsp.ini"), RESPONSE_EDIT_LSP,
                                                 _("Open lsp.ini in an editor tab (created with an example "
                                                 "if missing)")));
    return box;
}

/* ------------------------------------------------------------------------- */
/* Dialog                                                                    */
/* ------------------------------------------------------------------------- */

/* lsp.ini, created with a commented example when missing (caller frees) */
static gchar* ensure_lsp_ini(void) {
    gchar* path = lsp_ini_path();
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        static const gchar EXAMPLE[] =
            "# Language server commands (see docs/configuration.md).\n"
            "# Keys are languages: c, cpp, python, javascript, go, rust...\n"
            "# A command replaces the default one, \"none\" turns the server off.\n"
            "[servers]\n"
            "# c=clangd --background-index\n"
            "# python=pyright-langserver --stdio\n"
            "# rust=none\n";
        gchar* dir = g_path_get_dirname(path);
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
        GError* error = NULL;
        if (!g_file_set_contents(path, EXAMPLE, -1, &error)) {
            g_printerr("[ lsp.ini not created: %s ]\n", error->message);
            g_clear_error(&error);
        }
    }
    return path;
}

static void save_settings(Preferences* p) {
    gchar* path = settings_store_path();
    Settings s;
    settings_store_load(path, &s);
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(p->mode));
    s.editor = MODES[selected < G_N_ELEMENTS(MODES) ? selected : 0];
    s.line_numbers = gtk_check_button_get_active(GTK_CHECK_BUTTON(p->line_numbers));
    s.wrap = gtk_check_button_get_active(GTK_CHECK_BUTTON(p->wrap));
    s.tagbar = gtk_check_button_get_active(GTK_CHECK_BUTTON(p->tagbar));
    s.preview_tagbar = gtk_check_button_get_active(GTK_CHECK_BUTTON(p->preview_tagbar));
    s.color_column = gtk_check_button_get_active(GTK_CHECK_BUTTON(p->ruler))
                         ? gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(p->ruler_column))
                         : 0;
    s.width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(p->width));
    s.height = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(p->height));
    s.auto_terminal = panel_selected(p->auto_terminal);
    s.auto_preview = panel_selected(p->auto_preview);
    GError* error = NULL;
    if (!settings_store_save(path, &s, &error)) {
        g_printerr("[ Preferences not saved: %s ]\n", error->message);
        g_clear_error(&error);
    }
    g_free(path);

    AppData* app = p->app;
    editor_tagbar_set_shown(app, s.tagbar); /* before the copy: they compare */
    preview_tagbar_set_shown(app, s.preview_tagbar);
    gboolean column_changed = (s.color_column != app->settings.color_column);
    app->settings = s;
    if (column_changed) {
        apply_color_column(app);
    }
    EditorMode mode = preferences_editor_mode(s.editor);
    if (mode != app->default_editor_mode) {
        /* Same as picking the global mode in the top bar */
        app->default_editor_mode = mode;
        app->nav_prefix = NAV_PREFIX_NONE;
        app->nav_recenter_step = 0;
        search_mode_changed(app);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
    }
}

static void remember_section(Preferences* p) {
    const gchar* name = gtk_stack_get_visible_child_name(p->stack);
    if (name != NULL) {
        g_free(last_section);
        last_section = g_strdup(name);
    }
}

/* Widget to focus on the visible page */
static GtkWidget* page_focus(Preferences* p) {
    const gchar* name = gtk_stack_get_visible_child_name(p->stack);
    if (g_strcmp0(name, PREFERENCES_APPEARANCE) == 0) {
        return themes_page_focus(p->themes);
    }
    if (g_strcmp0(name, "editor") == 0) {
        return p->mode;
    }
    if (g_strcmp0(name, "window") == 0) {
        return p->width;
    }
    if (g_strcmp0(name, "layouts") == 0) {
        return p->auto_terminal;
    }
    return NULL;
}

void preferences_show(AppData* app, const gchar* section) {
    if (app == NULL || app_dialog_is_open(app->window)) {
        return;
    }
    Preferences p = {0};
    p.app = app;
    GtkWidget* dlg = app_dialog_new(_("Preferences"), app->window, _("_Cancel"), APP_RESPONSE_CANCEL, _("_Apply"),
                                    APP_RESPONSE_OK, NULL);
    p.dialog = APP_DIALOG(dlg);
    app_dialog_set_default_response(p.dialog, APP_RESPONSE_OK);
    app_dialog_set_width(p.dialog, 680);

    gchar* settings_path = settings_store_path();
    Settings s;
    settings_store_load(settings_path, &s);
    g_free(settings_path);

    GtkWidget* stack = gtk_stack_new();
    p.stack = GTK_STACK(stack);
    gtk_widget_set_hexpand(stack, TRUE);
    p.themes = themes_page_new(app, p.dialog);
    GtkWidget* appearance = page_box();
    gtk_box_append(GTK_BOX(appearance), themes_page_widget(p.themes));
    gtk_stack_add_titled(p.stack, appearance, PREFERENCES_APPEARANCE, _("Appearance"));
    gtk_stack_add_titled(p.stack, editor_page(&p), "editor", _("Editor"));
    gtk_stack_add_titled(p.stack, window_page(&p, &s), "window", _("Window"));
    gtk_stack_add_titled(p.stack, layouts_page(&p, &s), "layouts", _("Layouts"));
    gtk_stack_add_titled(p.stack, code_page(&p), "code", _("Code navigation"));
    const gchar* show = (section != NULL) ? section : last_section;
    if (show != NULL && gtk_stack_get_child_by_name(p.stack, show) != NULL) {
        gtk_stack_set_visible_child_name(p.stack, show);
    }

    GtkWidget* sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), p.stack);
    gtk_widget_set_name(sidebar, "preferences-sections");
    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(body, "preferences-body");
    gtk_box_append(GTK_BOX(body), sidebar);
    gtk_box_append(GTK_BOX(body), stack);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(p.dialog)), body);

    gboolean keep = FALSE;
    gchar* lsp_to_edit = NULL;
    while (TRUE) {
        gint resp = app_dialog_run(p.dialog, page_focus(&p));
        gboolean close = FALSE;
        if (themes_page_handle(p.themes, resp, &close)) {
            if (close) {
                keep = TRUE;
                break;
            }
        } else if (resp == RESPONSE_CURRENT_SIZE) {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p.width), gtk_widget_get_width(GTK_WIDGET(app->window)));
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p.height), gtk_widget_get_height(GTK_WIDGET(app->window)));
        } else if (resp == RESPONSE_EDIT_LSP) {
            lsp_to_edit = ensure_lsp_ini();
            keep = TRUE;
            break;
        } else {
            keep = (resp == APP_RESPONSE_OK);
            break;
        }
    }
    remember_section(&p);
    themes_page_finish(p.themes, keep);
    if (keep) {
        save_settings(&p);
    }
    app_dialog_destroy(p.dialog);
    themes_page_free(p.themes);
    if (lsp_to_edit != NULL) {
        editor_open_file(app, "lsp.ini", lsp_to_edit);
        g_free(lsp_to_edit);
    }
}
