/* Jupyter notebook tabs of the editor area (see ipynb_tab.h) */
#include "jupyter/ipynb_tab.h"
#include "core/i18n.h"
#include "editor/editor.h"
#include "editor/file_save.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_tab_private.h"
#include "markup/markdown.h"
#include "markup/math_paintable.h"
#include "theme/fonts.h"
#include "theme/icons.h"
#include "theme/text_style.h"
#include "theme/theme.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#define IPYNB_TAB_KEY "lookin-ipynb-tab"
#define CELL_INDEX_KEY "lookin-ipynb-cell"
#define CELL_PROMPT_KEY "lookin-ipynb-prompt"   /* on a cell's view: its In [n] label */
#define CELL_OUTPUTS_KEY "lookin-ipynb-outputs" /* on a cell's view: its outputs box */
#define CELL_RENDERED_KEY "lookin-ipynb-rendered" /* on a Markdown cell's view: its rendered view */
#define TAB_MAX_CHARS 24
#define SAVE_DELAY_MS 1000
#define MAX_OUTPUT_LINES 200
#define MAX_NOTEBOOK_BYTES (64 * 1024 * 1024)
#define IMAGE_MAX_HEIGHT 480


static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".ipynb-tab { background-color: @bg_view; }"
                  ".ipynb-tab .ipynb-bar { padding: 3px 8px; background-color: @bg_bar;"
                  " border-bottom: 1px solid @border; font-size: @size_bar; }"
                  ".ipynb-tab .ipynb-bar button { padding: 0 8px; border: none; min-height: 23px; }"
                  ".ipynb-tab .ipynb-bar label { color: @fg_muted; }"
                  ".ipynb-tab .ipynb-bar .ipynb-error { color: @error; }"
                  ".ipynb-tab .ipynb-input { padding: 4px 8px; background-color: @bg_bar;"
                  " border-bottom: 1px solid @border; }"
                  ".ipynb-tab .ipynb-cells { padding: 12px 16px 48px 16px; }"
                  ".ipynb-tab .ipynb-cell { border: 1px solid @border; border-left: 3px solid @border;"
                  " border-radius: 4px; background-color: @bg_view; }"
                  ".ipynb-tab .ipynb-cell.ipynb-current { border-left-color: @accent; }"
                  ".ipynb-tab .ipynb-cell-head { padding: 4px 4px 3px 8px; color: @fg_subtle; }"
                  ".ipynb-tab .ipynb-cell-head label { color: @fg_subtle; font-family: @font_code;"
                  " font-size: @size_code; }"
                  ".ipynb-tab .ipynb-cell-head button { padding: 0 6px; border: none; min-height: 23px; }"
                  ".ipynb-tab .ipynb-cell-head menubutton label { color: @fg_muted; font-family: @font_ui;"
                  " font-size: @size_bar; }"
                  ".ipynb-tab .ipynb-source { background-color: @bg_block; }"
                  ".ipynb-tab .ipynb-source text { background-color: @bg_block; }"
                  ".ipynb-tab .ipynb-rendered, .ipynb-tab .ipynb-rendered text { background-color: @bg_view;"
                  " color: @fg_text; }"
                  ".ipynb-tab .ipynb-outputs { padding: 6px 10px; border-top: 1px solid @border; }"
                  ".ipynb-tab .ipynb-output { color: @fg_text; font-family: @font_code; font-size: @size_code; }"
                  ".ipynb-tab .ipynb-output.ipynb-error { color: @error; }"
                  ".ipynb-tab .ipynb-out-prompt { color: @fg_subtle; font-family: @font_code;"
                  " font-size: @size_code; }");
}

static IpynbTab* tab_of(GtkWidget* root) {
    return root != NULL ? g_object_get_data(G_OBJECT(root), IPYNB_TAB_KEY) : NULL;
}

/* The notebook holding page, or NULL */
static GtkNotebook* notebook_of(GtkWidget* page) {
    GtkWidget* parent = gtk_widget_get_parent(page);
    GtkWidget* nb = parent != NULL ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    return (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), page) >= 0) ? GTK_NOTEBOOK(nb) : NULL;
}

/* In the window: a tab of the workspace shown */
static gboolean tab_shown(IpynbTab* t) {
    return t->app->window != NULL && gtk_widget_get_root(t->root) == GTK_ROOT(t->app->window);
}

/* The open tab of the notebook at path (any workspace), or NULL */
static IpynbTab* find_tab(AppData* app, const gchar* path) {
    for (guint i = 0; app->ipynb_pages != NULL && i < app->ipynb_pages->len; i++) {
        IpynbTab* t = tab_of(g_ptr_array_index(app->ipynb_pages, i));
        if (t != NULL && !t->closed && g_strcmp0(t->path, path) == 0) return t;
    }
    return NULL;
}

/* The tab holding the keyboard focus, or NULL */
static IpynbTab* focused_tab(AppData* app) {
    GtkWidget* f = app->window != NULL ? gtk_window_get_focus(app->window) : NULL;
    for (guint i = 0; f != NULL && app->ipynb_pages != NULL && i < app->ipynb_pages->len; i++) {
        GtkWidget* root = g_ptr_array_index(app->ipynb_pages, i);
        if (f == root || gtk_widget_is_ancestor(f, root)) return tab_of(root);
    }
    return NULL;
}

/* --- Saving --- */

void ipynb_tab_set_status(IpynbTab* t, const gchar* text, gboolean error) {
    gtk_label_set_text(GTK_LABEL(t->status), text);
    if (error) {
        gtk_widget_add_css_class(t->status, "ipynb-error");
    } else {
        gtk_widget_remove_css_class(t->status, "ipynb-error");
    }
}

void ipynb_tab_update_info(IpynbTab* t) {
    guint n = ipynb_doc_n_cells(t->doc);
    gchar* cells = g_strdup_printf(ngettext("%u cell", "%u cells", n), n);
    const gchar* kernel = ipynb_doc_kernel(t->doc);
    gchar* text = kernel != NULL ? g_strdup_printf("%s · %s", kernel, cells) : g_strdup(cells);
    gtk_label_set_text(GTK_LABEL(t->info), text);
    g_free(text);
    g_free(cells);
}

