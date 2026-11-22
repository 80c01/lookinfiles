/* References of a symbol in the References tab (see code_refs.h) */
#include "code/code_refs.h"
#include "core/i18n.h"
#include "core/remote_path.h"
#include "theme/palette.h"
#include "code/code_index.h"
#include "code/code_intel.h"
#include "editor/editor.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "widgets/focus_route.h"
#include "widgets/panels.h"
#include "git/git_tab.h"
#include <stdarg.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* One row: a file header or an occurrence                                   */
/* ------------------------------------------------------------------------- */

#define CODE_TYPE_REF_ITEM (code_ref_item_get_type())
G_DECLARE_FINAL_TYPE(CodeRefItem, code_ref_item, CODE, REF_ITEM, GObject)

struct _CodeRefItem {
    GObject parent;
    gboolean header;   /* file row */
    gchar* path;
    gchar* shown;      /* path shown (relative to the project; SearchArea: the detail) */
    gchar* title;      /* header: bold name (NULL: the file name) */
    gchar* icon_name;  /* header: file name of its type icon (NULL: the file's) */
    gint line;         /* occurrence: line and byte column of the name */
    gint column;
    gint length;       /* bytes of the name */
    gchar* text;       /* occurrence: the line; header: NULL */
    guint count;       /* header: occurrences in the file */
};

G_DEFINE_FINAL_TYPE(CodeRefItem, code_ref_item, G_TYPE_OBJECT)

static void code_ref_item_finalize(GObject* obj) {
    CodeRefItem* it = CODE_REF_ITEM(obj);
    g_free(it->path);
    g_free(it->shown);
    g_free(it->title);
    g_free(it->icon_name);
    g_free(it->text);
    G_OBJECT_CLASS(code_ref_item_parent_class)->finalize(obj);
}

static void code_ref_item_class_init(CodeRefItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = code_ref_item_finalize;
}

static void code_ref_item_init(CodeRefItem* it) {
    (void)it;
}

/* ------------------------------------------------------------------------- */
/* The page                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    GtkWidget** slot;  /* &app->refs_page or &app->search_page */
    GtkWidget* page;
    GtkWidget* label;  /* tab label (owned ref: kept while the page is detached) */
    GtkWidget* header;
    GtkWidget* list;
    GListStore* store;
    GtkSingleSelection* selection;
    const SearchArea* area; /* how its files show and open (NULL: as files) */
    gchar* origin;     /* References: where the lookup started (NULL: unknown) */
    gint origin_line;
    gint origin_column;
} RefsPage;

#define PAGE_KEY "code-refs-page"

static RefsPage* page_state(GtkWidget* page) {
    return page != NULL ? g_object_get_data(G_OBJECT(page), PAGE_KEY) : NULL;
}

GtkWidget* code_refs_page(AppData* app) {
    return app != NULL ? app->refs_page : NULL;
}

GtkWidget* code_refs_search_page(AppData* app) {
    return app != NULL ? app->search_page : NULL;
}

/* A translated sentence put in Pango markup: the sentence is escaped, the
 * arguments are markup already (bold names, count_markup()) or numbers */
static gchar* markup_sentence(const gchar* translated, ...) {
    gchar* format = g_markup_escape_text(translated, -1);
    va_list args;
    va_start(args, translated);
    gchar* out = g_strdup_vprintf(format, args);
    va_end(args);
    g_free(format);
    return out;
}

/* "3 files", translated and escaped for markup */
static gchar* count_markup(const gchar* translated, guint n) {
    gchar* text = g_strdup_printf(translated, n);
    gchar* out = g_markup_escape_text(text, -1);
    g_free(text);
    return out;
}

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".code-refs { background-color: @bg_view; }"
                  ".code-refs listview { background-color: @bg_view; }"
                  ".code-refs .refs-header { padding: 6px 10px; color: @fg; }"
                  ".code-refs .ref-line { font-family: @font_code; padding: 1px 10px 1px 28px; }"
                  ".code-refs .ref-file { padding: 4px 10px 1px 10px; }");
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    /* File-type icon (file headers only), then the text */
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), file_type_image_new());
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_item_set_child(item, box);
}

