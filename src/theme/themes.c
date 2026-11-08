/* Themes in the running application: switching, zoom and the Appearance
 * page of the Preferences dialog (see themes.h). */
#include "theme/themes.h"
#include "core/i18n.h"
#include "core/app_events.h"
#include "widgets/dialog.h"
#include "editor/editor.h"
#include "editor/editor_modes.h"
#include "theme/fonts.h"
#include "theme/icon_sets.h"
#include "core/file_types.h"
#include "theme/image_theme.h"
#include "media/image_utils.h"
#include "widgets/minimap.h"
#include "theme/palette.h"
#include "terminal.h"
#include "theme/text_style.h"
#include "theme/text_tags.h"
#include "theme/theme.h"
#include "theme/theme_store.h"
#include "explorer/tree_nav.h"

#include <glib/gstdio.h>
#include <string.h>

/* The theme shown: name, interface kind, file (user themes) and its
 * modification time, and its own fonts (before the user's) */
static gchar* current_name = NULL;
static gboolean current_dark = TRUE;
static gchar* current_path = NULL;
static gint64 current_mtime = -1;
static gchar* theme_family[FONT_COUNT];
static gint theme_size[FONT_COUNT];

static gint64 file_mtime(const gchar* path) {
    GStatBuf st;
    return (path != NULL && g_stat(path, &st) == 0) ? (gint64)st.st_mtime : -1;
}

const gchar* themes_current_name(void) {
    if (current_name == NULL) {
        gchar* path = theme_store_choice_path();
        current_name = theme_store_load_choice(path);
        g_free(path);
    }
    return current_name;
}

static void load_user_fonts(FontChoice* fonts) {
    gchar* path = theme_store_choice_path();
    theme_store_load_fonts(path, fonts);
    g_free(path);
}

static GPtrArray* list_themes(void) {
    gchar* dir = theme_store_user_dir();
    GPtrArray* themes = theme_store_list(dir, NULL);
    g_free(dir);
    return themes;
}

/* Palette and fonts of a theme with the user's fonts over it; no redraw */
static gboolean set_theme(const gchar* name, const FontChoice* fonts, GError** error) {
    GPtrArray* themes = list_themes();
    gboolean dark = TRUE;
    gboolean ok = theme_store_apply(themes, name, &dark, error);
    if (ok) {
        Theme* t = theme_store_find(themes, name);
        gchar* copy = g_strdup(name);
        g_free(current_name);
        current_name = copy;
        current_dark = dark;
        g_free(current_path);
        current_path = g_strdup(t->path);
        current_mtime = file_mtime(current_path);
        for (gint r = 0; r < FONT_COUNT; r++) {
            g_free(theme_family[r]);
            theme_family[r] = g_strdup(fonts_family((FontRole)r));
            theme_size[r] = fonts_size((FontRole)r);
        }
        fonts_mark_base();
        theme_store_apply_fonts(fonts);
    }
    g_ptr_array_free(themes, TRUE);
    return ok;
}

/* Every widget redrawn: drawing areas (gutters, minimaps, workspace
 * thumbnails) read the palette when they draw */
static void queue_draw_tree(GtkWidget* widget) {
    gtk_widget_queue_draw(widget);
    for (GtkWidget* child = gtk_widget_get_first_child(widget); child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        queue_draw_tree(child);
    }
}

/* Editor tabs of every workspace: minimap caches cleared, status (zoom) updated */
static void refresh_editor_tabs(AppData* app) {
    minimap_viewer_cache_clear(app);
    GHashTable* seen = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (gint w = -1; w < app->num_workspaces; w++) {
        GList* editors = (w < 0) ? app->editors : app->workspaces[w].editors;
        for (GList* l = editors; l != NULL; l = l->next) {
            EditorTab* tab = l->data;
            if (tab != NULL && g_hash_table_add(seen, tab)) {
                minimap_view_cache_clear(tab->minimap);
                editor_modes_update_status(tab);
            }
        }
    }
    g_hash_table_destroy(seen);
}