static void save_now(IpynbTab* t) {
    if (t->save_source != 0) {
        g_source_remove(t->save_source);
        t->save_source = 0;
    }
    if (!t->dirty) return;
    gchar* json = ipynb_doc_to_json(t->doc);
    GError* error = NULL;
    if (file_save_text(t->path, json, -1, &error)) {
        t->dirty = FALSE;
        if (!t->closed) ipynb_tab_set_status(t, _("Saved"), FALSE);
    } else {
        gchar* msg = g_strdup_printf(_("Not saved: %s"), error->message);
        if (!t->closed) ipynb_tab_set_status(t, msg, TRUE);
        g_warning("%s: %s", t->path, msg);
        g_free(msg);
        g_error_free(error);
    }
    g_free(json);
}

static gboolean on_save_timeout(gpointer data) {
    IpynbTab* t = data;
    t->save_source = 0;
    save_now(t);
    return G_SOURCE_REMOVE;
}

void ipynb_tab_mark_changed(IpynbTab* t) {
    t->dirty = TRUE;
    ipynb_tab_set_status(t, _("Modified"), FALSE);
    if (t->save_source != 0) g_source_remove(t->save_source);
    t->save_source = g_timeout_add(SAVE_DELAY_MS, on_save_timeout, t);
    ipynb_tagbar_changed(t);
}

/* --- Cells --- */

static void rebuild(IpynbTab* t);

static gboolean on_rebuild_idle(gpointer data) {
    IpynbTab* t = data;
    t->rebuild_source = 0;
    if (!t->closed) rebuild(t);
    return G_SOURCE_REMOVE;
}

/* Build the cells again (after a change of their number, order or type),
 * once the signal being handled is over; then focus the cell focus */
static void queue_rebuild(IpynbTab* t, guint focus) {
    t->focus_after = focus;
    if (t->rebuild_source == 0) t->rebuild_source = g_idle_add(on_rebuild_idle, t);
}

void ipynb_tab_rebuild_cells(IpynbTab* t) {
    queue_rebuild(t, t->current);
}

/* The rendered view of the Markdown cell at index when it is shown, else NULL */
static GtkWidget* shown_rendered(IpynbTab* t, guint index) {
    if (index >= t->views->len) return NULL;
    GtkWidget* rendered = g_object_get_data(G_OBJECT(g_ptr_array_index(t->views, index)), CELL_RENDERED_KEY);
    return rendered != NULL && gtk_widget_get_visible(rendered) ? rendered : NULL;
}

void ipynb_tab_focus_cell(IpynbTab* t, guint index) {
    if (t->views->len == 0) return;
    index = MIN(index, t->views->len - 1);
    GtkWidget* rendered = shown_rendered(t, index);
    gtk_widget_grab_focus(rendered != NULL ? rendered : g_ptr_array_index(t->views, index));
}

static guint cell_index(gpointer object) {
    return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(object), CELL_INDEX_KEY));
}

/* --- Rendered Markdown cells --- */

/* Show the Markdown cell at index rendered, or as its source (focused) */
static void show_markdown(IpynbTab* t, guint index, gboolean render) {
    if (index >= t->views->len) return;
    GtkWidget* view = g_ptr_array_index(t->views, index);
    GtkWidget* rendered = g_object_get_data(G_OBJECT(view), CELL_RENDERED_KEY);
    if (rendered == NULL) return;
    guint key = ipynb_doc_cell_key(t->doc, index);
    gchar* source = ipynb_doc_cell_markdown(t->doc, index);
    gchar* valid = g_utf8_make_valid(source, -1);
    g_free(source);
    /* An empty cell has nothing to show: it stays as source */
    gboolean blank = TRUE;
    for (const gchar* c = valid; blank && *c != '\0'; c++) blank = g_ascii_isspace(*c);
    if (blank) render = FALSE;
    if (render) {
        g_hash_table_remove(t->editing, GUINT_TO_POINTER(key));
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(rendered));
        render_markdown_to_buffer(buffer, GTK_TEXT_VIEW(rendered), valid, t->path, FALSE, NULL, 0);
    } else {
        g_hash_table_add(t->editing, GUINT_TO_POINTER(key));
    }
    g_free(valid);
    gboolean had_focus = gtk_widget_has_focus(view) || gtk_widget_has_focus(rendered);
    gtk_widget_set_visible(view, !render);
    gtk_widget_set_visible(rendered, render);
    gtk_widget_set_visible(gtk_widget_get_parent(rendered), render);
    if (had_focus || !render) gtk_widget_grab_focus(render ? rendered : view);
}

void ipynb_tab_render_cell(IpynbTab* t, guint index) {
    show_markdown(t, index, TRUE);
}

/* Double click on a rendered cell: edit its source */
static void on_rendered_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)x;
    (void)y;
    if (n_press != 2) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    GtkWidget* rendered = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    show_markdown(data, cell_index(rendered), FALSE);
}

/* Enter on a rendered cell: edit its source */
static gboolean on_rendered_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                                gpointer data) {
    (void)keycode;
    if ((keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter) ||
        (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK | GDK_SUPER_MASK)) != 0) {
        return FALSE;
    }
    GtkWidget* rendered = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    show_markdown(data, cell_index(rendered), FALSE);
    return TRUE;
}

void ipynb_tab_insert_cell(IpynbTab* t, guint at, IpynbCellType type) {
    guint index = ipynb_doc_insert_cell(t->doc, at, type);
    ipynb_tab_mark_changed(t);
    queue_rebuild(t, index);
}

static void move_cell(IpynbTab* t, guint index, gint by) {
    guint n = ipynb_doc_n_cells(t->doc);
    if (index >= n || (by < 0 && index == 0) || (by > 0 && index + 1 >= n)) return;
    guint to = by < 0 ? index - 1 : index + 1;
    ipynb_doc_move_cell(t->doc, index, to);
    ipynb_tab_mark_changed(t);
    queue_rebuild(t, to);
}