/* Bytes of a result's line shown around the match */
#define SHOWN_BEFORE_BYTES 120
#define SHOWN_MATCH_BYTES 200
#define SHOWN_AFTER_BYTES 240

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* box = gtk_list_item_get_child(item);
    GtkWidget* icon = gtk_widget_get_first_child(box);
    GtkWidget* label = gtk_widget_get_last_child(box);
    CodeRefItem* it = gtk_list_item_get_item(item);
    gchar* markup;
    gtk_widget_remove_css_class(box, it->header ? "ref-line" : "ref-file");
    gtk_widget_add_css_class(box, it->header ? "ref-file" : "ref-line");
    gtk_widget_set_visible(icon, it->header);
    if (it->header) {
        gchar* base = g_path_get_basename(it->path);
        file_type_image_set(icon, it->icon_name != NULL ? it->icon_name : base, FALSE);
        markup = g_markup_printf_escaped("<b>%s</b>  <span foreground=\"%s\">%s · %u</span>",
                                         it->title != NULL ? it->title : base, palette_color(PAL_FG_SUBTLE),
                                         it->shown, it->count);
        g_free(base);
    } else {
        /* Leading spaces dropped, the name in bold on a highlight; a long
         * line (minified code) cut around it: a label lays out all it holds */
        const gchar* t = it->text;
        gint skip = 0;
        while (t[skip] == ' ' && skip < it->column) skip++;
        const gchar* line = t + skip;
        gint line_len = (gint)strlen(line);
        gint col = CLAMP(it->column - skip, 0, line_len);
        gint len = CLAMP(it->length, 0, line_len - col);
        len = MIN(len, SHOWN_MATCH_BYTES);
        const gchar* from = line + MAX(col - SHOWN_BEFORE_BYTES, 0);
        while (from > line && ((guchar)*from & 0xC0) == 0x80) from--; /* a character's start */
        const gchar* hit = line + col;
        const gchar* hit_end = hit + len;
        while (*hit_end != '\0' && ((guchar)*hit_end & 0xC0) == 0x80) hit_end++; /* whole characters */
        const gchar* to = hit_end + MIN(line_len - (gint)(hit_end - line), SHOWN_AFTER_BYTES);
        while (*to != '\0' && ((guchar)*to & 0xC0) == 0x80) to++;
        gchar* before = g_strndup(from, (gsize)(hit - from));
        gchar* name = g_strndup(hit, (gsize)(hit_end - hit));
        gchar* after = g_strndup(hit_end, (gsize)(to - hit_end));
        markup = g_markup_printf_escaped("<span foreground=\"%s\">%5d</span>  %s%s<span background=\"%s\"><b>%s</b></span>%s%s",
                                         palette_color(PAL_FG_SUBTLE), it->line + 1, from > line ? "…" : "", before,
                                         palette_color(PAL_EDITOR_MATCH), name, after, *to != '\0' ? "…" : "");
        g_free(after);
        g_free(name);
        g_free(before);
    }
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
}

/* An occurrence: the area's way first, else in an editor tab */
static void open_at(RefsPage* rp, const gchar* path, gint line, gint column) {
    if (rp->area != NULL && rp->area->open != NULL && rp->area->open(rp->app, path, line, column)) return;
    /* The jump starts where the lookup did: back (Ctrl-O...) returns there */
    if (editor_open_at(rp->app, path, line, column) != NULL && rp->origin != NULL) {
        code_nav_remember(rp->app, rp->origin, rp->origin_line, rp->origin_column);
    }
}

/* The back key of the global mode: Vim Ctrl-O, Emacs M-, Notepad Ctrl+Alt+- */
static gboolean is_back_key(NavStyle style, guint keyval, GdkModifierType state) {
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    switch (style) {
    case NAV_STYLE_VIM: return mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_o || keyval == GDK_KEY_O);
    case NAV_STYLE_EMACS: return mods == GDK_ALT_MASK && keyval == GDK_KEY_comma;
    default:
        return mods == (GDK_CONTROL_MASK | GDK_ALT_MASK) && (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract);
    }
}

/* The back key in the list: to the tab and place of the lookup */
static gboolean on_list_key(GtkEventControllerKey* controller, guint keyval, guint keycode,
                            GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    RefsPage* rp = data;
    if (rp->origin == NULL || !is_back_key(app_nav_style(rp->app), keyval, state)) return FALSE;
    rp->app->nav_prefix = NAV_PREFIX_NONE;
    editor_open_at(rp->app, rp->origin, rp->origin_line, rp->origin_column);
    return TRUE;
}

