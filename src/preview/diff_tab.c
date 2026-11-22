/* Side-by-side and unified Diff tabs (see diff_tab.h). */
#include "preview/diff_tab.h"
#include "preview/diff_tab_render.h"
#include "formats/side_diff.h"
#include "core/app_events.h"
#include "core/file_types.h"
#include "core/i18n.h"
#include "core/text_encoding.h"
#include "core/vfs.h"
#include "editor/editor.h"
#include "git/git_utils.h"
#include "theme/icons.h"
#include "theme/palette.h"
#include "theme/text_style.h"
#include "widgets/focus_route.h"
#include "widgets/panels.h"

#include <string.h>

#define DIFF_TAB_KEY       "diff-tab"
#define DIFF_MAX_BYTES     (4 * 1024 * 1024)
#define DIFF_TAB_MAX_CHARS 24

typedef enum {
    DIFF_SRC_FILES = 0,
    DIFF_SRC_HEAD,
    DIFF_SRC_TEXTS
} DiffSourceKind;

typedef struct DiffTab DiffTab;

struct DiffTab {
    AppData* app;
    GtkWidget* root;
    GtkWidget* tab_label;
    GtkWidget* stats_label;
    GtkWidget* hunk_label;
    GtkWidget* prev_btn;
    GtkWidget* next_btn;
    GtkWidget* split_btn;
    GtkWidget* unified_btn;
    GtkWidget* swap_btn;
    GtkWidget* refresh_btn;
    GtkWidget* edit_btn;
    GtkWidget* stack;

    /* Split view */
    GtkWidget* paned;
    GtkWidget* left_hdr;
    GtkWidget* right_hdr;
    GtkWidget* left_scroll;
    GtkWidget* right_scroll;
    GtkWidget* left_view;
    GtkWidget* right_view;
    GtkTextBuffer* left_buf;
    GtkTextBuffer* right_buf;
    DiffGutterPane left_gutter;
    DiffGutterPane right_gutter;
    gboolean syncing_scroll;

    /* Unified view */
    GtkWidget* unified_scroll;
    GtkWidget* unified_view;
    GtkTextBuffer* unified_buf;
    GArray* unified_hunks;      /* guint line indices in unified_buf */
    GArray* unified_src_lines;  /* gint 1-based right (else left) source lines */
    GArray* unified_src_right;  /* gboolean: TRUE if right side, FALSE if left */

    /* Source configuration */
    DiffSourceKind src_kind;
    gchar* title;
    gchar* left_label;
    gchar* right_label;
    gchar* left_path;
    gchar* right_path;
    gchar* left_text_init;
    gchar* right_text_init;
    gchar* lang_hint;
    gboolean swapped;

    /* Computed diff & state */
    SideDiffDoc* doc;
    gint current_hunk; /* 0-based index into doc->hunks, or -1 */
    GCancellable* cancellable;
    gboolean closed;
    guint pending_key; /* '[' or ']' for Vim [c / ]c */
    NavPrefix nav_prefix;
};

/* Selected file for "Select for Compare" (one per application) */
static gchar* compare_selected_path = NULL;

void diff_tab_select_for_compare(AppData* app, const gchar* path) {
    g_free(compare_selected_path);
    compare_selected_path = (path != NULL && path[0] != '\0') ? g_strdup(path) : NULL;
    if (app != NULL && compare_selected_path != NULL) {
        gchar* base = g_path_get_basename(compare_selected_path);
        gchar* msg = g_strdup_printf(_("Selected “%s” for compare"), base);
        app_notice(app, msg, FALSE);
        g_free(msg);
        g_free(base);
    }
}

const gchar* diff_tab_selected_for_compare(AppData* app) {
    (void)app;
    return compare_selected_path;
}

void diff_tab_free(AppData* app) {
    (void)app;
    g_clear_pointer(&compare_selected_path, g_free);
}

static DiffTab* tab_of(GtkWidget* page) {
    return page != NULL ? g_object_get_data(G_OBJECT(page), DIFF_TAB_KEY) : NULL;
}

static GtkNotebook* notebook_of(GtkWidget* page) {
    GtkWidget* p = gtk_widget_get_parent(page);
    return GTK_IS_NOTEBOOK(p) ? GTK_NOTEBOOK(p) : NULL;
}

static void close_tab(DiffTab* dt) {
    if (dt == NULL || dt->closed) return;
    dt->closed = TRUE;
    if (dt->cancellable != NULL) {
        g_cancellable_cancel(dt->cancellable);
    }
    AppData* app = dt->app;
    GtkWidget* root = dt->root;
    GtkWidget* focus_back = focus_route_return_target(app, root);
    g_object_ref(root);
    editor_remove_page(app, root);
    focus_route_return(app, focus_back, root);
    g_object_unref(root);
}

static void on_close_page(GtkWidget* child, gpointer data) {
    (void)data;
    close_tab(tab_of(child));
}

static void on_tab_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press; (void)x; (void)y;
    DiffTab* dt = data;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    editor_page_show_context_menu(dt->app, dt->root, dt->tab_label, on_close_page, NULL);
}