static void delete_cell(IpynbTab* t, guint index) {
    IpynbCell* cell = ipynb_doc_take_cell(t->doc, index);
    if (cell == NULL) return;
    if (t->deleted != NULL) ipynb_cell_free(t->deleted);
    t->deleted = cell;
    t->deleted_at = index;
    gtk_widget_set_visible(t->undo_button, TRUE);
    /* A notebook keeps one cell to type in */
    if (ipynb_doc_n_cells(t->doc) == 0) ipynb_doc_insert_cell(t->doc, 0, IPYNB_CELL_CODE);
    ipynb_tab_mark_changed(t);
    queue_rebuild(t, index > 0 && index >= ipynb_doc_n_cells(t->doc) ? index - 1 : index);
}

static void on_undo_delete(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    if (t->deleted == NULL) return;
    guint index = ipynb_doc_put_cell(t->doc, t->deleted_at, t->deleted);
    t->deleted = NULL;
    gtk_widget_set_visible(t->undo_button, FALSE);
    ipynb_tab_mark_changed(t);
    queue_rebuild(t, index);
}

static void on_source_changed(GtkTextBuffer* buffer, gpointer data) {
    IpynbTab* t = data;
    if (t->building || t->closed) return;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    ipynb_doc_set_source(t->doc, cell_index(buffer), text);
    g_free(text);
    ipynb_tab_mark_changed(t);
}

static void set_current(IpynbTab* t, guint index) {
    t->current = index;
    for (guint i = 0; i < t->views->len; i++) {
        GtkWidget* cell = gtk_widget_get_parent(g_ptr_array_index(t->views, i));
        if (i == index) {
            gtk_widget_add_css_class(cell, "ipynb-current");
        } else {
            gtk_widget_remove_css_class(cell, "ipynb-current");
        }
    }
    ipynb_tagbar_cursor_moved(t);
}

static void on_view_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    IpynbTab* t = data;
    set_current(t, cell_index(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl))));
}

static void track_focus(IpynbTab* t, GtkWidget* view) {
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(on_view_focus_enter), t);
    gtk_widget_add_controller(view, focus);
}

/* The wheel over a rendered cell scrolls the cells: its own scrolled window
 * (horizontal only) would take the vertical wheel for itself (Shift+wheel
 * goes on, to a wide table) */
static gboolean on_rendered_scroll(GtkEventControllerScroll* ctrl, gdouble dx, gdouble dy, gpointer data) {
    (void)dx;
    IpynbTab* t = data;
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if (dy == 0 || t->scroll == NULL || (state & GDK_SHIFT_MASK) != 0) return FALSE;
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(t->scroll));
    gdouble page = gtk_adjustment_get_page_size(adj);
    /* As GtkScrolledWindow does: a wheel step scrolls page^(2/3) */
    gdouble step = gtk_event_controller_scroll_get_unit(ctrl) == GDK_SCROLL_UNIT_WHEEL ? pow(page, 2.0 / 3.0) : 1.0;
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + dy * step);
    return TRUE;
}

/* The rendered view of a Markdown cell (read only, focusable for the keys),
 * in a GtkScrolledWindow (its parent, hidden with it) that scrolls it
 * horizontally without a scrollbar: the tables of the Markdown renderer
 * follow the width of that window (markdown_table.c), so the view never
 * holds the cells wider than the tab */
static GtkWidget* rendered_view_new(IpynbTab* t, guint index) {
    GtkWidget* rendered = gtk_text_view_new();
    gtk_widget_add_css_class(rendered, "ipynb-rendered");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(rendered), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(rendered), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(rendered), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(rendered), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(rendered), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(rendered), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(rendered), 8);
    set_view_monospace(rendered, FALSE);
    ensure_markdown_tags(gtk_text_view_get_buffer(GTK_TEXT_VIEW(rendered)));
    gtk_widget_set_tooltip_text(rendered, _("Double-click or Enter to edit the Markdown"));
    g_object_set_data(G_OBJECT(rendered), CELL_INDEX_KEY, GUINT_TO_POINTER(index));
    track_focus(t, rendered);
    /* Capture: before the view's own double click (select a word) */
    GtkGesture* click = gtk_gesture_click_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_rendered_pressed), t);
    gtk_widget_add_controller(rendered, GTK_EVENT_CONTROLLER(click));
    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_rendered_key), t);
    gtk_widget_add_controller(rendered, keys);
    gtk_widget_set_visible(rendered, FALSE);
    GtkWidget* frame = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(frame), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(frame), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(frame), rendered);
    GtkEventController* wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(wheel, GTK_PHASE_CAPTURE);
    g_signal_connect(wheel, "scroll", G_CALLBACK(on_rendered_scroll), t);
    gtk_widget_add_controller(frame, wheel);
    gtk_widget_set_visible(frame, FALSE);
    return rendered;
}

#define CELL_TYPE_KEY "lookin-ipynb-cell-type"

static void on_type_chosen(GtkButton* item, gpointer data) {
    IpynbTab* t = data;
    GtkWidget* popover = gtk_widget_get_ancestor(GTK_WIDGET(item), GTK_TYPE_POPOVER);
    if (popover != NULL) gtk_popover_popdown(GTK_POPOVER(popover));
    guint index = cell_index(item);
    IpynbCellType type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), CELL_TYPE_KEY));
    if (type == ipynb_doc_cell_type(t->doc, index)) return;
    ipynb_doc_set_type(t->doc, index, type);
    /* Made Markdown: still edited until run */
    g_hash_table_add(t->editing, GUINT_TO_POINTER(ipynb_doc_cell_key(t->doc, index)));
    ipynb_tab_mark_changed(t);
    queue_rebuild(t, index);
}