static void open_item(RefsPage* rp, CodeRefItem* it) {
    if (it == NULL) return;
    if (it->header) {
        /* The file's first occurrence */
        guint n = g_list_model_get_n_items(G_LIST_MODEL(rp->store));
        for (guint i = 0; i < n; i++) {
            CodeRefItem* x = g_list_model_get_item(G_LIST_MODEL(rp->store), i);
            gboolean found = x == it && i + 1 < n;
            g_object_unref(x);
            if (found) {
                CodeRefItem* next = g_list_model_get_item(G_LIST_MODEL(rp->store), i + 1);
                open_at(rp, next->path, next->line, next->column);
                g_object_unref(next);
                return;
            }
        }
        return;
    }
    open_at(rp, it->path, it->line, it->column);
}

static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    RefsPage* rp = data;
    CodeRefItem* it = g_list_model_get_item(G_LIST_MODEL(rp->store), position);
    open_item(rp, it);
    if (it != NULL) g_object_unref(it);
}

/* The notebook holding page (the preview notebook, an editor notebook or a
 * terminal pane), or NULL when it is out of every notebook */
static GtkNotebook* page_notebook(GtkWidget* page) {
    GtkWidget* parent = page != NULL ? gtk_widget_get_parent(page) : NULL;
    GtkWidget* nb = parent != NULL ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    return (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), page) >= 0) ? GTK_NOTEBOOK(nb) : NULL;
}

/* Take page out of the notebook holding it (an editor notebook left empty goes) */
static void remove_page(AppData* app, GtkWidget* page) {
    GtkNotebook* nb = page_notebook(page);
    if (nb == NULL) return;
    if (GTK_WIDGET(nb) == app->notebook) {
        gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, page));
    } else {
        editor_remove_page(app, page);
    }
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    RefsPage* rp = data;
    AppData* app = rp->app;
    GtkWidget** slot = rp->slot;
    if (*slot == NULL || app->notebook == NULL) return;
    GtkWidget* closed = *slot;
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    *slot = NULL;
    remove_page(app, closed);
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed); /* frees rp */
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press,
                                  gdouble x, gdouble y, gpointer data) {
    (void)n_press; (void)x; (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    on_close_clicked(NULL, data);
}

static void on_menu_close(GtkWidget* child, gpointer data) {
    (void)child;
    on_close_clicked(NULL, data);
}

/* The tab menu, as for an editor tab: move, close */
static void on_label_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press; (void)x; (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    RefsPage* rp = data;
    editor_page_show_context_menu(rp->app, rp->page, rp->label, on_menu_close, rp);
}

static void page_free(gpointer p) {
    RefsPage* rp = p;
    if (rp->slot != NULL && *rp->slot == rp->page) *rp->slot = NULL;
    g_object_unref(rp->store);
    g_clear_object(&rp->label);
    g_free(rp->origin);
    g_free(rp);
}

/* Put the page in the notebook, right after the Git tab and the other result
 * page (editor tabs follow); it moves from there like an editor tab */
static void insert_page(AppData* app, RefsPage* rp) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* git_page = git_tab_page(app);
    gint after = git_page != NULL ? gtk_notebook_page_num(nb, git_page) : 0;
    GtkWidget* other = rp->slot == &app->refs_page ? app->search_page : app->refs_page;
    if (other != NULL) after = MAX(after, gtk_notebook_page_num(nb, other));
    gtk_notebook_insert_page(nb, rp->page, rp->label, after + 1);
    gtk_notebook_set_tab_reorderable(nb, rp->page, TRUE);
    gtk_notebook_set_tab_detachable(nb, rp->page, TRUE);
    *rp->slot = rp->page;
}

/* The References or Search page (slot), made when missing: right after
 * the Git tab, or after the other result page */