static void on_tab_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press; (void)x; (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_tab((DiffTab*)data);
}

static void on_root_destroy(GtkWidget* widget, gpointer data) {
    DiffTab* dt = data;
    if (dt == NULL) return;
    if (dt->cancellable != NULL) {
        g_cancellable_cancel(dt->cancellable);
    }
    if (dt->app != NULL && dt->app->diff_pages != NULL) {
        g_ptr_array_remove(dt->app->diff_pages, widget);
    }
}

static void tab_free(gpointer data) {
    DiffTab* dt = data;
    if (dt == NULL) return;
    if (dt->cancellable != NULL) {
        g_cancellable_cancel(dt->cancellable);
        g_clear_object(&dt->cancellable);
    }
    g_clear_pointer(&dt->doc, side_diff_free);
    g_clear_pointer(&dt->unified_hunks, g_array_unref);
    g_clear_pointer(&dt->unified_src_lines, g_array_unref);
    g_clear_pointer(&dt->unified_src_right, g_array_unref);
    g_clear_object(&dt->tab_label);
    g_free(dt->title);
    g_free(dt->left_label);
    g_free(dt->right_label);
    g_free(dt->left_path);
    g_free(dt->right_path);
    g_free(dt->left_text_init);
    g_free(dt->right_text_init);
    g_free(dt->lang_hint);
    g_free(dt);
}

/* ------------------------------------------------------------------------- */
/* Synchronized scrolling                                                    */
/* ------------------------------------------------------------------------- */

static void sync_adj(GtkAdjustment* from, GtkAdjustment* to, DiffTab* dt) {
    if (dt->syncing_scroll) return;
    dt->syncing_scroll = TRUE;
    gdouble val = gtk_adjustment_get_value(from);
    gdouble lower = gtk_adjustment_get_lower(to);
    gdouble max_v = MAX(lower, gtk_adjustment_get_upper(to) - gtk_adjustment_get_page_size(to));
    gtk_adjustment_set_value(to, CLAMP(val, lower, max_v));
    dt->syncing_scroll = FALSE;
}

static void on_left_vscroll(GtkAdjustment* adj, gpointer data) {
    DiffTab* dt = data;
    sync_adj(adj, gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(dt->right_scroll)), dt);
}

static void on_right_vscroll(GtkAdjustment* adj, gpointer data) {
    DiffTab* dt = data;
    sync_adj(adj, gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(dt->left_scroll)), dt);
}

static void on_left_hscroll(GtkAdjustment* adj, gpointer data) {
    DiffTab* dt = data;
    sync_adj(adj, gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(dt->right_scroll)), dt);
}

static void on_right_hscroll(GtkAdjustment* adj, gpointer data) {
    DiffTab* dt = data;
    sync_adj(adj, gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(dt->left_scroll)), dt);
}

static gboolean is_unified_mode(DiffTab* dt) {
    return g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(dt->stack)), "unified") == 0;
}

static void update_hunk_ui(DiffTab* dt) {
    guint total = is_unified_mode(dt)
        ? (dt->unified_hunks != NULL ? dt->unified_hunks->len : 0)
        : (dt->doc != NULL ? dt->doc->hunks->len : 0);
    gtk_widget_set_sensitive(dt->prev_btn, total > 0);
    gtk_widget_set_sensitive(dt->next_btn, total > 0);
    if (total == 0) {
        gtk_label_set_text(GTK_LABEL(dt->hunk_label), "");
    } else if (dt->current_hunk >= 0 && (guint)dt->current_hunk < total) {
        gchar* s = g_strdup_printf("%d/%u", dt->current_hunk + 1, total);
        gtk_label_set_text(GTK_LABEL(dt->hunk_label), s);
        g_free(s);
    } else {
        gchar* s = g_strdup_printf("0/%u", total);
        gtk_label_set_text(GTK_LABEL(dt->hunk_label), s);
        g_free(s);
    }
}

static void jump_to_hunk(DiffTab* dt, gint idx) {
    gboolean unified = is_unified_mode(dt);
    GArray* hunks = unified ? dt->unified_hunks : (dt->doc != NULL ? dt->doc->hunks : NULL);
    if (hunks == NULL || hunks->len == 0) return;
    gint n = (gint)hunks->len;
    if (idx < 0) idx = n - 1;
    if (idx >= n) idx = 0;
    dt->current_hunk = idx;

    guint row = g_array_index(hunks, guint, (guint)idx);
    GtkTextBuffer* buf = unified ? dt->unified_buf : dt->right_buf;
    GtkWidget* view = unified ? dt->unified_view : dt->right_view;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(buf, &it, (gint)row);
    gtk_text_buffer_place_cursor(buf, &it);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view), &it, 0.0, TRUE, 0.0, 0.25);
    if (!unified) {
        GtkTextIter lit;
        gtk_text_buffer_get_iter_at_line(dt->left_buf, &lit, (gint)row);
        gtk_text_buffer_place_cursor(dt->left_buf, &lit);
    }
    update_hunk_ui(dt);
}