/* The type of a cell: a menu button like the kernel one of the bar */
static GtkWidget* type_menu_new(IpynbTab* t, guint index, IpynbCellType current) {
    static const IpynbCellType types[] = { IPYNB_CELL_CODE, IPYNB_CELL_MARKDOWN, IPYNB_CELL_RAW };
    const gchar* const names[] = { _("Code"), _("Markdown"), _("Raw") };
    GtkWidget* button = gtk_menu_button_new();
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(button), FALSE);
    gtk_menu_button_set_always_show_arrow(GTK_MENU_BUTTON(button), TRUE);
    gtk_widget_set_focusable(button, FALSE);
    gtk_widget_set_tooltip_text(button, _("Cell Type"));
    GtkWidget* popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    GtkWidget* menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    for (guint i = 0; i < G_N_ELEMENTS(types); i++) {
        if (types[i] == current) gtk_menu_button_set_label(GTK_MENU_BUTTON(button), names[i]);
        GtkWidget* item = gtk_button_new_with_label(names[i]);
        gtk_widget_add_css_class(item, "flat");
        if (types[i] == current) gtk_widget_add_css_class(item, "menu-item-current");
        GtkWidget* text = gtk_button_get_child(GTK_BUTTON(item));
        if (GTK_IS_LABEL(text)) gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
        g_object_set_data(G_OBJECT(item), CELL_INDEX_KEY, GUINT_TO_POINTER(index));
        g_object_set_data(G_OBJECT(item), CELL_TYPE_KEY, GINT_TO_POINTER(types[i]));
        g_signal_connect(item, "clicked", G_CALLBACK(on_type_chosen), t);
        gtk_box_append(GTK_BOX(menu), item);
    }
    gtk_popover_set_child(GTK_POPOVER(popover), menu);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
    return button;
}

static void on_cell_run(GtkButton* button, gpointer data) {
    ipynb_run_cell(data, cell_index(button));
}

static void on_cell_up(GtkButton* button, gpointer data) {
    move_cell(data, cell_index(button), -1);
}

static void on_cell_down(GtkButton* button, gpointer data) {
    move_cell(data, cell_index(button), 1);
}

static void on_cell_add(GtkButton* button, gpointer data) {
    ipynb_tab_insert_cell(data, cell_index(button) + 1, IPYNB_CELL_CODE);
}

static void on_cell_delete(GtkButton* button, gpointer data) {
    delete_cell(data, cell_index(button));
}

static GtkWidget* cell_button(IpynbTab* t, guint index, const gchar* icon, const gchar* tooltip, GCallback cb) {
    GtkWidget* b = icon_button_new(icon, tooltip);
    g_object_set_data(G_OBJECT(b), CELL_INDEX_KEY, GUINT_TO_POINTER(index));
    g_signal_connect(b, "clicked", cb, t);
    return b;
}

/* The file name the source is highlighted as: the kernel's extension, or
 * one for its language */
static gchar* code_name(IpynbTab* t) {
    static const struct {
        const gchar* language;
        const gchar* extension;
    } KNOWN[] = { { "python", ".py" }, { "r", ".r" }, { "julia", ".jl" }, { "javascript", ".js" },
                  { "typescript", ".ts" }, { "ruby", ".rb" }, { "bash", ".sh" }, { "c++", ".cpp" },
                  { "rust", ".rs" }, { "scala", ".scala" }, { "haskell", ".hs" }, { "octave", ".m" } };
    const gchar* ext = ipynb_doc_file_extension(t->doc);
    const gchar* lang = ipynb_doc_language(t->doc);
    for (gsize i = 0; ext == NULL && lang != NULL && i < G_N_ELEMENTS(KNOWN); i++) {
        if (g_ascii_strcasecmp(lang, KNOWN[i].language) == 0) ext = KNOWN[i].extension;
    }
    if (ext == NULL) ext = ".py"; /* most notebooks */
    return g_strconcat("cell", ext[0] == '.' ? "" : ".", ext, NULL);
}

static GtkWidget* output_label(const gchar* text, gboolean error) {
    gchar* valid = g_utf8_make_valid(text, -1);
    GtkWidget* label = gtk_label_new(valid);
    g_free(valid);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_widget_set_focusable(label, FALSE);
    gtk_widget_add_css_class(label, "ipynb-output");
    if (error) gtk_widget_add_css_class(label, "ipynb-error");
    return label;
}

static GtkWidget* output_image(const IpynbOutput* o) {
    GdkTexture* texture = gdk_texture_new_from_bytes(o->image, NULL);
    if (texture == NULL) return output_label(_("(image not shown)"), FALSE);
    GtkWidget* picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_SCALE_DOWN);
    gtk_widget_set_halign(picture, GTK_ALIGN_START);
    gtk_widget_set_size_request(picture, -1, MIN(gdk_texture_get_height(texture), IMAGE_MAX_HEIGHT));
    g_object_unref(texture);
    return picture;
}

/* A formula output, scaled down to the cell's width */
static GtkWidget* output_math(IpynbTab* t, const IpynbOutput* o) {
    GdkPaintable* math = math_paintable_new(gtk_widget_get_pango_context(t->root), o->text, TRUE,
                                            fonts_size(FONT_UI), NULL);
    GtkWidget* picture = gtk_picture_new_for_paintable(math);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_SCALE_DOWN);
    gtk_widget_set_halign(picture, GTK_ALIGN_START);
    gtk_widget_set_size_request(picture, -1, gdk_paintable_get_intrinsic_height(math));
    gtk_widget_set_tooltip_text(picture, o->text);
    g_object_unref(math);
    return picture;
}

/* Fill box (emptied first) with the outputs of the code cell at index;
 * hidden without outputs */