static RefsPage* ensure_page(AppData* app, GtkWidget** slot, const gchar* title) {
    RefsPage* rp = page_state(*slot);
    if (rp != NULL) return rp;
    if (app->notebook == NULL) return NULL;
    ensure_css();
    rp = g_new0(RefsPage, 1);
    rp->app = app;
    rp->slot = slot;
    rp->store = g_list_store_new(CODE_TYPE_REF_ITEM);

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "code-refs");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(bar, "refs-header");
    rp->header = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(rp->header), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(rp->header), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(rp->header, TRUE);
    gtk_box_append(GTK_BOX(bar), rp->header);
    gchar* tip = g_strdup_printf(_("Close %s"), title);
    GtkWidget* close = icon_button_new("window-close", tip);
    g_free(tip);
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), rp);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(page), bar);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), NULL);
    rp->selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(rp->store)));
    rp->list = gtk_list_view_new(GTK_SELECTION_MODEL(rp->selection), factory);
    g_signal_connect(rp->list, "activate", G_CALLBACK(on_activate), rp);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_list_key), rp);
    gtk_widget_add_controller(rp->list, keys);
    focus_route_attach_list(app, rp->list, rp->selection);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), rp->list);
    gtk_box_append(GTK_BOX(page), scroll);

    rp->page = page;
    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, rp, page_free);
    rp->label = g_object_ref(icon_label_new("edit-find", title));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), rp);
    gtk_widget_add_controller(rp->label, GTK_EVENT_CONTROLLER(mclick));
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_label_right_click), rp);
    gtk_widget_add_controller(rp->label, GTK_EVENT_CONTROLLER(rclick));
    editor_page_set_movable(page);
    editor_page_set_layout_key(page, slot == &app->refs_page ? "references" : "search");
    insert_page(app, rp);
    return rp;
}

/* Fill the page: a header row per file then the occurrences (CodeReference*,
 * grouped by file), their length (else length) bytes highlighted. Returns the
 * files. */
static guint fill_page(RefsPage* rp, GPtrArray* refs, const gchar* root, gsize length, const SearchArea* area) {
    g_list_store_remove_all(rp->store);
    rp->area = area;
    guint files = 0;
    const gchar* header_path = NULL;
    for (guint i = 0; i < refs->len; i++) {
        CodeReference* r = g_ptr_array_index(refs, i);
        if (header_path == NULL || strcmp(header_path, r->path) != 0) {
            /* Count first: rows are drawn as soon as they are added */
            guint count = 0;
            for (guint j = i; j < refs->len && strcmp(((CodeReference*)g_ptr_array_index(refs, j))->path, r->path) == 0; j++) {
                count++;
            }
            CodeRefItem* header = g_object_new(CODE_TYPE_REF_ITEM, NULL);
            header->header = TRUE;
            header->path = g_strdup(r->path);
            header->count = count;
            gsize n = root != NULL ? strlen(root) : 0;
            gchar* detail = NULL;
            if (area != NULL && area->describe != NULL &&
                area->describe(rp->app, r->path, &header->title, &detail, &header->icon_name)) {
                header->shown = detail != NULL ? detail : g_strdup("");
            } else {
                header->shown = g_strdup(n > 0 && strncmp(r->path, root, n) == 0 && r->path[n] == '/'
                                             ? r->path + n + 1
                                             : r->path);
            }
            g_list_store_append(rp->store, header);
            g_object_unref(header);
            header_path = r->path;
            files++;
        }
        CodeRefItem* it = g_object_new(CODE_TYPE_REF_ITEM, NULL);
        it->path = g_strdup(r->path);
        it->line = r->line;
        it->column = r->column;
        it->length = r->length > 0 ? r->length : (gint)length;
        it->text = g_strdup(r->text);
        g_list_store_append(rp->store, it);
        g_object_unref(it);
    }
    return files;
}

/* Show the page with its first occurrence selected and the focus in it */
static void present_page(AppData* app, RefsPage* rp, gboolean any) {
    GtkNotebook* nb = page_notebook(rp->page);
    if (nb == NULL) return;
    /* The preview panel shows when the tab is there and the panel hidden */
    if (GTK_WIDGET(nb) == app->notebook && app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        gtk_widget_set_visible(app->viewer_box, TRUE);
    }
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, rp->page));
    if (any) gtk_single_selection_set_selected(rp->selection, 1);
    gtk_widget_grab_focus(rp->list);
}