void themes_refresh(AppData* app, gboolean dark) {
    icon_sets_use(theme_store_icon_set());
    GtkSettings* settings = gtk_settings_get_default();
    if (settings != NULL) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", dark, NULL);
    }
    theme_reload();
    text_tags_refresh_all();
#ifdef HAVE_GSV
    text_style_refresh_scheme(dark);
#endif
#ifdef HAVE_VTE
    terminal_refresh_theme();
#endif
    if (app == NULL) {
        return;
    }
    refresh_editor_tabs(app);
    if (app->window != NULL) {
        queue_draw_tree(GTK_WIDGET(app->window));
    }
    /* Rows and previews written with colours: rebuilt */
    app_emit(app, APP_EVENT_GIT_STATUS_CHANGED);
    app_emit(app, APP_EVENT_FILES_CHANGED);
    app_emit(app, APP_EVENT_PREVIEW_STALE);
}

static void save_choice(const gchar* name) {
    gchar* path = theme_store_choice_path();
    GError* error = NULL;
    if (!theme_store_save_choice(path, name, &error)) {
        g_printerr("[ Theme choice not saved: %s ]\n", error->message);
        g_clear_error(&error);
    }
    g_free(path);
}

static void save_fonts(const FontChoice* fonts) {
    gchar* path = theme_store_choice_path();
    GError* error = NULL;
    if (!theme_store_save_fonts(path, fonts, &error)) {
        g_printerr("[ Fonts not saved: %s ]\n", error->message);
        g_clear_error(&error);
    }
    g_free(path);
}

gboolean themes_apply(AppData* app, const gchar* name, gboolean remember, GError** error) {
    FontChoice fonts = {0};
    load_user_fonts(&fonts);
    gboolean ok = set_theme(name, &fonts, error);
    font_choice_clear(&fonts);
    if (!ok) {
        return FALSE;
    }
    themes_refresh(app, current_dark);
    if (remember) {
        save_choice(name);
    }
    return TRUE;
}

void themes_zoom(AppData* app, gint delta) {
    FontChoice fonts = {0};
    load_user_fonts(&fonts);
    for (gint r = 0; r < FONT_COUNT; r++) {
        fonts.size[r] = (delta == 0) ? 0 : CLAMP(fonts_size((FontRole)r) + delta, FONTS_MIN_SIZE, FONTS_MAX_SIZE);
    }
    save_fonts(&fonts);
    if (set_theme(themes_current_name(), &fonts, NULL)) {
        themes_refresh(app, current_dark);
    }
    font_choice_clear(&fonts);
}

/* ------------------------------------------------------------------------- */
/* Small dialogs                                                             */
/* ------------------------------------------------------------------------- */

static GtkWidget* padded_box(gint spacing) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    return box;
}

static GtkWidget* message_label(const gchar* message) {
    GtkWidget* label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 56);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    return label;
}

static void notice(AppData* app, const gchar* title, const gchar* message) {
    GtkWidget* dlg = app_dialog_new(title, app->window, _("_Close"), APP_RESPONSE_CLOSE, NULL);
    GtkWidget* box = padded_box(0);
    gtk_box_append(GTK_BOX(box), message_label(message));
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CLOSE);
    app_dialog_run(APP_DIALOG(dlg), NULL);
    app_dialog_destroy(APP_DIALOG(dlg));
}

static gboolean confirm(AppData* app, const gchar* title, const gchar* message, const gchar* ok_label) {
    GtkWidget* dlg = app_dialog_new(title, app->window, _("_Cancel"), APP_RESPONSE_CANCEL, NULL);
    GtkWidget* ok = app_dialog_add_button(APP_DIALOG(dlg), ok_label, APP_RESPONSE_OK);
    gtk_widget_add_css_class(ok, "destructive-action");
    GtkWidget* box = padded_box(0);
    gtk_box_append(GTK_BOX(box), message_label(message));
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);
    gint resp = app_dialog_run(APP_DIALOG(dlg),
                               app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
    app_dialog_destroy(APP_DIALOG(dlg));
    return resp == APP_RESPONSE_OK;
}