static void fill_outputs(IpynbTab* t, guint index, GtkWidget* box) {
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(box)) != NULL) {
        gtk_box_remove(GTK_BOX(box), child);
    }
    GPtrArray* outputs = ipynb_doc_cell_outputs(t->doc, index, MAX_OUTPUT_LINES);
    gint count = ipynb_doc_cell_execution_count(t->doc, index);
    for (guint i = 0; i < outputs->len; i++) {
        const IpynbOutput* o = g_ptr_array_index(outputs, i);
        if (o->result) {
            gchar* prompt = count >= 0 ? g_strdup_printf("Out [%d]:", count) : g_strdup("Out:");
            GtkWidget* label = gtk_label_new(prompt);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
            gtk_widget_add_css_class(label, "ipynb-out-prompt");
            gtk_box_append(GTK_BOX(box), label);
            g_free(prompt);
        }
        GtkWidget* shown = o->kind == IPYNB_OUTPUT_IMAGE  ? output_image(o)
                           : o->kind == IPYNB_OUTPUT_MATH ? output_math(t, o)
                                                          : output_label(o->text, o->kind == IPYNB_OUTPUT_ERROR);
        gtk_box_append(GTK_BOX(box), shown);
    }
    gtk_widget_set_visible(box, outputs->len > 0);
    g_ptr_array_unref(outputs);
}

/* The In [n] prompt of the code cell at index: [*] while it runs or waits */
static void set_prompt(IpynbTab* t, guint index, GtkWidget* label) {
    gchar* prompt;
    gint count = ipynb_doc_cell_execution_count(t->doc, index);
    if (g_hash_table_contains(t->running, GUINT_TO_POINTER(ipynb_doc_cell_key(t->doc, index)))) {
        prompt = g_strdup("In [*]:");
    } else if (count >= 0) {
        prompt = g_strdup_printf("In [%d]:", count);
    } else {
        prompt = g_strdup("In [ ]:");
    }
    gtk_label_set_text(GTK_LABEL(label), prompt);
    g_free(prompt);
}

/* Show the prompt and outputs of the cell at index again (after a run),
 * leaving its source and the other cells as they are */
void ipynb_tab_refresh_cell(IpynbTab* t, guint index) {
    if (index >= t->views->len) return;
    GtkWidget* view = g_ptr_array_index(t->views, index);
    GtkWidget* prompt = g_object_get_data(G_OBJECT(view), CELL_PROMPT_KEY);
    GtkWidget* outputs = g_object_get_data(G_OBJECT(view), CELL_OUTPUTS_KEY);
    if (prompt != NULL) set_prompt(t, index, prompt);
    if (outputs != NULL) fill_outputs(t, index, outputs);
}

static GtkWidget* build_cell(IpynbTab* t, guint index) {
    IpynbCellType type = ipynb_doc_cell_type(t->doc, index);
    GtkWidget* cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(cell, "ipynb-cell");

    /* Head: prompt, type, actions */
    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(head, "ipynb-cell-head");
    GtkWidget* prompt = NULL;
    if (type == IPYNB_CELL_CODE) {
        prompt = gtk_label_new(NULL);
        set_prompt(t, index, prompt);
        gtk_box_append(GTK_BOX(head), prompt);
    }
    gtk_box_append(GTK_BOX(head), type_menu_new(t, index, type));
    GtkWidget* spacer = gtk_label_new(NULL);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(head), spacer);
    if (type == IPYNB_CELL_CODE) {
        gtk_box_append(GTK_BOX(head), cell_button(t, index, "go-next", _("Run Cell (Ctrl+Enter)"), G_CALLBACK(on_cell_run)));
    }
    gtk_box_append(GTK_BOX(head), cell_button(t, index, "pan-up", _("Move Cell Up (Alt+Up)"), G_CALLBACK(on_cell_up)));
    gtk_box_append(GTK_BOX(head), cell_button(t, index, "pan-down", _("Move Cell Down (Alt+Down)"), G_CALLBACK(on_cell_down)));
    gtk_box_append(GTK_BOX(head),
                   cell_button(t, index, "list-add", _("New Code Cell Below (Alt+Enter)"), G_CALLBACK(on_cell_add)));
    gtk_box_append(GTK_BOX(head),
                   cell_button(t, index, "edit-delete", _("Delete Cell (Alt+Delete)"), G_CALLBACK(on_cell_delete)));
    gtk_box_append(GTK_BOX(cell), head);

    /* Source */
#ifdef HAVE_GSV
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(gtk_source_buffer_new(NULL));
    GtkWidget* view = gtk_source_view_new_with_buffer(GTK_SOURCE_BUFFER(buffer));
    apply_vscode_scheme(buffer, view);
#else
    GtkTextBuffer* buffer = gtk_text_buffer_new(NULL);
    GtkWidget* view = gtk_text_view_new_with_buffer(buffer);
#endif
    g_object_unref(buffer); /* the view keeps it */
    if (type != IPYNB_CELL_RAW) {
        gchar* name = type == IPYNB_CELL_CODE ? code_name(t) : g_strdup("cell.md");
        apply_buffer_language(buffer, name, FALSE);
        g_free(name);
    }
    gchar* source = ipynb_doc_cell_markdown(t->doc, index);
    gchar* valid = g_utf8_make_valid(source, -1);
    t->building = TRUE;
    gtk_text_buffer_set_text(buffer, valid, -1);
    t->building = FALSE;
    g_free(valid);
    g_free(source);
    gtk_text_buffer_set_modified(buffer, FALSE);
    g_object_set_data(G_OBJECT(buffer), CELL_INDEX_KEY, GUINT_TO_POINTER(index));
    g_signal_connect(buffer, "changed", G_CALLBACK(on_source_changed), t);

    gtk_widget_add_css_class(view, "ipynb-source");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 6);
    set_view_monospace(view, TRUE);
    g_object_set_data(G_OBJECT(view), CELL_INDEX_KEY, GUINT_TO_POINTER(index));
    track_focus(t, view);
    gtk_box_append(GTK_BOX(cell), view);
    g_ptr_array_add(t->views, view);

    if (type == IPYNB_CELL_MARKDOWN) {
        GtkWidget* rendered = rendered_view_new(t, index);
        gtk_box_append(GTK_BOX(cell), gtk_widget_get_parent(rendered));
        g_object_set_data(G_OBJECT(view), CELL_RENDERED_KEY, rendered);
        if (!g_hash_table_contains(t->editing, GUINT_TO_POINTER(ipynb_doc_cell_key(t->doc, index)))) {
            show_markdown(t, index, TRUE);
        }
    }

    if (type == IPYNB_CELL_CODE) {
        GtkWidget* outputs = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(outputs, "ipynb-outputs");
        fill_outputs(t, index, outputs);
        gtk_box_append(GTK_BOX(cell), outputs);
        g_object_set_data(G_OBJECT(view), CELL_PROMPT_KEY, prompt);
        g_object_set_data(G_OBJECT(view), CELL_OUTPUTS_KEY, outputs);
    }
    return cell;
}