/* Show the references (CodeReference*) of name */
static void show_references(AppData* app, EditorTab* tab, CodeNavMessageFunc message, const gchar* name,
                            GPtrArray* refs, const gchar* root, const gchar* source, gboolean truncated) {
    RefsPage* rp = ensure_page(app, &app->refs_page, _("References"));
    if (rp == NULL) return;
    g_clear_pointer(&rp->origin, g_free);
    if (tab != NULL && g_list_find(app->editors, tab) != NULL && tab->path != NULL && tab->buffer != NULL) {
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
        rp->origin = g_strdup(tab->path);
        rp->origin_line = gtk_text_iter_get_line(&it);
        rp->origin_column = gtk_text_iter_get_line_index(&it);
    }
    guint files = fill_page(rp, refs, root, strlen(name), NULL);
    gchar* bold = g_markup_printf_escaped("<b>%s</b>", name);
    gchar* sentence = NULL;
    gchar* hint = NULL;
    if (refs->len == 0) {
        sentence = markup_sentence(_("No references to %s found"), bold);
        hint = g_strdup(source);
    } else {
        gchar* in_files = count_markup(ngettext("%u file", "%u files", files), files);
        /* TRANSLATORS: "References to NAME: 12 in 3 files" */
        sentence = markup_sentence(_("References to %1$s: %2$u in %3$s"), bold, refs->len, in_files);
        g_free(in_files);
        if (truncated) {
            gchar* cut = g_markup_escape_text(_(", list cut"), -1);
            gchar* joined = g_strconcat(sentence, cut, NULL);
            g_free(sentence);
            g_free(cut);
            sentence = joined;
        }
        /* TRANSLATORS: %s is where the references come from (a language server name, _("whole words in the project")) */
        hint = g_strdup_printf(_("%s; Enter opens"), source);
    }
    gchar* markup = g_markup_printf_escaped(" <span foreground=\"%s\">(%s)</span>", palette_color(PAL_FG_SUBTLE), hint);
    gchar* full = g_strconcat(sentence, markup, NULL);
    g_free(markup);
    markup = full;
    g_free(sentence);
    g_free(hint);
    g_free(bold);
    gtk_label_set_markup(GTK_LABEL(rp->header), markup);
    g_free(markup);
    present_page(app, rp, refs->len > 0);
    if (message != NULL && tab != NULL && g_list_find(app->editors, tab) != NULL) {
        gchar* msg = g_strdup_printf(ngettext("%1$u reference to %2$s (References tab)",
                                                "%1$u references to %2$s (References tab)", refs->len),
                                       refs->len, name);
        message(tab, msg);
        g_free(msg);
    }
}

void code_refs_show_searching(AppData* app, const gchar* query, const gchar* root) {
    RefsPage* rp = ensure_page(app, &app->search_page, _("Search"));
    if (rp == NULL) return;
    g_list_store_remove_all(rp->store);
    gchar* base = remote_path_display_name(app, root); /* a machine's name, not its folder's */
    gchar* bold = g_markup_printf_escaped("<b>%s</b>", query);
    gchar* where = g_markup_escape_text(base, -1);
    gchar* markup = markup_sentence(_("Searching for %1$s in %2$s…"), bold, where);
    g_free(where);
    g_free(bold);
    gtk_label_set_markup(GTK_LABEL(rp->header), markup);
    gtk_widget_set_tooltip_text(rp->header, NULL);
    g_free(markup);
    g_free(base);
    present_page(app, rp, FALSE);
}

/* Fill the Search page with the results, without showing it */
static void set_search(RefsPage* rp, const gchar* query, GPtrArray* refs, const gchar* root, const gchar* how,
                       const gchar* note, const SearchArea* area) {
    guint files = fill_page(rp, refs, root, strlen(query), area);
    /* Why the search stopped early first: the header is ellipsized */
    gchar* warn = note != NULL ? g_markup_printf_escaped(" <span foreground=\"%s\">· %s</span>",
                                                         palette_color(PAL_WARNING), note)
                               : g_strdup("");
    gchar* bold = g_markup_printf_escaped("<b>%s</b>", query);
    gchar* markup = NULL;
    if (refs->len == 0) {
        markup = markup_sentence(_("No results for %s"), bold);
    } else {
        gchar* in_files = count_markup(ngettext("%u file", "%u files", files), files);
        /* TRANSLATORS: "QUERY: 12 results in 3 files" */
        markup = markup_sentence(ngettext("%1$s: %2$u result in %3$s", "%1$s: %2$u results in %3$s", refs->len), bold,
                                 refs->len, in_files);
        g_free(in_files);
    }
    g_free(bold);
    gchar* hint = refs->len > 0 ? g_strdup_printf(_("%s; Enter opens"), how) : g_strdup(how);
    gchar* tail = g_markup_printf_escaped(" <span foreground=\"%s\">(%s)</span>", palette_color(PAL_FG_SUBTLE), hint);
    g_free(hint);
    gchar* all = g_strconcat(markup, warn, tail, NULL);
    gtk_label_set_markup(GTK_LABEL(rp->header), all);
    gtk_widget_set_tooltip_markup(rp->header, all);
    g_free(all);
    g_free(tail);
    g_free(markup);
    g_free(warn);
}