/* A name typed by the user (caller frees), NULL if cancelled or empty */
static gchar* prompt_name(AppData* app, const gchar* initial) {
    GtkWidget* dlg = app_dialog_new(_("Save Theme As"), app->window, _("_Cancel"), APP_RESPONSE_CANCEL, _("_Save"),
                                    APP_RESPONSE_OK, NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    GtkWidget* box = padded_box(8);
    gtk_box_append(GTK_BOX(box), message_label(_("Save the current colours and fonts as a theme named:")));
    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), initial);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);
    gchar* name = NULL;
    if (app_dialog_run(APP_DIALOG(dlg), entry) == APP_RESPONSE_OK) {
        name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry))));
        if (name[0] == '\0') {
            g_clear_pointer(&name, g_free);
        }
    }
    app_dialog_destroy(APP_DIALOG(dlg));
    return name;
}

/* ------------------------------------------------------------------------- */
/* Theme file edited in a tab                                                */
/* ------------------------------------------------------------------------- */

static gboolean show_theme_error(gpointer data) {
    AppData* app = data;
    gchar* message = g_object_steal_data(G_OBJECT(app->window), "theme-error");
    if (message != NULL && !app_dialog_is_open(app->window)) {
        notice(app, _("Theme Not Applied"), message);
    }
    g_free(message);
    return G_SOURCE_REMOVE;
}

void themes_on_files_changed(AppData* app) {
    if (current_path == NULL || file_mtime(current_path) == current_mtime) {
        return;
    }
    current_mtime = file_mtime(current_path);
    gchar* text = NULL;
    GError* error = NULL;
    Theme* t = NULL;
    if (g_file_get_contents(current_path, &text, NULL, &error)) {
        t = theme_parse(text, current_path, &error);
    }
    g_free(text);
    /* The file may rename its theme */
    gchar* name = (t != NULL) ? g_strdup(t->name) : NULL;
    theme_free(t);
    if (name != NULL && themes_apply(app, name, TRUE, &error)) {
        g_free(name);
        return;
    }
    g_free(name);
    /* Shown once the save is over */
    gchar* message = g_strdup_printf("%s\n\n%s", current_path, error != NULL ? error->message : "");
    g_clear_error(&error);
    g_object_set_data_full(G_OBJECT(app->window), "theme-error", message, g_free);
    g_idle_add(show_theme_error, app);
}

/* ------------------------------------------------------------------------- */
/* The Appearance page of the Preferences dialog                             */
/* ------------------------------------------------------------------------- */

/* Colours shown for each theme in the list */
static const PaletteRole SWATCHES[] = {PAL_BG_VIEW,     PAL_FG_TEXT,    PAL_ACCENT,
                                       PAL_SYN_KEYWORD, PAL_SYN_STRING, PAL_SYN_FUNCTION};

struct ThemesPage {
    AppData* app;
    AppDialog* dialog;
    GtkWidget* widget;
    GtkListBox* list;
    GtkWidget* edit_btn;
    GtkWidget* delete_btn;
    GPtrArray* themes;
    gchar* original_name;     /* restored by Cancel */
    FontChoice original_fonts;
    FontChoice fonts;         /* the user's fonts being edited */
    GtkWidget* family[FONT_COUNT];
    GtkWidget* size[FONT_COUNT];
    gboolean updating;        /* filling widgets: no live preview */
    guint family_timer;
    gchar* to_edit;           /* theme file opened in a tab once the dialog is gone */
};