static void step_hunk(DiffTab* dt, gint dir) {
    gboolean unified = is_unified_mode(dt);
    GArray* hunks = unified ? dt->unified_hunks : (dt->doc != NULL ? dt->doc->hunks : NULL);
    if (hunks == NULL || hunks->len == 0) return;
    gint next = (dt->current_hunk < 0) ? (dir > 0 ? 0 : (gint)hunks->len - 1)
                                       : dt->current_hunk + dir;
    jump_to_hunk(dt, next);
}

static void update_headers(DiffTab* dt) {
    const gchar* l = dt->swapped ? dt->right_label : dt->left_label;
    const gchar* r = dt->swapped ? dt->left_label : dt->right_label;
    gtk_label_set_text(GTK_LABEL(dt->left_hdr), l != NULL ? l : "");
    gtk_label_set_text(GTK_LABEL(dt->right_hdr), r != NULL ? r : "");

    const gchar* edit_target = dt->swapped
        ? (dt->left_path != NULL ? dt->left_path : dt->right_path)
        : (dt->right_path != NULL ? dt->right_path : dt->left_path);
    gtk_widget_set_visible(dt->edit_btn, edit_target != NULL);
    gtk_widget_set_visible(dt->refresh_btn, dt->src_kind != DIFF_SRC_TEXTS);
}

static void apply_diff_result(DiffTab* dt, SideDiffDoc* doc, const gchar* error_msg) {
    g_clear_pointer(&dt->doc, side_diff_free);
    dt->current_hunk = -1;
    update_headers(dt);

    if (error_msg != NULL) {
        gtk_label_set_text(GTK_LABEL(dt->stats_label), error_msg);
        gtk_text_buffer_set_text(dt->left_buf, error_msg, -1);
        gtk_text_buffer_set_text(dt->right_buf, error_msg, -1);
        gtk_text_buffer_set_text(dt->unified_buf, error_msg, -1);
        g_array_set_size(dt->unified_hunks, 0);
        update_hunk_ui(dt);
        return;
    }

    dt->doc = doc;
    if (doc->hunks->len == 0) {
        gtk_label_set_text(GTK_LABEL(dt->stats_label), _("Files are identical"));
    } else {
        gchar* markup = g_markup_printf_escaped(
            "<span foreground=\"%s\">+%u</span>  <span foreground=\"%s\">-%u</span>  "
            "<span foreground=\"%s\">· %u %s%s</span>",
            palette_color(PAL_DIFF_ADDED_FG), doc->added,
            palette_color(PAL_DIFF_REMOVED_FG), doc->removed,
            palette_color(PAL_FG_SUBTLE), doc->hunks->len,
            doc->hunks->len == 1 ? _("hunk") : _("hunks"),
            doc->truncated ? _(" (truncated)") : "");
        gtk_label_set_markup(GTK_LABEL(dt->stats_label), markup);
        g_free(markup);
    }

    if (dt->lang_hint != NULL) {
        apply_buffer_language(dt->left_buf, dt->lang_hint, FALSE);
        apply_buffer_language(dt->right_buf, dt->lang_hint, FALSE);
    }
    diff_render_split(dt->doc, dt->left_buf, dt->right_buf, &dt->left_gutter, &dt->right_gutter);
    diff_render_unified(dt->doc, dt->unified_buf, dt->unified_hunks, dt->unified_src_lines,
                        dt->unified_src_right);
    update_hunk_ui(dt);
    if (doc->hunks->len > 0) {
        jump_to_hunk(dt, 0);
    }
}

/* ------------------------------------------------------------------------- */
/* Worker task: read files / HEAD blob and compute SideDiffDoc               */
/* ------------------------------------------------------------------------- */

typedef struct {
    DiffTab* dt;
    DiffSourceKind src_kind;
    gchar* left_path;
    gchar* right_path;
    gchar* left_text;
    gchar* right_text;
    gboolean swapped;
    SideDiffDoc* doc;
    gchar* error;
} DiffTaskData;

static void diff_task_data_free(gpointer data) {
    DiffTaskData* td = data;
    if (td == NULL) return;
    g_free(td->left_path);
    g_free(td->right_path);
    g_free(td->left_text);
    g_free(td->right_text);
    g_clear_pointer(&td->doc, side_diff_free);
    g_free(td->error);
    g_free(td);
}

/* The bytes of path, NUL-terminated (over SFTP for a remote machine's
 * file); NULL when it cannot be read or is larger than DIFF_MAX_BYTES
 * (*too_large then) */
static gchar* load_bytes(const gchar* path, gsize* len, gboolean* too_large) {
    *too_large = FALSE;
    if (vfs_is_remote(path)) {
        guint64 size = 0;
        GBytes* bytes = vfs_read(path, DIFF_MAX_BYTES + 1, &size, NULL, NULL);
        if (bytes == NULL) return NULL;
        gsize n = g_bytes_get_size(bytes);
        *too_large = size > DIFF_MAX_BYTES || n > DIFF_MAX_BYTES;
        gchar* raw = NULL;
        if (!*too_large) {
            raw = g_malloc(n + 1);
            memcpy(raw, g_bytes_get_data(bytes, NULL), n);
            raw[n] = '\0';
        }
        g_bytes_unref(bytes);
        *len = n;
        return raw;
    }
    GStatBuf st;
    if (g_stat(path, &st) != 0) return NULL;
    *too_large = st.st_size > DIFF_MAX_BYTES;
    gchar* raw = NULL;
    if (*too_large || !g_file_get_contents(path, &raw, len, NULL)) return NULL;
    return raw;
}