void code_refs_show_search(AppData* app, const gchar* query, GPtrArray* refs, const gchar* root,
                           const gchar* how, const gchar* note, const SearchArea* area) {
    RefsPage* rp = ensure_page(app, &app->search_page, _("Search"));
    if (rp == NULL) return;
    set_search(rp, query, refs, root, how, note, area);
    present_page(app, rp, refs->len > 0);
}

void code_refs_set_search(GtkWidget* page, const gchar* query, GPtrArray* refs, const gchar* root,
                          const gchar* how, const gchar* note, const SearchArea* area) {
    RefsPage* rp = page_state(page);
    if (rp != NULL) set_search(rp, query, refs, root, how, note, area);
}

/* The page of slot for the workspace left, an owned ref (or NULL): taken out
 * of the preview notebook with its tab; in the editor panel or a terminal
 * pane, it already left with them (editor_detach_tabs()) */
static GtkWidget* detach_slot(AppData* app, GtkWidget** slot) {
    GtkWidget* page = *slot;
    if (page_state(page) == NULL || app->notebook == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    *slot = NULL;
    GtkNotebook* nb = page_notebook(page);
    if (nb != NULL && GTK_WIDGET(nb) == app->notebook) gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, page));
    return page;
}

/* Put back a page taken by detach_slot() (takes the ref) */
static void attach_slot(AppData* app, GtkWidget** slot, GtkWidget* page) {
    RefsPage* rp = page_state(page);
    if (rp != NULL && app->notebook != NULL && *slot == NULL) {
        if (page_notebook(page) != NULL) {
            *slot = page; /* back with the editor panel or the terminal panes */
        } else {
            insert_page(app, rp);
        }
    }
    if (page != NULL) g_object_unref(page);
}

GtkWidget* code_refs_detach_search(AppData* app) {
    return app != NULL ? detach_slot(app, &app->search_page) : NULL;
}

void code_refs_attach_search(AppData* app, GtkWidget* page) {
    if (app != NULL) {
        attach_slot(app, &app->search_page, page);
    } else if (page != NULL) {
        g_object_unref(page);
    }
}

static guint refs_serial = 0;

void code_refs_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    refs_serial++; /* a lookup still running is for the workspace left: dropped */
    GtkWidget* page = detach_slot(app, &app->refs_page);
    if (page == NULL) return;
    if (ws->refs_page != NULL) g_object_unref(ws->refs_page);
    ws->refs_page = page;
}

void code_refs_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->refs_page == NULL) return;
    GtkWidget* page = ws->refs_page;
    ws->refs_page = NULL;
    attach_slot(app, &app->refs_page, page);
}

void code_refs_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_slot(app, &app->refs_page);
        if (page != NULL) {
            remove_page(app, page); /* still in the editor panel shown */
            g_object_unref(page);
        }
    }
    g_clear_object(&ws->refs_page);
}