static void rebuild(IpynbTab* t) {
    if (t->rebuild_source != 0) {
        g_source_remove(t->rebuild_source);
        t->rebuild_source = 0;
    }
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(t->cells_box)) != NULL) {
        gtk_box_remove(GTK_BOX(t->cells_box), child);
    }
    g_ptr_array_set_size(t->views, 0);
    for (guint i = 0; i < ipynb_doc_n_cells(t->doc); i++) {
        gtk_box_append(GTK_BOX(t->cells_box), build_cell(t, i));
    }
    ipynb_tab_update_info(t);
    guint focus = MIN(t->focus_after, t->views->len > 0 ? t->views->len - 1 : 0);
    set_current(t, focus);
    if (tab_shown(t) && gtk_widget_get_mapped(t->root)) ipynb_tab_focus_cell(t, focus);
}

/* --- Bar --- */

static void on_add_code(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    ipynb_tab_insert_cell(t, t->current + 1, IPYNB_CELL_CODE);
}

static void on_add_markdown(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    ipynb_tab_insert_cell(t, t->current + 1, IPYNB_CELL_MARKDOWN);
}

static void on_clear_outputs(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    if (!ipynb_doc_clear_outputs(t->doc, G_MAXUINT)) return;
    ipynb_tab_mark_changed(t);
    queue_rebuild(t, t->current);
}

static void on_edit_json(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    save_now(t);
    editor_open_text_file(t->app, t->name, t->path);
}

static void on_tagbar(GtkButton* button, gpointer data) {
    (void)button;
    ipynb_tagbar_toggle(data);
}

static GtkWidget* bar_button(IpynbTab* t, const gchar* icon, const gchar* text, const gchar* tooltip, GCallback cb) {
    GtkWidget* b = icon_button_new(icon, tooltip);
    icon_button_set_content(b, icon, text);
    g_signal_connect(b, "clicked", cb, t);
    return b;
}

/* --- Keys --- */

static gboolean on_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                       gpointer data) {
    (void)keycode;
    IpynbTab* t = data;
    if (t->closed) return FALSE;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK | GDK_SUPER_MASK);
    /* Ctrl+K Ctrl+O: tagbar, as in the editor tabs (from the tagbar too);
     * another key after Ctrl+K does what it does alone */
    GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(ctrl));
    if (t->chord_k && event != NULL && gdk_key_event_is_modifier(event)) return FALSE;
    if (t->chord_k) {
        t->chord_k = FALSE;
        if (mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_o || keyval == GDK_KEY_O)) {
            ipynb_tagbar_toggle(t);
            return TRUE;
        }
    } else if (mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
        t->chord_k = TRUE;
        return TRUE;
    }
    /* The tagbar takes its other keys (Alt+Up would move a cell) */
    if (ipynb_tagbar_has_focus(t)) return FALSE;
    if (mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_Up || keyval == GDK_KEY_Down)) {
        if (keyval == GDK_KEY_Up && t->current > 0) ipynb_tab_focus_cell(t, t->current - 1);
        if (keyval == GDK_KEY_Down) ipynb_tab_focus_cell(t, t->current + 1);
        return TRUE;
    }
    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) &&
        (mods == GDK_SHIFT_MASK || mods == GDK_CONTROL_MASK)) {
        if (mods == GDK_SHIFT_MASK) {
            ipynb_run_advance(t);
        } else {
            ipynb_run_cell(t, t->current);
        }
        return TRUE;
    }
    if (mods != GDK_ALT_MASK) return FALSE;
    switch (keyval) {
    case GDK_KEY_Up:
        move_cell(t, t->current, -1);
        return TRUE;
    case GDK_KEY_Down:
        move_cell(t, t->current, 1);
        return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        ipynb_tab_insert_cell(t, t->current + 1, IPYNB_CELL_CODE);
        return TRUE;
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
        delete_cell(t, t->current);
        return TRUE;
    default:
        return FALSE;
    }
}

/* --- The tab --- */

/* Take the tab out of its notebook, or of the hidden workspace keeping it */
static void close_tab(IpynbTab* t) {
    AppData* app = t->app;
    ipynb_run_stop(t);
    ipynb_tagbar_release(t);
    save_now(t);
    t->closed = TRUE;
    GtkWidget* root = g_object_ref(t->root);
    if (app->ipynb_pages != NULL) g_ptr_array_remove(app->ipynb_pages, root);
    GtkNotebook* nb = notebook_of(root);
    if (nb != NULL && tab_shown(t)) {
        editor_remove_page(app, root);
    } else if (nb != NULL) {
        /* In the editor panel of a hidden workspace: a notebook left empty
         * goes when the workspace shows again */
        gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, root));
    } else {
        for (gint i = 0; i < app->num_workspaces; i++) {
            GPtrArray* kept = app->workspaces[i].ipynb_pages;
            if (kept != NULL) g_ptr_array_remove(kept, root);
        }
    }
    g_object_unref(root);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    IpynbTab* t = data;
    if (!t->closed) close_tab(t);
}

static void on_menu_close(GtkWidget* child, gpointer data) {
    (void)data;
    IpynbTab* t = tab_of(child);
    if (t != NULL && !t->closed) close_tab(t);
}