static gchar* read_text_file(const gchar* path, gsize* out_len, gchar** err_out) {
    gsize len = 0;
    gboolean too_large = FALSE;
    gchar* raw = load_bytes(path, &len, &too_large);
    if (raw == NULL) {
        gchar* base = g_path_get_basename(path);
        *err_out = too_large ? g_strdup_printf(_("%s is too large to compare"), base)
                             : g_strdup_printf(_("Cannot read %s"), base);
        g_free(base);
        return NULL;
    }
    if (!g_utf8_validate(raw, (gssize)len, NULL)) {
        if (!file_content_is_text((const guchar*)raw, MIN(len, FILE_SNIFF_BYTES))) {
            g_free(raw);
            gchar* base = g_path_get_basename(path);
            *err_out = g_strdup_printf(_("%s is a binary file: cannot compare as text"), base);
            g_free(base);
            return NULL;
        }
        gchar* utf8 = text_encoding_to_utf8(raw, len, NULL);
        g_free(raw);
        raw = utf8;
        len = strlen(raw);
    }
    *out_len = len;
    return raw;
}

static void diff_worker(GTask* task, gpointer source, gpointer task_data, GCancellable* cancellable) {
    (void)source;
    DiffTaskData* td = task_data;
    if (g_cancellable_is_cancelled(cancellable)) {
        g_task_return_boolean(task, FALSE);
        return;
    }

    gchar* l_text = NULL;
    gchar* r_text = NULL;
    gsize l_len = 0, r_len = 0;

    if (td->src_kind == DIFF_SRC_FILES) {
        l_text = read_text_file(td->left_path, &l_len, &td->error);
        if (l_text != NULL && !g_cancellable_is_cancelled(cancellable)) {
            r_text = read_text_file(td->right_path, &r_len, &td->error);
        }
    } else if (td->src_kind == DIFF_SRC_HEAD) {
        l_text = git_read_head_blob(td->right_path, &l_len);
        if (l_text == NULL) {
            l_text = g_strdup("");
            l_len = 0;
        }
        if (!g_cancellable_is_cancelled(cancellable)) {
            if (td->right_text != NULL) {
                r_text = g_strdup(td->right_text);
                r_len = strlen(r_text);
            } else {
                r_text = read_text_file(td->right_path, &r_len, &td->error);
            }
        }
    } else {
        l_text = g_strdup(td->left_text != NULL ? td->left_text : "");
        r_text = g_strdup(td->right_text != NULL ? td->right_text : "");
        l_len = strlen(l_text);
        r_len = strlen(r_text);
    }

    if (td->error == NULL && !g_cancellable_is_cancelled(cancellable)) {
        if (td->swapped) {
            td->doc = side_diff_compute(r_text, r_len, l_text, l_len);
        } else {
            td->doc = side_diff_compute(l_text, l_len, r_text, r_len);
        }
    }
    g_free(l_text);
    g_free(r_text);
    g_task_return_boolean(task, td->error == NULL && td->doc != NULL);
}

static void on_diff_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    DiffTab* dt = data;
    GTask* task = G_TASK(res);
    DiffTaskData* td = g_task_get_task_data(task);
    if (dt->closed || g_cancellable_is_cancelled(g_task_get_cancellable(task))) {
        return;
    }
    SideDiffDoc* doc = td->doc;
    td->doc = NULL;
    apply_diff_result(dt, doc, td->error);
}

static void start_diff_load(DiffTab* dt) {
    if (dt->cancellable != NULL) {
        g_cancellable_cancel(dt->cancellable);
        g_clear_object(&dt->cancellable);
    }
    dt->cancellable = g_cancellable_new();
    gtk_label_set_text(GTK_LABEL(dt->stats_label), _("Comparing…"));

    DiffTaskData* td = g_new0(DiffTaskData, 1);
    td->dt = dt;
    td->src_kind = dt->src_kind;
    td->left_path = g_strdup(dt->left_path);
    td->right_path = g_strdup(dt->right_path);
    td->left_text = g_strdup(dt->left_text_init);
    td->right_text = g_strdup(dt->right_text_init);
    td->swapped = dt->swapped;

    GTask* task = g_task_new(NULL, dt->cancellable, on_diff_done, dt);
    g_task_set_task_data(task, td, diff_task_data_free);
    g_task_run_in_thread(task, diff_worker);
    g_object_unref(task);
}

/* ------------------------------------------------------------------------- */
/* Actions & keyboard navigation                                             */
/* ------------------------------------------------------------------------- */