/* ------------------------------------------------------------------------- */
/* Requests                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    EditorTab* tab;
    guint serial;
    gchar* name;
    gchar* origin;
    CodeLang lang;
    GHashTable* texts;
    gchar* root;
    GPtrArray* refs;
    gboolean truncated;
    CodeNavMessageFunc message;
} RefsLookup;

static void lookup_free(gpointer p) {
    RefsLookup* rl = p;
    g_free(rl->name);
    g_free(rl->origin);
    if (rl->texts != NULL) g_hash_table_destroy(rl->texts);
    g_free(rl->root);
    if (rl->refs != NULL) g_ptr_array_unref(rl->refs);
    g_free(rl);
}

static void refs_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    RefsLookup* rl = data;
    rl->root = code_index_project_root(rl->origin);
    gboolean t1 = FALSE, t2 = FALSE;
    GPtrArray* files = code_index_project_files(rl->root, code_lang_family(rl->lang), cancellable, &t1);
    rl->refs = code_index_references(rl->name, rl->lang, rl->origin, rl->texts, files, cancellable, &t2);
    rl->truncated = t1 || t2;
    g_ptr_array_unref(files);
    g_task_return_boolean(task, TRUE);
}

static void refs_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    RefsLookup* rl = g_task_get_task_data(G_TASK(res));
    if (rl->serial != refs_serial || g_task_had_error(G_TASK(res))) return;
    show_references(rl->app, rl->tab, rl->message, rl->name, rl->refs, rl->root, _("whole words in the project"),
                    rl->truncated);
}

static gchar* buffer_text(GtkTextBuffer* b) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(b, &s, &e);
    return gtk_text_buffer_get_text(b, &s, &e, TRUE);
}

static void start_index_lookup(AppData* app, EditorTab* tab, const gchar* name, CodeNavMessageFunc message) {
    RefsLookup* rl = g_new0(RefsLookup, 1);
    rl->app = app;
    rl->tab = tab;
    rl->serial = ++refs_serial;
    rl->name = g_strdup(name);
    rl->origin = g_canonicalize_filename(tab->path, NULL);
    rl->lang = code_lang_from_name(tab->path);
    rl->message = message;
    rl->texts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t->path == NULL || t->buffer == NULL || (t != tab && !gtk_text_buffer_get_modified(t->buffer))) continue;
        g_hash_table_insert(rl->texts, g_canonicalize_filename(t->path, NULL), buffer_text(t->buffer));
    }
    GTask* task = g_task_new(NULL, NULL, refs_done, NULL);
    g_task_set_task_data(task, rl, lookup_free);
    g_task_run_in_thread(task, refs_thread);
    g_object_unref(task);
}

typedef struct {
    guint serial;
    gchar* name;
    CodeNavMessageFunc message;
} ServerRefs;

/* Line of path (open tab first) */
static gchar* read_line(AppData* app, const gchar* path, gint line) {
    EditorTab* t = editor_find_tab(app, path);
    if (t != NULL && t->buffer != NULL && line < gtk_text_buffer_get_line_count(t->buffer)) {
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_line(t->buffer, &s, line);
        e = s;
        if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
        return gtk_text_iter_get_slice(&s, &e);
    }
    return NULL;
}

static gint compare_refs(gconstpointer a, gconstpointer b, gpointer origin) {
    const CodeReference* x = *(CodeReference* const*)a;
    const CodeReference* y = *(CodeReference* const*)b;
    gboolean xo = strcmp(x->path, origin) == 0, yo = strcmp(y->path, origin) == 0;
    if (xo != yo) return xo ? -1 : 1;
    gint c = strcmp(x->path, y->path);
    if (c != 0) return c;
    return x->line != y->line ? x->line - y->line : x->column - y->column;
}

static void on_server_refs(AppData* app, EditorTab* tab, GPtrArray* locations, gpointer data) {
    ServerRefs* sr = data;
    if (sr->serial != refs_serial) {
        g_free(sr->name);
        g_free(sr);
        return;
    }
    if (locations == NULL) {
        /* The server could not answer: the index searches */
        start_index_lookup(app, tab, sr->name, sr->message);
        g_free(sr->name);
        g_free(sr);
        return;
    }
    GPtrArray* refs = g_ptr_array_new_with_free_func((GDestroyNotify)code_reference_free);
    GHashTable* file_lines = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_strfreev);
    for (guint i = 0; i < locations->len; i++) {
        LspLocation* loc = g_ptr_array_index(locations, i);
        CodeReference* r = g_new0(CodeReference, 1);
        r->path = g_canonicalize_filename(loc->path, NULL);
        r->line = loc->line;
        r->column = loc->column;
        r->text = read_line(app, r->path, r->line);
        if (r->text == NULL) {
            gchar** lines = g_hash_table_lookup(file_lines, r->path);
            if (lines == NULL) {
                gchar* data_text = NULL;
                g_file_get_contents(r->path, &data_text, NULL, NULL);
                lines = g_strsplit(data_text != NULL ? data_text : "", "\n", -1);
                g_free(data_text);
                g_hash_table_insert(file_lines, g_strdup(r->path), lines);
            }
            r->text = r->line < (gint)g_strv_length(lines) ? g_strdup(lines[r->line]) : g_strdup("");
        }
        for (gchar* c = r->text; *c != '\0'; c++) {
            if (*c == '\t' || *c == '\r') *c = ' ';
        }
        if (!g_utf8_validate(r->text, -1, NULL)) {
            gchar* valid = g_utf8_make_valid(r->text, -1);
            g_free(r->text);
            r->text = valid;
        }
        g_ptr_array_add(refs, r);
    }
    g_hash_table_destroy(file_lines);
    gchar* origin = g_canonicalize_filename(tab->path, NULL);
    /* Same order as the index: this file first, then by path and line */
    g_ptr_array_sort_with_data(refs, compare_refs, origin);
    gchar* root = code_index_project_root(origin);
    gchar* server = code_intel_server_label(app, tab);
    show_references(app, tab, sr->message, sr->name, refs, root, server != NULL ? server : _("language server"), FALSE);
    g_free(server);
    g_free(root);
    g_free(origin);
    g_ptr_array_unref(refs);
    g_free(sr->name);
    g_free(sr);
}