/* The tab menu, as for an editor tab: move, close */
static void on_label_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    IpynbTab* t = data;
    if (!t->closed) editor_page_show_context_menu(t->app, t->root, t->tab_label, on_menu_close, NULL);
}

static void on_root_destroy(GtkWidget* widget, gpointer data) {
    IpynbTab* t = data;
    ipynb_run_stop(t);
    ipynb_tagbar_release(t);
    save_now(t); /* destroyed with its window or workspace: the last changes */
    t->closed = TRUE;
    if (t->rebuild_source != 0) {
        g_source_remove(t->rebuild_source);
        t->rebuild_source = 0;
    }
    if (t->app->ipynb_pages != NULL) g_ptr_array_remove(t->app->ipynb_pages, widget);
}

static void tab_free(gpointer data) {
    IpynbTab* t = data;
    if (t->save_source != 0) g_source_remove(t->save_source);
    if (t->rebuild_source != 0) g_source_remove(t->rebuild_source);
    g_clear_object(&t->tab_label);
    if (t->deleted != NULL) ipynb_cell_free(t->deleted);
    ipynb_doc_free(t->doc);
    g_ptr_array_unref(t->views);
    g_hash_table_unref(t->running);
    g_hash_table_unref(t->editing);
    if (t->stale != NULL) g_hash_table_unref(t->stale);
    g_free(t->path);
    g_free(t->name);
    g_free(t);
}

static GtkWidget* tab_label_new(IpynbTab* t) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* image = file_type_image_new();
    file_type_image_set(image, t->name, FALSE);
    gtk_box_append(GTK_BOX(box), image);
    GtkWidget* text = gtk_label_new(t->name);
    gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(text), TAB_MAX_CHARS);
    /* As wide as the name (up to TAB_MAX_CHARS) even when tabs are crowded */
    gtk_label_set_width_chars(GTK_LABEL(text), (gint)MIN(g_utf8_strlen(t->name, -1), TAB_MAX_CHARS));
    gtk_box_append(GTK_BOX(box), text);
    gtk_widget_set_tooltip_text(box, t->path);

    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), t);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(mclick));
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_label_right_click), t);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(rclick));
    return g_object_ref_sink(box);
}


/* A new tab editing doc (taken), in no notebook yet */
static IpynbTab* tab_new(AppData* app, const gchar* name, const gchar* path, IpynbDoc* doc) {
    ensure_css();
    IpynbTab* t = g_new0(IpynbTab, 1);
    t->app = app;
    t->path = g_strdup(path);
    t->name = g_strdup(name);
    t->doc = doc;
    t->views = g_ptr_array_new();
    t->running = g_hash_table_new(NULL, NULL);
    t->editing = g_hash_table_new(NULL, NULL);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "ipynb-tab");
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    t->root = root;

    /* Bar (top): new cells, clear outputs, JSON; kernel, save state */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(bar, "ipynb-bar");
    GtkWidget* kernel_button = ipynb_run_bar(t, bar);
    gtk_box_append(GTK_BOX(bar), bar_button(t, "list-add", _("Code"), _("New Code Cell Below (Alt+Enter)"),
                                            G_CALLBACK(on_add_code)));
    gtk_box_append(GTK_BOX(bar), bar_button(t, "list-add", _("Markdown"), _("New Markdown Cell Below"),
                                            G_CALLBACK(on_add_markdown)));
    gtk_box_append(GTK_BOX(bar), bar_button(t, "edit-clear", _("Clear Outputs"), _("Clear the Outputs of Every Cell"),
                                            G_CALLBACK(on_clear_outputs)));
    gtk_box_append(GTK_BOX(bar), bar_button(t, "text-x-generic", _("Edit as JSON"),
                                            _("Open the File as Text in an Editor Tab"), G_CALLBACK(on_edit_json)));
    t->undo_button = bar_button(t, "edit-delete", _("Undo Delete"), _("Put Back the Cell Deleted Last"),
                                G_CALLBACK(on_undo_delete));
    gtk_widget_set_visible(t->undo_button, FALSE);
    gtk_box_append(GTK_BOX(bar), t->undo_button);
    t->info = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(t->info), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(t->info), 1.0f);
    gtk_widget_set_hexpand(t->info, TRUE);
    gtk_widget_set_tooltip_text(t->info, _("Shift+Enter: run the cell and go to the next; Ctrl+Enter: run it; "
                                           "Double-click or Enter: edit a rendered Markdown cell; "
                                           "Ctrl+Up / Ctrl+Down: previous / next cell; Alt+Up / Alt+Down: move the "
                                           "cell; Alt+Enter: new code cell; Alt+Delete: delete the cell"));
    gtk_box_append(GTK_BOX(bar), t->info);
    gtk_box_append(GTK_BOX(bar), kernel_button);
    GtkWidget* tagbar = icon_button_new("view-list-bullet", _("Show / Hide the Headings (Ctrl+K Ctrl+O)"));
    g_signal_connect(tagbar, "clicked", G_CALLBACK(on_tagbar), t);
    gtk_box_append(GTK_BOX(bar), tagbar);
    t->status = gtk_label_new(NULL);
    gtk_widget_set_margin_start(t->status, 8);
    gtk_box_append(GTK_BOX(bar), t->status);
    /* In a narrow pane the bar is cut on the right instead of widening the
     * tab past the pane (the tagbar would be out of sight) */
    GtkWidget* bar_clip = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(bar_clip), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(bar_clip), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(bar_clip), bar);
    gtk_box_append(GTK_BOX(root), bar_clip);
    gtk_box_append(GTK_BOX(root), ipynb_run_input_row_new(t));

    t->cells_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(t->cells_box, "ipynb-cells");
    GtkWidget* scroll = gtk_scrolled_window_new();
    /* Cells wider than a narrow pane scroll: the tagbar keeps its place */
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), t->cells_box);
    gtk_widget_set_hexpand(scroll, TRUE);
    t->scroll = scroll;
    GtkWidget* paned = ipynb_tagbar_wrap(t, scroll);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(root), paned);

    /* Capture: the text views would take Ctrl+Up and the Alt keys first */
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), t);
    gtk_widget_add_controller(root, keys);

    t->tab_label = tab_label_new(t);
    g_object_set_data_full(G_OBJECT(root), IPYNB_TAB_KEY, t, tab_free);
    g_signal_connect(root, "destroy", G_CALLBACK(on_root_destroy), t);
    if (app->ipynb_pages == NULL) app->ipynb_pages = g_ptr_array_new();
    g_ptr_array_add(app->ipynb_pages, root);
    editor_page_set_file(root, path);

    t->focus_after = 0;
    rebuild(t);
    /* Shown as in a new editor tab (Preferences → Editor) */
    ipynb_tagbar_set_shown(t, app->settings.tagbar);
    ipynb_tab_set_status(t, _("Saved"), FALSE);
    return t;
}

