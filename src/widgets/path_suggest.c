/* Path suggestions under a path field (see path_suggest.h) */
#include "widgets/path_suggest.h"
#include "core/folder_complete.h"
#include "theme/icons.h"
#include "theme/palette.h"

#include <string.h>

#define SUGGEST_KEY "lookin-path-suggest"
#define MAX_LISTED 5000 /* entries read from one folder */

typedef struct {
    gchar* name;
    gboolean dir;
} Entry;

typedef struct {
    PathSuggestKind kind;
    PathSuggestExtra extra;
    gpointer extra_data;
    GtkWidget* entry;
    GtkWidget* popover;
    GtkWidget* scroll;
    GtkWidget* list;
    GtkStringList* shown; /* field texts of the suggestions */
    GtkSingleSelection* selection;
    GtkEventController* focus;
    GCancellable* cancel; /* listing of listed_dir */
    gchar* listed_dir;
    GArray* entries;      /* Entry of listed_dir, NULL while read */
    gboolean active;      /* suggestions asked for (typing, Ctrl+Space) */
    gboolean quiet;       /* the text is being set here */
} Suggest;

static void entry_clear(gpointer data) {
    g_free(((Entry*)data)->name);
}

static GArray* entries_new(void) {
    GArray* a = g_array_new(FALSE, FALSE, sizeof(Entry));
    g_array_set_clear_func(a, entry_clear);
    return a;
}

static void forget_listing(Suggest* s) {
    if (s->cancel != NULL) {
        g_cancellable_cancel(s->cancel);
        g_clear_object(&s->cancel);
    }
    g_clear_pointer(&s->listed_dir, g_free);
    g_clear_pointer(&s->entries, g_array_unref);
}

static void refresh(Suggest* s);

/* A suggestion that is a folder: all of them for FOLDERS, those ending with
 * '/' for PROGRAMS */
static gboolean is_folder_text(const Suggest* s, const gchar* text) {
    return s->kind == PATH_SUGGEST_FOLDERS || g_str_has_suffix(text, "/");
}

/* ------------------------------------------------------------------------- */
/* Reading the folder in the background                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    Suggest* s; /* only touched when not cancelled */
    GCancellable* cancel;
    PathSuggestKind kind;
    GArray* entries;
} Listing;

static void listing_free(Listing* l) {
    g_object_unref(l->cancel);
    g_array_unref(l->entries);
    g_free(l);
}

static void listing_finish(Listing* l) {
    if (!g_cancellable_is_cancelled(l->cancel)) {
        Suggest* s = l->s;
        s->entries = g_array_ref(l->entries);
        refresh(s);
    }
    listing_free(l);
}

static void on_next_files(GObject* source, GAsyncResult* res, gpointer data) {
    Listing* l = data;
    GFileEnumerator* en = G_FILE_ENUMERATOR(source);
    GList* infos = g_file_enumerator_next_files_finish(en, res, NULL);
    for (GList* i = infos; i != NULL; i = i->next) {
        GFileInfo* info = i->data;
        GFileType type = g_file_info_get_file_type(info);
        gboolean dir = type == G_FILE_TYPE_DIRECTORY;
        gboolean program = l->kind == PATH_SUGGEST_PROGRAMS && type == G_FILE_TYPE_REGULAR &&
                           g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE);
        if (dir || program) {
            Entry e = { g_strdup(g_file_info_get_name(info)), dir };
            g_array_append_val(l->entries, e);
        }
    }
    gboolean more = infos != NULL && l->entries->len < MAX_LISTED && !g_cancellable_is_cancelled(l->cancel);
    g_list_free_full(infos, g_object_unref);
    if (more) {
        g_file_enumerator_next_files_async(en, 200, G_PRIORITY_DEFAULT, l->cancel, on_next_files, l);
        return;
    }
    g_file_enumerator_close_async(en, G_PRIORITY_DEFAULT, NULL, NULL, NULL);
    g_object_unref(en);
    listing_finish(l);
}

static void on_enumerated(GObject* source, GAsyncResult* res, gpointer data) {
    Listing* l = data;
    GFileEnumerator* en = g_file_enumerate_children_finish(G_FILE(source), res, NULL);
    if (en == NULL || g_cancellable_is_cancelled(l->cancel)) {
        if (en != NULL) g_object_unref(en);
        listing_finish(l); /* unreadable: no entry */
        return;
    }
    g_file_enumerator_next_files_async(en, 200, G_PRIORITY_DEFAULT, l->cancel, on_next_files, l);
}