static void open_current_in_editor(DiffTab* dt) {
    if (dt == NULL || dt->app == NULL) return;
    const gchar* lpath = dt->swapped ? dt->right_path : dt->left_path;
    const gchar* rpath = dt->swapped ? dt->left_path : dt->right_path;
    const gchar* target = NULL;
    gint line = 0;

    if (is_unified_mode(dt)) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(dt->unified_buf, &it, gtk_text_buffer_get_insert(dt->unified_buf));
        guint uline = (guint)gtk_text_iter_get_line(&it);
        if (uline < dt->unified_src_lines->len) {
            gint sl = g_array_index(dt->unified_src_lines, gint, uline);
            gboolean sr = g_array_index(dt->unified_src_right, gboolean, uline);
            target = (sr && rpath != NULL) ? rpath : (lpath != NULL ? lpath : rpath);
            line = MAX(sl - 1, 0);
        }
    } else if (dt->doc != NULL && dt->doc->rows->len > 0) {
        GtkWidget* focus = gtk_window_get_focus(dt->app->window);
        gboolean on_left = (focus == dt->left_view);
        GtkTextBuffer* buf = on_left ? dt->left_buf : dt->right_buf;
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(buf, &it, gtk_text_buffer_get_insert(buf));
        guint row_idx = MIN((guint)gtk_text_iter_get_line(&it), dt->doc->rows->len - 1);
        SideDiffRow* r = g_ptr_array_index(dt->doc->rows, row_idx);
        if (on_left && lpath != NULL) {
            target = lpath;
            line = r->left_line > 0 ? r->left_line - 1 : MAX(r->right_line - 1, 0);
        } else if (rpath != NULL) {
            target = rpath;
            line = r->right_line > 0 ? r->right_line - 1 : MAX(r->left_line - 1, 0);
        } else if (lpath != NULL) {
            target = lpath;
            line = r->left_line > 0 ? r->left_line - 1 : 0;
        }
    }
    if (target == NULL) {
        target = rpath != NULL ? rpath : lpath;
    }
    if (target != NULL) {
        editor_open_at(dt->app, target, line, 0);
    }
}

static void on_prev_hunk_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    step_hunk((DiffTab*)data, -1);
}

static void on_next_hunk_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    step_hunk((DiffTab*)data, +1);
}

static void on_mode_toggled(GtkToggleButton* btn, gpointer data) {
    DiffTab* dt = data;
    if (!gtk_toggle_button_get_active(btn)) return;
    gboolean unified = (GTK_WIDGET(btn) == dt->unified_btn);
    gtk_stack_set_visible_child_name(GTK_STACK(dt->stack), unified ? "unified" : "split");
    dt->current_hunk = -1;
    update_hunk_ui(dt);
    gtk_widget_grab_focus(unified ? dt->unified_view : dt->right_view);
}

static void on_swap_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    DiffTab* dt = data;
    dt->swapped = !dt->swapped;
    start_diff_load(dt);
}

static void on_refresh_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    start_diff_load((DiffTab*)data);
}

static void on_edit_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    open_current_in_editor((DiffTab*)data);
}

static void on_view_double_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)gesture; (void)x; (void)y;
    if (n_press == 2) {
        open_current_in_editor((DiffTab*)data);
    }
}

static gboolean on_key_pressed(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                               GdkModifierType state, gpointer data) {
    (void)ctrl; (void)keycode;
    DiffTab* dt = data;
    gboolean ctrl_m = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt_m = (state & GDK_ALT_MASK) != 0;
    gboolean shift_m = (state & GDK_SHIFT_MASK) != 0;

    /* Vim ]c / [c hunk navigation */
    if (dt->pending_key != 0) {
        guint prev = dt->pending_key;
        dt->pending_key = 0;
        if (!ctrl_m && !alt_m && keyval == GDK_KEY_c) {
            step_hunk(dt, prev == GDK_KEY_bracketright ? +1 : -1);
            return TRUE;
        }
    }
    if (!ctrl_m && !alt_m && (keyval == GDK_KEY_bracketright || keyval == GDK_KEY_bracketleft)) {
        dt->pending_key = keyval;
        return TRUE;
    }

    /* Alt+Down / Alt+Up (every mode), Emacs M-n / M-p */
    if (alt_m && !ctrl_m && !shift_m) {
        if (keyval == GDK_KEY_Down || keyval == GDK_KEY_n) {
            step_hunk(dt, +1);
            return TRUE;
        }
        if (keyval == GDK_KEY_Up || keyval == GDK_KEY_p) {
            step_hunk(dt, -1);
            return TRUE;
        }
    }

    if (!ctrl_m && !alt_m && !shift_m) {
        if (keyval == GDK_KEY_F5 && dt->src_kind != DIFF_SRC_TEXTS) {
            start_diff_load(dt);
            return TRUE;
        }
        if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
            open_current_in_editor(dt);
            return TRUE;
        }
    }

    /* Panel/tab navigation keys from the global mode */
    NavStyle style = app_nav_style(dt->app);
    NavPrefix next_pref = NAV_PREFIX_NONE;
    NavAction act = nav_keys_lookup(style, dt->nav_prefix, keyval, state, &next_pref);
    dt->nav_prefix = next_pref;
    if (act == NAV_PENDING) return TRUE;
    if (act == NAV_PANEL_NEXT || act == NAV_PANEL_PREV || act == NAV_TAB_CLOSE ||
        act == NAV_TAB_NEXT || act == NAV_TAB_PREV) {
        focus_route_panel_action(dt->app, dt->root, act);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* Widget construction & tab creation                                        */
/* ------------------------------------------------------------------------- */

static GtkWidget* make_diff_view(GtkTextBuffer** out_buf, DiffTab* dt) {
#ifdef HAVE_GSV
    GtkTextBuffer* buf = GTK_TEXT_BUFFER(gtk_source_buffer_new(NULL));
    GtkWidget* view = gtk_source_view_new_with_buffer(GTK_SOURCE_BUFFER(buf));
#else
    GtkTextBuffer* buf = gtk_text_buffer_new(NULL);
    GtkWidget* view = gtk_text_view_new_with_buffer(buf);
#endif
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    set_view_monospace(view, TRUE);
    set_view_text_padding(view);
#ifdef HAVE_GSV
    apply_vscode_scheme(buf, view);
#endif
    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_view_double_click), dt);
    gtk_widget_add_controller(view, GTK_EVENT_CONTROLLER(click));
    *out_buf = buf;
    return view;
}