static void draw_swatch(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    (void)data;
    GdkRGBA color;
    if (!gdk_rgba_parse(&color, g_object_get_data(G_OBJECT(area), "color"))) {
        return;
    }
    gdk_cairo_set_source_rgba(cr, &color);
    cairo_rectangle(cr, 0.5, 0.5, width - 1, height - 1);
    cairo_fill_preserve(cr);
    double r, g, b;
    palette_rgb(PAL_BORDER_STRONG, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
}

static GtkWidget* theme_row(GPtrArray* themes, const Theme* t, gboolean in_use) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    g_object_set_data_full(G_OBJECT(row), "theme-name", g_strdup(t->name), g_free);

    /* Its colours, read by applying it (the caller restores the current theme) */
    GtkWidget* swatches = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_valign(swatches, GTK_ALIGN_CENTER);
    gboolean dark = TRUE;
    theme_store_apply(themes, t->name, &dark, NULL);
    for (gsize i = 0; i < G_N_ELEMENTS(SWATCHES); i++) {
        GtkWidget* area = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 14);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 14);
        g_object_set_data_full(G_OBJECT(area), "color", g_strdup(palette_color(SWATCHES[i])), g_free);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_swatch, NULL, NULL);
        gtk_box_append(GTK_BOX(swatches), area);
    }
    gtk_box_append(GTK_BOX(row), swatches);

    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* name = gtk_label_new(t->name);
    gtk_widget_add_css_class(name, "layout-name");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gchar* base = (t->path != NULL) ? g_path_get_basename(t->path) : NULL;
    gchar* origin = base != NULL ? g_strdup_printf(_("your theme, %s"), base) : g_strdup(_("built-in"));
    gchar* summary_text = g_strdup_printf("%s · %s%s", dark ? _("Dark") : _("Light"), origin,
                                          in_use ? _(" · in use") : "");
    g_free(origin);
    g_free(base);
    GtkWidget* summary = gtk_label_new(summary_text);
    g_free(summary_text);
    gtk_widget_add_css_class(summary, "dim-label");
    gtk_widget_add_css_class(summary, "layout-summary");
    gtk_label_set_xalign(GTK_LABEL(summary), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(summary), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(text), name);
    gtk_box_append(GTK_BOX(text), summary);
    gtk_box_append(GTK_BOX(row), text);
    return row;
}

static const gchar* row_name(GtkListBoxRow* row) {
    GtkWidget* child = (row != NULL) ? gtk_list_box_row_get_child(row) : NULL;
    return (child != NULL) ? g_object_get_data(G_OBJECT(child), "theme-name") : NULL;
}

static const Theme* selected_theme(ThemesPage* d) {
    return theme_store_find(d->themes, row_name(gtk_list_box_get_selected_row(d->list)));
}

/* Edit and Delete act on the user's theme files only */
static void update_buttons(ThemesPage* d) {
    const Theme* t = selected_theme(d);
    gboolean user = (t != NULL && t->path != NULL);
    gtk_widget_set_sensitive(d->edit_btn, user);
    gtk_widget_set_sensitive(d->delete_btn, user);
}

/* Font fields: the user's family (the theme's as placeholder) and the size */
static void update_font_fields(ThemesPage* d) {
    d->updating = TRUE;
    for (gint r = 0; r < FONT_COUNT; r++) {
        gtk_editable_set_text(GTK_EDITABLE(d->family[r]), d->fonts.family[r] != NULL ? d->fonts.family[r] : "");
        gtk_entry_set_placeholder_text(GTK_ENTRY(d->family[r]), theme_family[r]);
        gtk_widget_remove_css_class(d->family[r], "error");
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->size[r]), fonts_size((FontRole)r));
    }
    d->updating = FALSE;
}

/* Reload the theme list and select name (the first theme if missing) */
static void fill_list(ThemesPage* d, const gchar* select) {
    if (d->themes != NULL) {
        g_ptr_array_free(d->themes, TRUE);
    }
    d->themes = list_themes();
    d->updating = TRUE;
    for (GtkListBoxRow* row = gtk_list_box_get_row_at_index(d->list, 0); row != NULL;
         row = gtk_list_box_get_row_at_index(d->list, 0)) {
        gtk_list_box_remove(d->list, GTK_WIDGET(row));
    }
    gint index = 0;
    for (guint i = 0; i < d->themes->len; i++) {
        const Theme* t = g_ptr_array_index(d->themes, i);
        gtk_list_box_append(d->list, theme_row(d->themes, t, g_strcmp0(t->name, d->original_name) == 0));
        if (g_strcmp0(t->name, select) == 0) {
            index = (gint)i;
        }
    }
    /* The swatches applied each theme: back to the one shown */
    set_theme(current_name, &d->fonts, NULL);
    gtk_list_box_select_row(d->list, gtk_list_box_get_row_at_index(d->list, index));
    d->updating = FALSE;
    update_buttons(d);
}