static void start_listing(Suggest* s, const gchar* dir) {
    forget_listing(s);
    s->listed_dir = g_strdup(dir);
    s->cancel = g_cancellable_new();
    Listing* l = g_new0(Listing, 1);
    l->s = s;
    l->cancel = g_object_ref(s->cancel);
    l->kind = s->kind;
    l->entries = entries_new();
    GFile* file = g_file_new_for_path(dir);
    /* Links followed: a venv's bin/python is a link to the real program */
    g_file_enumerate_children_async(file,
                                    G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE
                                                                   "," G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE,
                                    G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT, s->cancel, on_enumerated, l);
    g_object_unref(file);
}

/* ------------------------------------------------------------------------- */
/* The suggestions                                                           */
/* ------------------------------------------------------------------------- */

typedef struct {
    gint score;
    gboolean python; /* PROGRAMS: a Python program, before the others */
    const Entry* entry;
} Scored;

static gint compare_scored(gconstpointer a, gconstpointer b) {
    const Scored* x = a;
    const Scored* y = b;
    if (x->score != y->score) return y->score > x->score ? 1 : -1;
    if (x->python != y->python) return x->python ? -1 : 1;
    return g_utf8_collate(x->entry->name, y->entry->name);
}

static void add_unique(GPtrArray* out, GHashTable* seen, gchar* text) {
    if (out->len >= PATH_SUGGEST_MAX || g_hash_table_contains(seen, text)) {
        g_free(text);
        return;
    }
    g_hash_table_add(seen, text);
    g_ptr_array_add(out, text);
}

/* The entries of the folder typed matching the name started */
static void collect_path(Suggest* s, const gchar* text, const gchar* dir, const gchar* prefix, GPtrArray* out,
                         GHashTable* seen) {
    if (g_strcmp0(s->listed_dir, dir) != 0) start_listing(s, dir);
    if (s->entries == NULL) return;
    GArray* scored = g_array_new(FALSE, FALSE, sizeof(Scored));
    for (guint i = 0; i < s->entries->len; i++) {
        const Entry* e = &g_array_index(s->entries, Entry, i);
        Scored sc = { folder_complete_name_score(e->name, prefix),
                      s->kind == PATH_SUGGEST_PROGRAMS && !e->dir && folder_complete_is_python(e->name), e };
        if (sc.score >= 0) g_array_append_val(scored, sc);
    }
    g_array_sort(scored, compare_scored);
    for (guint i = 0; i < scored->len; i++) {
        const Entry* e = g_array_index(scored, Scored, i).entry;
        gchar* joined = folder_complete_join(text, e->name);
        if (e->dir && s->kind == PATH_SUGGEST_PROGRAMS) {
            gchar* slashed = g_strconcat(joined, "/", NULL);
            g_free(joined);
            joined = slashed;
        }
        add_unique(out, seen, joined);
    }
    g_array_unref(scored);
}

static void hide(Suggest* s) {
    s->active = FALSE;
    forget_listing(s); /* read again next time: the folders may change */
    if (gtk_widget_get_visible(s->popover)) gtk_popover_popdown(GTK_POPOVER(s->popover));
}

static void refresh(Suggest* s) {
    if (!s->active) return;
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(s->entry));
    GPtrArray* out = g_ptr_array_new();
    GHashTable* seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gchar* dir = NULL;
    gchar* prefix = NULL;
    gboolean is_path = folder_complete_split(text, g_get_home_dir(), &dir, &prefix);
    if (is_path) collect_path(s, text, dir, prefix, out, seen);
    g_free(dir);
    g_free(prefix);
    if (s->extra != NULL) {
        GPtrArray* extra = g_ptr_array_new();
        s->extra(text, is_path, extra, s->extra_data);
        for (guint i = 0; i < extra->len; i++) add_unique(out, seen, g_ptr_array_index(extra, i));
        g_ptr_array_unref(extra); /* the texts went to seen or were freed */
    }
    g_ptr_array_add(out, NULL);
    guint old = g_list_model_get_n_items(G_LIST_MODEL(s->shown));
    gtk_string_list_splice(s->shown, 0, old, (const gchar* const*)out->pdata);
    guint n = out->len - 1;
    g_ptr_array_unref(out);
    g_hash_table_destroy(seen); /* after the splice: it owns the texts */
    gtk_single_selection_set_selected(s->selection, GTK_INVALID_LIST_POSITION);
    if (n == 0) {
        if (gtk_widget_get_visible(s->popover)) gtk_popover_popdown(GTK_POPOVER(s->popover));
        return;
    }
    gtk_list_view_scroll_to(GTK_LIST_VIEW(s->list), 0, GTK_LIST_SCROLL_NONE, NULL);
    gtk_widget_set_size_request(s->scroll, MAX(gtk_widget_get_width(s->entry), 240), -1);
    if (!gtk_widget_get_visible(s->popover)) {
        gtk_popover_popup(GTK_POPOVER(s->popover));
    } else {
        /* The entry does not allocate the popover: lay it out again for
         * the rows now shown (else it keeps its first height) */
        gtk_popover_present(GTK_POPOVER(s->popover));
    }
}