static GtkWidget* tab_label_new(DiffTab* dt, const gchar* title) {
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* icon = icon_image_new("view-dual");
    gtk_box_append(GTK_BOX(hbox), icon);

    GtkWidget* lbl = gtk_label_new(title);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), DIFF_TAB_MAX_CHARS);
    /* As wide as the title (up to DIFF_TAB_MAX_CHARS) even when tabs are crowded */
    gtk_label_set_width_chars(GTK_LABEL(lbl), (gint)MIN(g_utf8_strlen(title, -1), DIFF_TAB_MAX_CHARS));
    gtk_box_append(GTK_BOX(hbox), lbl);
    gtk_widget_set_tooltip_text(hbox, title);

    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_tab_right_click), dt);
    gtk_widget_add_controller(hbox, GTK_EVENT_CONTROLLER(rclick));

    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_tab_middle_click), dt);
    gtk_widget_add_controller(hbox, GTK_EVENT_CONTROLLER(mclick));

    return g_object_ref_sink(hbox);
}

static DiffTab* diff_tab_create(AppData* app, DiffSourceKind kind, const gchar* title,
                                const gchar* left_label, const gchar* left_path, const gchar* left_text,
                                const gchar* right_label, const gchar* right_path, const gchar* right_text,
                                const gchar* lang_hint) {
    DiffTab* dt = g_new0(DiffTab, 1);
    dt->app = app;
    dt->src_kind = kind;
    dt->title = g_strdup(title);
    dt->left_label = g_strdup(left_label);
    dt->right_label = g_strdup(right_label);
    dt->left_path = g_strdup(left_path);
    dt->right_path = g_strdup(right_path);
    dt->left_text_init = g_strdup(left_text);
    dt->right_text_init = g_strdup(right_text);
    dt->lang_hint = g_strdup(lang_hint);
    dt->current_hunk = -1;
    dt->unified_hunks = g_array_new(FALSE, FALSE, sizeof(guint));
    dt->unified_src_lines = g_array_new(FALSE, FALSE, sizeof(gint));
    dt->unified_src_right = g_array_new(FALSE, FALSE, sizeof(gboolean));

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    dt->root = root;

    /* Top toolbar */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(bar, "diff-tab-bar");

    dt->stats_label = gtk_label_new(_("Comparing…"));
    gtk_label_set_xalign(GTK_LABEL(dt->stats_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(dt->stats_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(dt->stats_label, TRUE);
    gtk_box_append(GTK_BOX(bar), dt->stats_label);

    dt->hunk_label = gtk_label_new("");
    gtk_widget_add_css_class(dt->hunk_label, "dim-label");
    gtk_box_append(GTK_BOX(bar), dt->hunk_label);

    GtkWidget* nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(nav_box, "linked");
    dt->prev_btn = icon_button_new("go-previous", _("Previous Hunk (Alt+Up / [c)"));
    dt->next_btn = icon_button_new("go-next", _("Next Hunk (Alt+Down / ]c)"));
    gtk_widget_set_focusable(dt->prev_btn, FALSE);
    gtk_widget_set_focusable(dt->next_btn, FALSE);
    g_signal_connect(dt->prev_btn, "clicked", G_CALLBACK(on_prev_hunk_clicked), dt);
    g_signal_connect(dt->next_btn, "clicked", G_CALLBACK(on_next_hunk_clicked), dt);
    gtk_box_append(GTK_BOX(nav_box), dt->prev_btn);
    gtk_box_append(GTK_BOX(nav_box), dt->next_btn);
    gtk_box_append(GTK_BOX(bar), nav_box);

    GtkWidget* mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(mode_box, "linked");
    dt->split_btn = gtk_toggle_button_new_with_label(_("Split"));
    dt->unified_btn = gtk_toggle_button_new_with_label(_("Unified"));
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(dt->unified_btn), GTK_TOGGLE_BUTTON(dt->split_btn));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dt->split_btn), TRUE);
    gtk_widget_set_focusable(dt->split_btn, FALSE);
    gtk_widget_set_focusable(dt->unified_btn, FALSE);
    g_signal_connect(dt->split_btn, "toggled", G_CALLBACK(on_mode_toggled), dt);
    g_signal_connect(dt->unified_btn, "toggled", G_CALLBACK(on_mode_toggled), dt);
    gtk_box_append(GTK_BOX(mode_box), dt->split_btn);
    gtk_box_append(GTK_BOX(mode_box), dt->unified_btn);
    gtk_box_append(GTK_BOX(bar), mode_box);

    dt->swap_btn = icon_button_new("swap-horizontal", _("Swap Left and Right Sides"));
    gtk_widget_set_focusable(dt->swap_btn, FALSE);
    g_signal_connect(dt->swap_btn, "clicked", G_CALLBACK(on_swap_clicked), dt);
    gtk_box_append(GTK_BOX(bar), dt->swap_btn);

    dt->refresh_btn = icon_button_new("view-refresh", _("Refresh Comparison (F5)"));
    gtk_widget_set_focusable(dt->refresh_btn, FALSE);
    g_signal_connect(dt->refresh_btn, "clicked", G_CALLBACK(on_refresh_clicked), dt);
    gtk_box_append(GTK_BOX(bar), dt->refresh_btn);

    dt->edit_btn = icon_button_new("document-edit", _("Edit File at Current Line (Enter)"));
    gtk_widget_set_focusable(dt->edit_btn, FALSE);
    g_signal_connect(dt->edit_btn, "clicked", G_CALLBACK(on_edit_clicked), dt);
    gtk_box_append(GTK_BOX(bar), dt->edit_btn);

    gtk_box_append(GTK_BOX(root), bar);

    /* Stack: Split vs Unified */
    dt->stack = gtk_stack_new();
    gtk_widget_set_hexpand(dt->stack, TRUE);
    gtk_widget_set_vexpand(dt->stack, TRUE);

    /* Split page */
    dt->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    paned_use_thin_handle(dt->paned);
    gtk_paned_set_resize_start_child(GTK_PANED(dt->paned), TRUE);
    gtk_paned_set_resize_end_child(GTK_PANED(dt->paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(dt->paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(dt->paned), TRUE);

    GtkWidget* lbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    dt->left_hdr = gtk_label_new(left_label);
    gtk_widget_add_css_class(dt->left_hdr, "diff-pane-header");
    gtk_label_set_xalign(GTK_LABEL(dt->left_hdr), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(dt->left_hdr), PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(lbox), dt->left_hdr);
    dt->left_view = make_diff_view(&dt->left_buf, dt);
    dt->left_scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(dt->left_scroll, TRUE);
    gtk_widget_set_vexpand(dt->left_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(dt->left_scroll), dt->left_view);
    gtk_box_append(GTK_BOX(lbox), dt->left_scroll);
    diff_gutter_pane_init(&dt->left_gutter, &dt->doc, dt->left_view, dt->left_buf, dt->left_scroll, FALSE);

    GtkWidget* rbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    dt->right_hdr = gtk_label_new(right_label);
    gtk_widget_add_css_class(dt->right_hdr, "diff-pane-header");
    gtk_label_set_xalign(GTK_LABEL(dt->right_hdr), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(dt->right_hdr), PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(rbox), dt->right_hdr);
    dt->right_view = make_diff_view(&dt->right_buf, dt);
    dt->right_scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(dt->right_scroll, TRUE);
    gtk_widget_set_vexpand(dt->right_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(dt->right_scroll), dt->right_view);
    gtk_box_append(GTK_BOX(rbox), dt->right_scroll);
    diff_gutter_pane_init(&dt->right_gutter, &dt->doc, dt->right_view, dt->right_buf, dt->right_scroll, TRUE);

    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(dt->left_scroll)),
                     "value-changed", G_CALLBACK(on_left_vscroll), dt);
    g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(dt->right_scroll)),
                     "value-changed", G_CALLBACK(on_right_vscroll), dt);
    g_signal_connect(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(dt->left_scroll)),
                     "value-changed", G_CALLBACK(on_left_hscroll), dt);
    g_signal_connect(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(dt->right_scroll)),
                     "value-changed", G_CALLBACK(on_right_hscroll), dt);

    gtk_paned_set_start_child(GTK_PANED(dt->paned), lbox);
    gtk_paned_set_end_child(GTK_PANED(dt->paned), rbox);
    gtk_stack_add_named(GTK_STACK(dt->stack), dt->paned, "split");

    /* Unified page */
    dt->unified_view = make_diff_view(&dt->unified_buf, dt);
    dt->unified_scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(dt->unified_scroll, TRUE);
    gtk_widget_set_vexpand(dt->unified_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(dt->unified_scroll), dt->unified_view);
    gtk_stack_add_named(GTK_STACK(dt->stack), dt->unified_scroll, "unified");

    gtk_stack_set_visible_child_name(GTK_STACK(dt->stack), "split");
    gtk_box_append(GTK_BOX(root), dt->stack);

    focus_route_mark_focus(dt->right_view);
    editor_page_set_movable(root);

    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), dt);
    gtk_widget_add_controller(root, keys);

    dt->tab_label = tab_label_new(dt, title);
    g_object_set_data_full(G_OBJECT(root), DIFF_TAB_KEY, dt, tab_free);
    g_signal_connect(root, "destroy", G_CALLBACK(on_root_destroy), dt);

    if (app->diff_pages == NULL) app->diff_pages = g_ptr_array_new();
    g_ptr_array_add(app->diff_pages, root);

    editor_add_page_like_file(app, root, dt->tab_label);
    GtkNotebook* nb = notebook_of(root);
    if (nb != NULL) {
        gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, root));
    }
    gtk_widget_grab_focus(dt->right_view);
    update_headers(dt);
    start_diff_load(dt);
    return dt;
}