/* Live preview: selecting a theme applies it */
static void on_row_selected(GtkListBox* list, GtkListBoxRow* row, gpointer data) {
    (void)list;
    ThemesPage* d = data;
    if (d->updating || row == NULL) {
        return;
    }
    const gchar* name = row_name(row);
    if (name != NULL && g_strcmp0(name, current_name) != 0 && set_theme(name, &d->fonts, NULL)) {
        themes_refresh(d->app, current_dark);
    }
    update_font_fields(d);
    update_buttons(d);
}

static void on_row_activated(GtkListBox* list, GtkListBoxRow* row, gpointer data) {
    (void)list;
    (void)row;
    app_dialog_response(APP_DIALOG(data), APP_RESPONSE_OK);
}

static void apply_fonts_live(ThemesPage* d) {
    if (set_theme(current_name, &d->fonts, NULL)) {
        themes_refresh(d->app, current_dark);
    }
}

static void on_size_changed(GtkSpinButton* spin, gpointer data) {
    ThemesPage* d = data;
    if (d->updating) {
        return;
    }
    for (gint r = 0; r < FONT_COUNT; r++) {
        if (GTK_WIDGET(spin) == d->size[r]) {
            gint size = gtk_spin_button_get_value_as_int(spin);
            d->fonts.size[r] = (size == theme_size[r]) ? 0 : size;
        }
    }
    apply_fonts_live(d);
}

/* Families are applied once the typing pauses */
static gboolean apply_families(gpointer data) {
    ThemesPage* d = data;
    d->family_timer = 0;
    gboolean valid = TRUE;
    for (gint r = 0; r < FONT_COUNT; r++) {
        gchar* text = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(d->family[r]))));
        if (text[0] == '\0') {
            g_clear_pointer(&d->fonts.family[r], g_free);
            gtk_widget_remove_css_class(d->family[r], "error");
        } else if (fonts_valid_family(text)) {
            g_free(d->fonts.family[r]);
            d->fonts.family[r] = g_strdup(text);
            gtk_widget_remove_css_class(d->family[r], "error");
        } else {
            gtk_widget_add_css_class(d->family[r], "error");
            valid = FALSE;
        }
        g_free(text);
    }
    if (valid) {
        apply_fonts_live(d);
    }
    return G_SOURCE_REMOVE;
}

/* A family being typed is applied before any button acts */
static void flush_families(ThemesPage* d) {
    if (d->family_timer != 0) {
        g_source_remove(d->family_timer);
        apply_families(d);
    }
}

static void on_family_changed(GtkEditable* editable, gpointer data) {
    (void)editable;
    ThemesPage* d = data;
    if (d->updating) {
        return;
    }
    if (d->family_timer != 0) {
        g_source_remove(d->family_timer);
    }
    d->family_timer = g_timeout_add(500, apply_families, d);
}

static GtkWidget* fonts_grid(ThemesPage* d) {
    static const gchar* const LABELS[FONT_COUNT] = {[FONT_UI] = N_("Interface"), [FONT_CODE] = N_("Code")};
    static const gchar* const TIPS[FONT_COUNT] = {
        [FONT_UI] = N_("Interface and documents: font names separated by commas (empty: the theme's)"),
        [FONT_CODE] = N_("Files, code and terminals: font names separated by commas (empty: the theme's)"),
    };
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    for (gint r = 0; r < FONT_COUNT; r++) {
        GtkWidget* label = gtk_label_new(_(LABELS[r]));
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        d->family[r] = gtk_entry_new();
        gtk_widget_set_hexpand(d->family[r], TRUE);
        gtk_widget_set_tooltip_text(d->family[r], _(TIPS[r]));
        d->size[r] = gtk_spin_button_new_with_range(FONTS_MIN_SIZE, FONTS_MAX_SIZE, 1);
        gtk_widget_set_tooltip_text(d->size[r], _("Size in pixels (Ctrl+= / Ctrl+- / Ctrl+0)"));
        gtk_grid_attach(GTK_GRID(grid), label, 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), d->family[r], 1, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), d->size[r], 2, r, 1, 1);
        g_signal_connect(d->family[r], "changed", G_CALLBACK(on_family_changed), d);
        g_signal_connect(d->size[r], "value-changed", G_CALLBACK(on_size_changed), d);
    }
    return grid;
}