/* Put the suggestion chosen (none: the first with drill) in the field. A
 * folder taken with drill (and with Enter for PROGRAMS: a folder is no
 * program) gets a '/' and lists its entries. */
static void accept(Suggest* s, gboolean drill) {
    guint pos = gtk_single_selection_get_selected(s->selection);
    if (pos == GTK_INVALID_LIST_POSITION && drill && g_list_model_get_n_items(G_LIST_MODEL(s->shown)) > 0) pos = 0;
    if (pos == GTK_INVALID_LIST_POSITION) return;
    const gchar* text = gtk_string_list_get_string(s->shown, pos);
    gboolean folder = is_folder_text(s, text);
    gboolean open = folder && (drill || s->kind == PATH_SUGGEST_PROGRAMS);
    gchar* set = open && !g_str_has_suffix(text, "/") ? g_strconcat(text, "/", NULL) : g_strdup(text);
    s->quiet = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(s->entry), set);
    gtk_editable_set_position(GTK_EDITABLE(s->entry), -1);
    s->quiet = FALSE;
    g_free(set);
    if (open) {
        refresh(s);
    } else {
        hide(s);
    }
}

static void move_selection(Suggest* s, gint delta) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(s->shown));
    if (n == 0) return;
    guint cur = gtk_single_selection_get_selected(s->selection);
    gint next = cur == GTK_INVALID_LIST_POSITION ? (delta > 0 ? 0 : -1) : (gint)cur + delta;
    if (next < 0) {
        /* Up past the first: back to the text typed */
        gtk_single_selection_set_selected(s->selection, GTK_INVALID_LIST_POSITION);
        return;
    }
    next = MIN(next, (gint)n - 1);
    gtk_single_selection_set_selected(s->selection, (guint)next);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(s->list), (guint)next, GTK_LIST_SCROLL_NONE, NULL);
}

/* ------------------------------------------------------------------------- */
/* Field and popover signals                                                 */
/* ------------------------------------------------------------------------- */

static void on_changed(GtkEditable* editable, gpointer data) {
    (void)editable;
    Suggest* s = data;
    if (s->quiet || !gtk_event_controller_focus_contains_focus(GTK_EVENT_CONTROLLER_FOCUS(s->focus))) return;
    s->active = TRUE;
    refresh(s);
}

static void on_focus_leave(GtkEventControllerFocus* focus, gpointer data) {
    (void)focus;
    hide(data);
}

static gboolean on_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                       gpointer data) {
    (void)ctrl;
    (void)keycode;
    Suggest* s = data;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    if (mods == GDK_CONTROL_MASK && keyval == GDK_KEY_space) {
        s->active = TRUE;
        refresh(s);
        return TRUE;
    }
    if (!gtk_widget_get_visible(s->popover)) return FALSE;
    if (mods == GDK_CONTROL_MASK) {
        switch (gdk_keyval_to_lower(keyval)) {
        case GDK_KEY_n: case GDK_KEY_j: move_selection(s, 1); return TRUE;
        case GDK_KEY_p: case GDK_KEY_k: move_selection(s, -1); return TRUE;
        default: return FALSE;
        }
    }
    if (mods != 0) return FALSE;
    switch (keyval) {
    case GDK_KEY_Down: case GDK_KEY_KP_Down: move_selection(s, 1); return TRUE;
    case GDK_KEY_Up: case GDK_KEY_KP_Up: move_selection(s, -1); return TRUE;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: move_selection(s, 8); return TRUE;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: move_selection(s, -8); return TRUE;
    case GDK_KEY_Tab: case GDK_KEY_KP_Tab: accept(s, TRUE); return TRUE;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_ISO_Enter:
        if (gtk_single_selection_get_selected(s->selection) != GTK_INVALID_LIST_POSITION) {
            accept(s, FALSE);
            return TRUE;
        }
        hide(s);
        return FALSE; /* the field's own Enter */
    case GDK_KEY_Escape: hide(s); return TRUE;
    default: return FALSE;
    }
}