/* ------------------------------------------------------------------------- */
/* Public entry points                                                       */
/* ------------------------------------------------------------------------- */

void diff_tab_open_files(AppData* app, const gchar* left_path, const gchar* right_path) {
    if (app == NULL || left_path == NULL || right_path == NULL || app->notebook == NULL) return;
    gchar* lbase = g_path_get_basename(left_path);
    gchar* rbase = g_path_get_basename(right_path);
    gchar* title = g_strdup_printf("%s ↔ %s", lbase, rbase);
    diff_tab_create(app, DIFF_SRC_FILES, title,
                    left_path, left_path, NULL,
                    right_path, right_path, NULL,
                    rbase);
    g_free(title);
    g_free(lbase);
    g_free(rbase);
}

void diff_tab_open_head(AppData* app, const gchar* path, const gchar* right_text) {
    if (app == NULL || path == NULL || app->notebook == NULL) return;
    gchar* base = g_path_get_basename(path);
    gchar* title = g_strdup_printf(_("%s (HEAD)"), base);
    gchar* llabel = g_strdup_printf(_("%s (HEAD)"), base);
    gchar* rlabel = right_text != NULL
        ? g_strdup_printf(_("%s (Buffer)"), base)
        : g_strdup_printf(_("%s (Working Tree)"), base);
    diff_tab_create(app, DIFF_SRC_HEAD, title,
                    llabel, NULL, NULL,
                    rlabel, path, right_text,
                    base);
    g_free(rlabel);
    g_free(llabel);
    g_free(title);
    g_free(base);
}