static void copy_fonts(FontChoice* to, const FontChoice* from) {
    font_choice_clear(to);
    for (gint r = 0; r < FONT_COUNT; r++) {
        to->family[r] = g_strdup(from->family[r]);
        to->size[r] = from->size[r];
    }
}

/* The page buttons report their response on the dialog, handled by
 * themes_page_handle() once app_dialog_run() returns */
static void on_page_button_clicked(GtkButton* button, gpointer data) {
    gint response = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response"));
    app_dialog_response(APP_DIALOG(data), response);
}

static GtkWidget* page_button(ThemesPage* d, GtkWidget* row, const gchar* label, gint response,
                              const gchar* tooltip) {
    GtkWidget* button = gtk_button_new_with_mnemonic(label);
    gtk_widget_set_tooltip_text(button, tooltip);
    g_object_set_data(G_OBJECT(button), "response", GINT_TO_POINTER(response));
    g_signal_connect(button, "clicked", G_CALLBACK(on_page_button_clicked), d->dialog);
    gtk_box_append(GTK_BOX(row), button);
    return button;
}

ThemesPage* themes_page_new(AppData* app, AppDialog* dialog) {
    ThemesPage* d = g_new0(ThemesPage, 1);
    d->app = app;
    d->dialog = dialog;
    d->original_name = g_strdup(themes_current_name());
    load_user_fonts(&d->original_fonts);
    copy_fonts(&d->fonts, &d->original_fonts);
    /* What is shown is the current theme with the user's fonts */
    set_theme(d->original_name, &d->fonts, NULL);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    d->widget = box;
    gchar* dir = theme_store_user_dir();
    gchar* hint_text = g_strdup_printf(_("Selecting a theme applies it at once; Apply keeps it, Cancel goes "
                                         "back. Your themes are INI files in %s."), dir);
    g_free(dir);
    GtkWidget* hint = message_label(hint_text);
    g_free(hint_text);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_box_append(GTK_BOX(box), hint);

    GtkWidget* list = gtk_list_box_new();
    d->list = GTK_LIST_BOX(list);
    gtk_widget_set_name(list, "theme-list");
    gtk_list_box_set_selection_mode(d->list, GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(d->list, FALSE);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 150);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(box), scroll);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    page_button(d, buttons, _("_Save As…"), THEMES_RESPONSE_SAVE_AS,
                _("Save the current colours and fonts as a theme of yours"));
    page_button(d, buttons, _("From _Image…"), THEMES_RESPONSE_FROM_IMAGE,
                _("Create a theme of yours from the colours of an image (the selected one by default)"));
    d->edit_btn = page_button(d, buttons, _("_Edit"), THEMES_RESPONSE_EDIT,
                              _("Open the selected theme file in an editor tab: saving it applies it "
                                "(your themes only)"));
    d->delete_btn = page_button(d, buttons, _("_Delete"), THEMES_RESPONSE_DELETE,
                                _("Delete the selected theme file (your themes only)"));
    gtk_widget_add_css_class(d->delete_btn, "destructive-action");
    gtk_box_append(GTK_BOX(box), buttons);

    GtkWidget* fonts_title = gtk_label_new(_("Your fonts, over any theme"));
    gtk_widget_add_css_class(fonts_title, "dialog-section");
    gtk_label_set_xalign(GTK_LABEL(fonts_title), 0.0f);
    gtk_widget_set_margin_top(fonts_title, 6);
    gtk_box_append(GTK_BOX(box), fonts_title);
    gtk_box_append(GTK_BOX(box), fonts_grid(d));

    g_signal_connect(list, "row-selected", G_CALLBACK(on_row_selected), d);
    g_signal_connect(list, "row-activated", G_CALLBACK(on_row_activated), dialog);
    fill_list(d, d->original_name);
    update_font_fields(d);
    return d;
}

GtkWidget* themes_page_widget(ThemesPage* page) {
    return page->widget;
}