static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    Suggest* s = data;
    gtk_single_selection_set_selected(s->selection, position);
    accept(s, FALSE);
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    Suggest* s = data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_box_append(GTK_BOX(box), s->kind == PATH_SUGGEST_FOLDERS ? icon_image_new("folder") : glyph_image_new("folder"));
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_START);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_item_set_child(item, box);
}

/* "name  parent", the parent dimmed; for PROGRAMS the icon of a folder, a
 * Python or another program */
static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    Suggest* s = data;
    const gchar* text = gtk_string_object_get_string(gtk_list_item_get_item(item));
    gboolean folder = is_folder_text(s, text);
    gchar* path = g_strdup(text);
    gsize len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';
    gchar* slash = strrchr(path, '/');
    const gchar* name = slash != NULL && slash[1] != '\0' ? slash + 1 : path;
    GtkWidget* row = gtk_list_item_get_child(item);
    if (s->kind == PATH_SUGGEST_PROGRAMS) {
        GtkWidget* image = gtk_widget_get_first_child(row);
        if (folder) {
            glyph_image_set(image, "folder");
        } else if (folder_complete_is_python(name)) {
            file_type_image_set(image, "python.py", FALSE);
        } else {
            glyph_image_set(image, "utilities-terminal");
        }
    }
    gchar* markup;
    if (slash == NULL || slash[1] == '\0') {
        markup = g_markup_printf_escaped("%s", path);
    } else {
        gchar* parent = slash == path ? g_strdup("/") : g_strndup(path, (gsize)(slash - path));
        markup = g_markup_printf_escaped("%s  <span foreground=\"%s\">%s</span>", slash + 1,
                                         palette_color(PAL_FG_SUBTLE), parent);
        g_free(parent);
    }
    gtk_label_set_markup(GTK_LABEL(gtk_widget_get_last_child(row)), markup);
    g_free(markup);
    g_free(path);
}

static void on_entry_destroy(GtkWidget* entry, gpointer data) {
    Suggest* s = data;
    g_object_set_data(G_OBJECT(entry), SUGGEST_KEY, NULL);
    forget_listing(s);
    gtk_widget_unparent(s->popover);
    g_free(s);
}

void path_suggest_attach(GtkWidget* entry, PathSuggestKind kind) {
    Suggest* s = g_new0(Suggest, 1);
    s->kind = kind;
    s->entry = entry;
    s->shown = gtk_string_list_new(NULL);
    s->selection = gtk_single_selection_new(G_LIST_MODEL(s->shown)); /* takes s->shown */
    gtk_single_selection_set_autoselect(s->selection, FALSE);
    gtk_single_selection_set_can_unselect(s->selection, TRUE);
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), s);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), s);
    s->list = gtk_list_view_new(GTK_SELECTION_MODEL(s->selection), factory);
    gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(s->list), TRUE);
    gtk_widget_set_can_focus(s->list, FALSE); /* the focus stays in the field */
    g_signal_connect(s->list, "activate", G_CALLBACK(on_activate), s);
    s->scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s->scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(s->scroll), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(s->scroll), 320);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s->scroll), s->list);

    s->popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(s->popover), s->scroll);
    gtk_popover_set_autohide(GTK_POPOVER(s->popover), FALSE);
    gtk_popover_set_has_arrow(GTK_POPOVER(s->popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(s->popover), GTK_POS_BOTTOM);
    gtk_widget_set_halign(s->popover, GTK_ALIGN_START);
    gtk_widget_set_can_focus(s->popover, FALSE);
    gtk_widget_set_parent(s->popover, entry);

    s->focus = gtk_event_controller_focus_new();
    g_signal_connect(s->focus, "leave", G_CALLBACK(on_focus_leave), s);
    gtk_widget_add_controller(entry, s->focus);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), s);
    gtk_widget_add_controller(entry, keys);
    g_signal_connect(entry, "changed", G_CALLBACK(on_changed), s);
    g_signal_connect(entry, "destroy", G_CALLBACK(on_entry_destroy), s);
    g_object_set_data(G_OBJECT(entry), SUGGEST_KEY, s);
}

void path_suggest_set_extra(GtkWidget* entry, PathSuggestExtra extra, gpointer data) {
    Suggest* s = g_object_get_data(G_OBJECT(entry), SUGGEST_KEY);
    if (s == NULL) return;
    s->extra = extra;
    s->extra_data = data;
}

gboolean path_suggest_is_open(GtkWidget* entry) {
    Suggest* s = entry != NULL ? g_object_get_data(G_OBJECT(entry), SUGGEST_KEY) : NULL;
    return s != NULL && gtk_widget_get_visible(s->popover);
}