gboolean code_refs_find(AppData* app, EditorTab* tab, CodeNavMessageFunc message) {
    if (app == NULL || tab == NULL || tab->buffer == NULL) return FALSE;
    if (!code_nav_is_code(tab)) {
        if (message != NULL) message(tab, _("No code navigation for this file type"));
        return FALSE;
    }
    GtkTextIter it, s, e;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    s = it;
    gtk_text_iter_set_line_offset(&s, 0);
    e = s;
    if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
    gchar* line = gtk_text_iter_get_slice(&s, &e);
    gint ws = 0, we = 0;
    if (!code_word_at(line, -1, gtk_text_iter_get_line_index(&it), code_lang_from_name(tab->path), &ws, &we)) {
        g_free(line);
        if (message != NULL) message(tab, _("No identifier under the cursor"));
        return FALSE;
    }
    gchar* name = g_strndup(line + ws, (gsize)(we - ws));
    g_free(line);
    if (message != NULL) {
        gchar* msg = g_strdup_printf(_("Searching the references of %s..."), name);
        message(tab, msg);
        g_free(msg);
    }
    ServerRefs* sr = g_new0(ServerRefs, 1);
    sr->serial = ++refs_serial;
    sr->name = g_strdup(name);
    sr->message = message;
    if (!code_intel_references(app, tab, gtk_text_iter_get_line(&it), ws, on_server_refs, sr)) {
        g_free(sr->name);
        g_free(sr);
        start_index_lookup(app, tab, name, message);
    }
    g_free(name);
    return TRUE;
}

/* page has the focus, or is the current page of the preview notebook, and shows */
static gboolean page_shown(AppData* app, GtkWidget* page) {
    if (page == NULL || app->notebook == NULL) return FALSE;
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(app->window) : NULL;
    gboolean focused = focus != NULL && (focus == page || gtk_widget_is_ancestor(focus, page));
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (!focused && gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, page)) return FALSE;
    return gtk_widget_get_mapped(page);
}

gboolean code_refs_close_shown(AppData* app) {
    if (app == NULL) return FALSE;
    GtkWidget* pages[] = {app->refs_page, app->search_page};
    for (gsize i = 0; i < G_N_ELEMENTS(pages); i++) {
        RefsPage* rp = page_state(pages[i]);
        if (rp != NULL && page_shown(app, pages[i])) {
            on_close_clicked(NULL, rp);
            return TRUE;
        }
    }
    return FALSE;
}

GtkWidget* code_refs_open(AppData* app, gboolean search) {
    if (app == NULL) return NULL;
    GtkWidget** slot = search ? &app->search_page : &app->refs_page;
    gboolean fresh = (*slot == NULL);
    RefsPage* rp = ensure_page(app, slot, search ? _("Search") : _("References"));
    if (rp == NULL) return NULL;
    if (fresh) {
        const gchar* empty = search ? _("No search yet — press Ctrl+Shift+F to search in files")
                                    : _("No references yet — press Shift+F12 on a name in an editor tab");
        gchar* markup = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", palette_color(PAL_FG_SUBTLE), empty);
        gtk_label_set_markup(GTK_LABEL(rp->header), markup);
        g_free(markup);
    }
    return rp->page;
}

void code_refs_close(AppData* app, gboolean search) {
    if (app == NULL) return;
    RefsPage* rp = page_state(search ? app->search_page : app->refs_page);
    if (rp != NULL) on_close_clicked(NULL, rp);
}