GtkWidget* themes_page_focus(ThemesPage* page) {
    GtkListBoxRow* sel = gtk_list_box_get_selected_row(page->list);
    return sel != NULL ? GTK_WIDGET(sel) : GTK_WIDGET(page->list);
}

static void save_as(ThemesPage* d) {
    AppData* app = d->app;
    gchar* suggestion = g_strdup_printf(_("%s (mine)"), current_name);
    gchar* name = prompt_name(app, suggestion);
    g_free(suggestion);
    const Theme* existing = theme_store_find(d->themes, name);
    if (name != NULL && existing != NULL && existing->path == NULL) {
        notice(app, _("Theme Not Saved"), _("A built-in theme has this name: choose another one."));
    } else if (name != NULL) {
        gchar* udir = theme_store_user_dir();
        GError* error = NULL;
        gchar* path = theme_store_save_current(udir, name, current_dark, &error);
        g_free(udir);
        if (path == NULL) {
            gchar* msg = g_strdup_printf(_("The theme could not be saved: %s"), error->message);
            notice(app, _("Theme Not Saved"), msg);
            g_free(msg);
            g_clear_error(&error);
        } else {
            /* The new theme holds the fonts: no override needed */
            font_choice_clear(&d->fonts);
            fill_list(d, name);
            set_theme(name, &d->fonts, NULL);
            update_font_fields(d);
        }
        g_free(path);
    }
    g_free(name);
}

/* An image path and a theme name typed by the user (caller frees both);
 * FALSE if cancelled or one is empty */
static gboolean prompt_image(AppData* app, gchar** path, gchar** name) {
    ExplorerRow* row = tree_nav_selected(app);
    const gchar* selected = (row != NULL && row->path != NULL && is_image_file(row->path)) ? row->path : "";
    GtkWidget* dlg = app_dialog_new(_("Theme from Image"), app->window, _("_Cancel"), APP_RESPONSE_CANCEL, _("_Create"),
                                    APP_RESPONSE_OK, NULL);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    GtkWidget* box = padded_box(8);
    gtk_box_append(GTK_BOX(box), message_label(_("Create a theme from the colours of the image file:")));
    GtkWidget* path_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(path_entry), selected);
    gtk_entry_set_activates_default(GTK_ENTRY(path_entry), TRUE);
    gtk_box_append(GTK_BOX(box), path_entry);
    gtk_box_append(GTK_BOX(box), message_label(_("Named:")));
    GtkWidget* name_entry = gtk_entry_new();
    gchar* base = g_path_get_basename(selected[0] != '\0' ? selected : "Image");
    gchar* dot = strrchr(base, '.');
    if (dot != NULL && dot != base) {
        *dot = '\0';
    }
    gtk_editable_set_text(GTK_EDITABLE(name_entry), base);
    g_free(base);
    gtk_entry_set_activates_default(GTK_ENTRY(name_entry), TRUE);
    gtk_box_append(GTK_BOX(box), name_entry);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);
    *path = NULL;
    *name = NULL;
    if (app_dialog_run(APP_DIALOG(dlg), selected[0] != '\0' ? name_entry : path_entry) == APP_RESPONSE_OK) {
        *path = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(path_entry))));
        *name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(name_entry))));
    }
    app_dialog_destroy(APP_DIALOG(dlg));
    if (*path == NULL || (*path)[0] == '\0' || (*name)[0] == '\0') {
        g_clear_pointer(path, g_free);
        g_clear_pointer(name, g_free);
        return FALSE;
    }
    return TRUE;
}

/* Colours of an image file (scaled down: the main colours are enough) */
static gboolean derive_from_file(const gchar* path, ImageTheme* theme) {
    GdkPixbuf* pixbuf = pixbuf_from_file(path, 512);
    if (pixbuf == NULL) {
        return FALSE;
    }
    gboolean ok = gdk_pixbuf_get_bits_per_sample(pixbuf) == 8 &&
                  image_theme_derive(gdk_pixbuf_read_pixels(pixbuf), gdk_pixbuf_get_width(pixbuf),
                                     gdk_pixbuf_get_height(pixbuf), gdk_pixbuf_get_rowstride(pixbuf),
                                     gdk_pixbuf_get_n_channels(pixbuf), theme);
    g_object_unref(pixbuf);
    return ok;
}