/* The notebook at path, or NULL when the tab cannot edit it (too large,
 * not a notebook, nbformat 3); an empty file is a new notebook */
static IpynbDoc* load(const gchar* path) {
    GStatBuf st;
    if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > MAX_NOTEBOOK_BYTES) return NULL;
    gchar* text = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &text, &len, NULL)) return NULL;
    gsize blank = 0;
    while (blank < len && g_ascii_isspace(text[blank])) blank++;
    IpynbDoc* doc = blank == len ? ipynb_doc_new() : ipynb_doc_parse(text, len, NULL);
    g_free(text);
    return doc;
}

gboolean ipynb_tab_open(AppData* app, const gchar* name, const gchar* full, gboolean dedicated) {
    if (app == NULL || full == NULL || app->notebook == NULL) return FALSE;
    gchar* lower = g_ascii_strdown(full, -1);
    gboolean notebook = g_str_has_suffix(lower, ".ipynb");
    g_free(lower);
    if (!notebook) return FALSE;

    IpynbTab* t = find_tab(app, full);
    if (t != NULL && !tab_shown(t)) {
        /* Open in another workspace: it moves to this one */
        close_tab(t);
        t = NULL;
    }
    if (t == NULL) {
        IpynbDoc* doc = load(full);
        if (doc == NULL) return FALSE; /* edited as text */
        gchar* base = name != NULL ? g_strdup(name) : g_path_get_basename(full);
        t = tab_new(app, base, full, doc);
        g_free(base);
        if (dedicated) {
            editor_add_page(app, t->root, t->tab_label);
        } else {
            editor_add_page_like_file(app, t->root, t->tab_label);
        }
    }
    GtkNotebook* nb = notebook_of(t->root);
    if (nb != NULL) gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, t->root));
    ipynb_tab_focus_cell(t, t->current);
    return TRUE;
}

gboolean ipynb_tab_save_focused(AppData* app) {
    IpynbTab* t = app != NULL ? focused_tab(app) : NULL;
    if (t == NULL || t->closed) return FALSE;
    if (!t->dirty) {
        /* Nothing new: written again all the same, as editors do */
        t->dirty = TRUE;
    }
    save_now(t);
    return TRUE;
}

void ipynb_tab_save_all(AppData* app) {
    for (guint i = 0; app != NULL && app->ipynb_pages != NULL && i < app->ipynb_pages->len; i++) {
        IpynbTab* t = tab_of(g_ptr_array_index(app->ipynb_pages, i));
        if (t != NULL && !t->closed) save_now(t);
    }
}

gboolean ipynb_tab_close_shown(AppData* app) {
    if (app == NULL) return FALSE;
    IpynbTab* t = focused_tab(app);
    if (t == NULL && app->notebook != NULL) {
        /* Else the current page of the preview notebook */
        GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
        gint cur = gtk_notebook_get_current_page(nb);
        t = cur >= 0 ? tab_of(gtk_notebook_get_nth_page(nb, cur)) : NULL;
        if (t != NULL && !gtk_widget_get_mapped(t->root)) t = NULL;
    }
    if (t == NULL || t->closed) return FALSE;
    close_tab(t);
    return TRUE;
}

void ipynb_tab_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (gint i = gtk_notebook_get_n_pages(nb) - 1; i >= 0; i--) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        if (tab_of(page) == NULL) continue;
        if (ws->ipynb_pages == NULL) ws->ipynb_pages = g_ptr_array_new_with_free_func(g_object_unref);
        g_ptr_array_insert(ws->ipynb_pages, 0, g_object_ref(page));
        gtk_notebook_remove_page(nb, i);
    }
}

void ipynb_tab_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->ipynb_pages == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (guint i = 0; i < ws->ipynb_pages->len; i++) {
        GtkWidget* root = g_ptr_array_index(ws->ipynb_pages, i);
        IpynbTab* t = tab_of(root);
        if (t == NULL) continue;
        gtk_notebook_append_page(nb, root, t->tab_label);
        gtk_notebook_set_tab_reorderable(nb, root, TRUE);
        gtk_notebook_set_tab_detachable(nb, root, TRUE);
    }
    g_clear_pointer(&ws->ipynb_pages, g_ptr_array_unref);
}

void ipynb_tab_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    /* Its tabs close with it: every change saved first */
    ipynb_tab_save_all(app);
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        ipynb_tab_workspace_hidden(app, ws);
    }
    g_clear_pointer(&ws->ipynb_pages, g_ptr_array_unref);
}

void ipynb_tab_set_tagbar_shown(AppData* app, gboolean shown) {
    for (guint i = 0; app != NULL && app->ipynb_pages != NULL && i < app->ipynb_pages->len; i++) {
        IpynbTab* t = tab_of(g_ptr_array_index(app->ipynb_pages, i));
        if (t != NULL && !t->closed) ipynb_tagbar_set_shown(t, shown);
    }
}

GtkWidget* ipynb_tab_tagbar_list(GtkWidget* page) {
    IpynbTab* t = page != NULL ? tab_of(page) : NULL;
    return t != NULL && !t->closed ? ipynb_tagbar_list(t) : NULL;
}