void diff_tab_open_texts(AppData* app, const gchar* title,
                         const gchar* left_label, const gchar* left_path, const gchar* left_text,
                         const gchar* right_label, const gchar* right_path, const gchar* right_text,
                         const gchar* lang_hint) {
    if (app == NULL || app->notebook == NULL) return;
    diff_tab_create(app, DIFF_SRC_TEXTS, title != NULL ? title : _("Diff"),
                    left_label != NULL ? left_label : _("Left"), left_path, left_text,
                    right_label != NULL ? right_label : _("Right"), right_path, right_text,
                    lang_hint);
}

static DiffTab* focused_diff_tab(AppData* app) {
    if (app == NULL || app->window == NULL) return NULL;
    for (GtkWidget* w = gtk_window_get_focus(app->window); w != NULL; w = gtk_widget_get_parent(w)) {
        DiffTab* dt = tab_of(w);
        if (dt != NULL) return dt;
    }
    return NULL;
}

gboolean diff_tab_close_shown(AppData* app) {
    if (app == NULL) return FALSE;
    DiffTab* dt = focused_diff_tab(app);
    if (dt == NULL && app->notebook != NULL) {
        GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
        gint cur = gtk_notebook_get_current_page(nb);
        dt = cur >= 0 ? tab_of(gtk_notebook_get_nth_page(nb, cur)) : NULL;
        if (dt != NULL && !gtk_widget_get_mapped(dt->root)) dt = NULL;
    }
    if (dt == NULL || dt->closed) return FALSE;
    close_tab(dt);
    return TRUE;
}

void diff_tab_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (gint i = gtk_notebook_get_n_pages(nb) - 1; i >= 0; i--) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        if (tab_of(page) == NULL) continue;
        if (ws->diff_pages == NULL) ws->diff_pages = g_ptr_array_new_with_free_func(g_object_unref);
        g_ptr_array_insert(ws->diff_pages, 0, g_object_ref(page));
        gtk_notebook_remove_page(nb, i);
    }
}

void diff_tab_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->diff_pages == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (guint i = 0; i < ws->diff_pages->len; i++) {
        GtkWidget* root = g_ptr_array_index(ws->diff_pages, i);
        DiffTab* dt = tab_of(root);
        if (dt == NULL) continue;
        gtk_notebook_append_page(nb, root, dt->tab_label);
        gtk_notebook_set_tab_reorderable(nb, root, TRUE);
        gtk_notebook_set_tab_detachable(nb, root, TRUE);
    }
    g_clear_pointer(&ws->diff_pages, g_ptr_array_unref);
}

void diff_tab_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        diff_tab_workspace_hidden(app, ws);
    }
    g_clear_pointer(&ws->diff_pages, g_ptr_array_unref);
}