static void save_from_image(ThemesPage* d) {
    AppData* app = d->app;
    gchar* path = NULL;
    gchar* name = NULL;
    if (!prompt_image(app, &path, &name)) {
        return;
    }
    ImageTheme colors;
    const Theme* existing = theme_store_find(d->themes, name);
    if (existing != NULL && existing->path == NULL) {
        notice(app, _("Theme Not Created"), _("A built-in theme has this name: choose another one."));
    } else if (!derive_from_file(path, &colors)) {
        gchar* msg = g_strdup_printf(_("%s cannot be read as an image (or has no opaque pixel)."), path);
        notice(app, _("Theme Not Created"), msg);
        g_free(msg);
    } else {
        /* The current look with the image's colours, saved as a new theme */
        for (gint r = 0; r < PAL_COUNT; r++) {
            palette_set((PaletteRole)r, colors.colors[r]);
        }
        gchar* udir = theme_store_user_dir();
        GError* error = NULL;
        gchar* file = theme_store_save_current(udir, name, colors.dark, &error);
        g_free(udir);
        if (file == NULL) {
            gchar* msg = g_strdup_printf(_("The theme could not be saved: %s"), error->message);
            notice(app, _("Theme Not Created"), msg);
            g_free(msg);
            g_clear_error(&error);
            set_theme(current_name, &d->fonts, NULL);
        } else {
            /* The new theme holds the fonts: no override needed */
            font_choice_clear(&d->fonts);
            fill_list(d, name);
            set_theme(name, &d->fonts, NULL);
            update_font_fields(d);
        }
        themes_refresh(app, current_dark);
        g_free(file);
    }
    g_free(path);
    g_free(name);
}

gboolean themes_page_handle(ThemesPage* page, gint response, gboolean* close) {
    ThemesPage* d = page;
    *close = FALSE;
    flush_families(d);
    const Theme* t = selected_theme(d);
    switch (response) {
    case THEMES_RESPONSE_SAVE_AS:
        save_as(d);
        return TRUE;
    case THEMES_RESPONSE_FROM_IMAGE:
        save_from_image(d);
        return TRUE;
    case THEMES_RESPONSE_DELETE:
        if (t != NULL && t->path != NULL) {
            gchar* msg = g_strdup_printf(_("Delete the theme “%1$s” and its file %2$s?"), t->name, t->path);
            if (confirm(d->app, _("Delete Theme"), msg, _("_Delete"))) {
                g_remove(t->path);
                fill_list(d, THEME_DEFAULT_NAME);
                on_row_selected(d->list, gtk_list_box_get_selected_row(d->list), d);
            }
            g_free(msg);
        }
        return TRUE;
    case THEMES_RESPONSE_EDIT:
        if (t != NULL && t->path != NULL) {
            g_free(d->to_edit);
            d->to_edit = g_strdup(t->path);
            *close = TRUE;
        }
        return TRUE;
    default:
        return FALSE;
    }
}

void themes_page_finish(ThemesPage* page, gboolean keep) {
    flush_families(page);
    if (keep) {
        save_choice(current_name);
        save_fonts(&page->fonts);
    } else if (set_theme(page->original_name, &page->original_fonts, NULL)) {
        /* Back to what was shown before */
        themes_refresh(page->app, current_dark);
    }
    g_signal_handlers_disconnect_by_func(page->list, on_row_selected, page);
}

void themes_page_free(ThemesPage* page) {
    if (page == NULL) {
        return;
    }
    if (page->family_timer != 0) {
        g_source_remove(page->family_timer);
    }
    g_ptr_array_free(page->themes, TRUE);
    font_choice_clear(&page->fonts);
    font_choice_clear(&page->original_fonts);
    g_free(page->original_name);
    if (page->to_edit != NULL) {
        gchar* base = g_path_get_basename(page->to_edit);
        editor_open_file(page->app, base, page->to_edit);
        g_free(base);
        g_free(page->to_edit);
    }
    g_free(page);
}
