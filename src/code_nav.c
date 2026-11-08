/* Code navigation in editor tabs (see code_nav.h) */
#include "code_nav.h"
#include "preview_state.h"
#include "core/app_paths.h"
#include "code_index.h"
#include "code_intel.h"
#include "code_tags.h"
#include "code_scan.h"
#include "editor.h"
#include "core/file_types.h"
#include "core/i18n.h"
#include "jump_list.h"
#include <string.h>

#define CODE_NAV_HISTORY 100

typedef struct CodeNav {
    JumpList* jumps;
    GCancellable* cancellable;  /* lookup in progress */
    guint serial;               /* of the latest lookup: older answers are dropped */
    GCancellable* find_cancellable; /* code_nav_find() in progress */
    guint find_serial;
    GMutex tags_lock;           /* guards tags (workers create indexes) */
    GHashTable* tags;           /* project root -> CodeTagsIndex* */
} CodeNav;

/* One lookup, from the request to the answer */
typedef struct {
    AppData* app;
    CodeNav* nav;
    guint serial;
    EditorTab* tab;          /* origin tab (checked again on the answer) */
    gchar* origin;           /* canonical path of the origin file */
    gchar* name;
    CodeLang lang;
    gboolean declaration;
    gboolean at_cursor;      /* the name is the word under the cursor */
    gint line, column;       /* cursor: line and byte index of the word start */
    gchar* text;             /* origin buffer */
    GHashTable* texts;       /* path -> text of modified open buffers, origin included */
    CodeNavMessageFunc message;
    CodeNavFoundFunc found;  /* code_nav_find(): report instead of jumping */
    gpointer found_data;
    /* Answer */
    gchar* root;
    GPtrArray* matches;
    gboolean truncated;
    gboolean local_found;
    gint local_line, local_column;
    gchar* doc;              /* code_nav_find(): documentation of the best match */
} Lookup;

static CodeNav* nav_of(AppData* app) {
    if (app->code_nav == NULL) {
        CodeNav* nav = g_new0(CodeNav, 1);
        nav->jumps = jump_list_new(CODE_NAV_HISTORY);
        g_mutex_init(&nav->tags_lock);
        nav->tags = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)code_tags_index_free);
        app->code_nav = nav;
    }
    return app->code_nav;
}

static void lookup_free(gpointer p) {
    Lookup* lk = p;
    g_free(lk->origin);
    g_free(lk->name);
    g_free(lk->text);
    if (lk->texts != NULL) g_hash_table_destroy(lk->texts);
    g_free(lk->root);
    g_free(lk->doc);
    if (lk->matches != NULL) g_ptr_array_unref(lk->matches);
    g_free(lk);
}

static void say(CodeNavMessageFunc message, EditorTab* tab, const gchar* text) {
    if (message != NULL && tab != NULL) message(tab, text);
}

static gboolean tab_alive(AppData* app, EditorTab* tab) {
    return tab != NULL && g_list_find(app->editors, tab) != NULL;
}

static gchar* buffer_text(GtkTextBuffer* buffer) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(buffer, &s, &e);
    return gtk_text_buffer_get_text(buffer, &s, &e, TRUE);
}

static CodeLang tab_lang(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL || tab->mode == EDITOR_MODE_ORGMODE) return CODE_LANG_NONE;
    const gchar* name = tab->path != NULL ? tab->path : tab->name;
    CodeLang lang = code_lang_from_name(name);
    if (lang != CODE_LANG_NONE) return lang;
    /* Scripts without extension: the #! line */
    GtkTextIter s, e;
    gtk_text_buffer_get_start_iter(tab->buffer, &s);
    e = s;
    gtk_text_iter_forward_to_line_end(&e);
    gchar* first = gtk_text_iter_get_slice(&s, &e);
    lang = code_lang_detect(name, first, -1);
    g_free(first);
    return lang;
}

const gchar* code_nav_backend_label(EditorTab* tab) {
    if (!code_nav_is_code(tab)) return NULL;
    return code_tags_program() != NULL ? "ctags" : _("built-in index");
}

gboolean code_nav_is_code(EditorTab* tab) {
    return tab != NULL && tab->path != NULL && tab_lang(tab) != CODE_LANG_NONE;
}

/* Cursor of a tab: line and byte index */
static void cursor_of(EditorTab* tab, gint* line, gint* index) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    *line = gtk_text_iter_get_line(&it);
    *index = gtk_text_iter_get_line_index(&it);
}

/* Path shown in messages: relative to the project root when inside it */
static const gchar* shown_path(const gchar* path, const gchar* root) {
    gsize n = root != NULL ? strlen(root) : 0;
    if (n > 0 && strncmp(path, root, n) == 0 && path[n] == '/') return path + n + 1;
    return path;
}

/* Jump from the tab's cursor to path:line:index, remembered in the history;
 * NULL when the file cannot be opened (too large, unreadable) */
static EditorTab* jump(AppData* app, EditorTab* from, const gchar* path, gint line, gint index) {
    CodeNav* nav = nav_of(app);
    gchar* origin = from != NULL && from->path != NULL ? g_canonicalize_filename(from->path, NULL) : NULL;
    gint l = 0, c = 0;
    if (origin != NULL) cursor_of(from, &l, &c);
    EditorTab* dest = editor_open_at(app, path, line, index);
    if (dest != NULL && origin != NULL) jump_list_push(nav->jumps, origin, l, c);
    g_free(origin);
    return dest;
}

/* The ctags index of a project (created on first use), or NULL without
 * ctags. Called from workers. */
static CodeTagsIndex* tags_for(CodeNav* nav, const gchar* root) {
    const gchar* program = code_tags_program();
    if (program == NULL || root == NULL) return NULL;
    g_mutex_lock(&nav->tags_lock);
    CodeTagsIndex* index = g_hash_table_lookup(nav->tags, root);
    if (index == NULL) {
        gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_SHA1, root, -1);
        gchar* name = g_strdup_printf("%s.tags", sum);
        gchar* cache_dir = app_cache_dir();
        gchar* cache = g_build_filename(cache_dir, "tags", name, NULL);
        g_free(cache_dir);
        index = code_tags_index_new(program, cache);
        g_hash_table_insert(nav->tags, g_strdup(root), index);
        g_free(cache);
        g_free(name);
        g_free(sum);
    }
    g_mutex_unlock(&nav->tags_lock);
    return index;
}

static void on_preview_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    AppData* app = data;
    GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    if (n_press != 1 || (mods & GDK_CONTROL_MASK) == 0 || (mods & (GDK_SHIFT_MASK | GDK_ALT_MASK)) != 0) return;
    /* Only a source file shown as text (not rendered, not a summary) */
    if (preview_state_path(app) == NULL || preview_state_kind(app) != FILE_KIND_TEXT ||
        code_lang_from_name(preview_state_path(app)) == CODE_LANG_NONE) {
        return;
    }
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    gint bx = 0, by = 0;
    gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_WIDGET, (gint)x, (gint)y, &bx, &by);
    GtkTextIter it;
    if (!gtk_text_view_get_iter_at_location(view, &it, bx, by)) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gint line = gtk_text_iter_get_line(&it);
    gint index = gtk_text_iter_get_line_index(&it);
    gchar* path = g_strdup(preview_state_path(app));
    EditorTab* tab = editor_open_at(app, path, line, index);
    g_free(path);
    CodeNavMessageFunc message = g_object_get_data(G_OBJECT(gesture), "code-nav-message");
    if (tab != NULL) code_nav_goto_definition(app, tab, FALSE, message);
}

void code_nav_attach_preview(AppData* app, CodeNavMessageFunc message) {
    if (app == NULL || app->text_view == NULL) return;
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_object_set_data(G_OBJECT(click), "code-nav-message", (gpointer)message);
    g_signal_connect(click, "pressed", G_CALLBACK(on_preview_click), app);
    gtk_widget_add_controller(app->text_view, GTK_EVENT_CONTROLLER(click));
}

EditorTab* code_nav_jump(AppData* app, EditorTab* from, const gchar* path, gint line, gint index) {
    if (app == NULL || path == NULL) return NULL;
    return jump(app, from, path, line, index);
}

static void lookup_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    Lookup* lk = data;
    lk->root = code_index_project_root(lk->origin);
    gboolean files_truncated = FALSE, find_truncated = FALSE;
    GPtrArray* files = code_index_project_files(lk->root, code_lang_family(lk->lang), cancellable,
                                                &files_truncated);
    CodeQuery q = {lk->name, lk->lang, lk->declaration, lk->origin, lk->texts, tags_for(lk->nav, lk->root)};
    lk->matches = code_index_find(&q, files, cancellable, &find_truncated);
    lk->truncated = files_truncated || find_truncated;
    if (lk->found != NULL && lk->matches->len > 0) {
        /* Documentation comment of the best match, from its buffer or file */
        CodeMatch* best = g_ptr_array_index(lk->matches, 0);
        const gchar* text = g_hash_table_lookup(lk->texts, best->path);
        gchar* owned = NULL;
        gsize len = 0;
        if (text != NULL) len = strlen(text);
        else if (g_file_get_contents(best->path, &owned, &len, NULL)) text = owned;
        if (text != NULL) {
            CodeLang lang = code_lang_from_name(best->path);
            lk->doc = code_scan_doc_comment(text, len, lang != CODE_LANG_NONE ? lang : lk->lang, best->symbol->line);
        }
        g_free(owned);
    }
    if (lk->at_cursor) {
        lk->local_found = code_scan_local_definition(lk->text, strlen(lk->text), lk->lang, lk->name, lk->line,
                                                     lk->column, &lk->local_line, &lk->local_column);
    }
    g_ptr_array_unref(files);
    g_task_return_boolean(task, TRUE);
}

static void lookup_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    GTask* task = G_TASK(res);
    Lookup* lk = g_task_get_task_data(task);
    AppData* app = lk->app;
    CodeNav* nav = nav_of(app);
    if (lk->found != NULL) {
        if (lk->serial != nav->find_serial || g_task_had_error(task) || !tab_alive(app, lk->tab)) return;
        g_clear_object(&nav->find_cancellable);
        lk->found(app, lk->tab, lk->matches->len > 0 ? g_ptr_array_index(lk->matches, 0) : NULL, lk->doc, lk->root,
                  lk->found_data);
        return;
    }
    /* A newer lookup replaced this one, or the tab is gone */
    if (lk->serial != nav->serial || g_task_had_error(task) || !tab_alive(app, lk->tab)) return;
    g_clear_object(&nav->cancellable);

    /* The symbol under the cursor itself is not a destination */
    CodeMatch* target = NULL;
    guint count = 0;
    gboolean on_symbol = FALSE;
    for (guint i = 0; i < lk->matches->len; i++) {
        CodeMatch* m = g_ptr_array_index(lk->matches, i);
        if (lk->at_cursor && strcmp(m->path, lk->origin) == 0 && m->symbol->line == lk->line &&
            m->symbol->column == lk->column) {
            on_symbol = TRUE;
            continue;
        }
        if (target == NULL) target = m;
        count++;
    }

    if (target != NULL) {
        EditorTab* dest = jump(app, lk->tab, target->path, target->symbol->line, target->symbol->column);
        gchar* msg;
        gboolean decl = target->symbol->declaration;
        const gchar* path = shown_path(target->path, lk->root);
        const gchar* kind = code_symbol_kind_name(target->symbol->kind);
        gint line = target->symbol->line + 1;
        if (dest == NULL) {
            msg = g_strdup_printf(decl ? _("Declaration of %1$s is in %2$s:%3$d, which cannot be opened in an editor tab")
                                       : _("Definition of %1$s is in %2$s:%3$d, which cannot be opened in an editor tab"),
                                  lk->name, path, line);
        } else if (count > 1) {
            msg = g_strdup_printf(decl ? _("Declaration of %1$s (%2$s): %3$s:%4$d, 1 of %5$u")
                                       : _("Definition of %1$s (%2$s): %3$s:%4$d, 1 of %5$u"),
                                  lk->name, kind, path, line, count);
        } else {
            msg = g_strdup_printf(decl ? _("Declaration of %1$s (%2$s): %3$s:%4$d")
                                       : _("Definition of %1$s (%2$s): %3$s:%4$d"),
                                  lk->name, kind, path, line);
        }
        if (dest != NULL && lk->truncated) {
            /* TRANSLATORS: appended to a "Definition of ..." message */
            gchar* full = g_strconcat(msg, _(" (partial search)"), NULL);
            g_free(msg);
            msg = full;
        }
        say(lk->message, dest != NULL ? dest : lk->tab, msg);
        g_free(msg);
        return;
    }
    if (on_symbol) {
        gchar* msg = g_strdup_printf(lk->declaration ? _("%s is defined here (no other declaration found)")
                                                     : _("%s is defined here (no other definition found)"),
                                     lk->name);
        say(lk->message, lk->tab, msg);
        g_free(msg);
        return;
    }
    if (lk->local_found) {
        EditorTab* dest = jump(app, lk->tab, lk->origin, lk->local_line, lk->local_column);
        gchar* msg = g_strdup_printf(_("No definition of %s found: first use in this scope"), lk->name);
        say(lk->message, dest != NULL ? dest : lk->tab, msg);
        g_free(msg);
        return;
    }
    gchar* msg = g_strdup_printf(lk->truncated ? _("No definition found for %s (partial search: project too large)")
                                               : _("No definition found for %s"),
                                 lk->name);
    say(lk->message, lk->tab, msg);
    g_free(msg);
}

static void run_lookup(AppData* app, EditorTab* tab, const gchar* name, gboolean declaration, gboolean at_cursor,
                       gint line, gint column, CodeNavMessageFunc message, CodeNavFoundFunc found,
                       gpointer found_data) {
    CodeNav* nav = nav_of(app);
    GCancellable** cancellable = found != NULL ? &nav->find_cancellable : &nav->cancellable;
    if (*cancellable != NULL) {
        g_cancellable_cancel(*cancellable);
        g_clear_object(cancellable);
    }
    *cancellable = g_cancellable_new();

    Lookup* lk = g_new0(Lookup, 1);
    lk->app = app;
    lk->nav = nav;
    lk->serial = found != NULL ? ++nav->find_serial : ++nav->serial;
    lk->found = found;
    lk->found_data = found_data;
    lk->tab = tab;
    lk->origin = g_canonicalize_filename(tab->path, NULL);
    lk->name = g_strdup(name);
    lk->lang = tab_lang(tab);
    lk->declaration = declaration;
    lk->at_cursor = at_cursor;
    lk->line = line;
    lk->column = column;
    lk->text = buffer_text(tab->buffer);
    lk->message = message;
    /* Unsaved buffers are searched as edited */
    lk->texts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_insert(lk->texts, g_strdup(lk->origin), g_strdup(lk->text));
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t == tab || t->path == NULL || t->buffer == NULL || !gtk_text_buffer_get_modified(t->buffer)) continue;
        g_hash_table_insert(lk->texts, g_canonicalize_filename(t->path, NULL), buffer_text(t->buffer));
    }

    GTask* task = g_task_new(NULL, *cancellable, lookup_done, NULL);
    g_task_set_task_data(task, lk, lookup_free);
    g_task_run_in_thread(task, lookup_thread);
    g_object_unref(task);
}

static void start_lookup(AppData* app, EditorTab* tab, const gchar* name, gboolean declaration,
                         gboolean at_cursor, gint line, gint column, CodeNavMessageFunc message) {
    run_lookup(app, tab, name, declaration, at_cursor, line, column, message, NULL, NULL);
}

gboolean code_nav_find(AppData* app, EditorTab* tab, const gchar* name, CodeNavFoundFunc func, gpointer data) {
    if (app == NULL || tab == NULL || tab->buffer == NULL || name == NULL || func == NULL ||
        !code_nav_is_code(tab)) {
        return FALSE;
    }
    gint line = 0, col = 0;
    cursor_of(tab, &line, &col);
    run_lookup(app, tab, name, FALSE, FALSE, line, col, NULL, func, data);
    return TRUE;
}

/* A definition asked to the language server (the index answers when it
 * cannot) */
typedef struct {
    guint serial;
    gchar* name;
    gboolean declaration;
    gint line, start, end;   /* the word under the cursor */
    gchar* origin;
    CodeNavMessageFunc message;
} ServerLookup;

static void server_lookup_free(ServerLookup* sl) {
    g_free(sl->name);
    g_free(sl->origin);
    g_free(sl);
}

static void on_server_definition(AppData* app, EditorTab* tab, GPtrArray* locations, gpointer data) {
    ServerLookup* sl = data;
    CodeNav* nav = nav_of(app);
    if (sl->serial != nav->serial) {
        server_lookup_free(sl);
        return;
    }
    /* The word under the cursor is not a destination */
    LspLocation* target = NULL;
    guint count = 0;
    for (guint i = 0; locations != NULL && i < locations->len; i++) {
        LspLocation* loc = g_ptr_array_index(locations, i);
        gchar* canon = g_canonicalize_filename(loc->path, NULL);
        gboolean here = strcmp(canon, sl->origin) == 0 && loc->line == sl->line && loc->column >= sl->start &&
                        loc->column <= sl->end;
        g_free(canon);
        if (here) continue;
        if (target == NULL) target = loc;
        count++;
    }
    if (target == NULL) {
        /* Nothing (or only here): the index looks for other candidates */
        start_lookup(app, tab, sl->name, sl->declaration, TRUE, sl->line, sl->start, sl->message);
        server_lookup_free(sl);
        return;
    }
    gchar* root = code_index_project_root(sl->origin);
    gchar* server = code_intel_server_label(app, tab);
    EditorTab* dest = jump(app, tab, target->path, target->line, target->column);
    gboolean decl = sl->declaration;
    const gchar* path = shown_path(target->path, root);
    const gchar* by = server != NULL ? server : _("language server");
    gchar* msg;
    if (dest == NULL) {
        msg = g_strdup_printf(decl ? _("Declaration of %1$s is in %2$s:%3$d, which cannot be opened in an editor tab")
                                   : _("Definition of %1$s is in %2$s:%3$d, which cannot be opened in an editor tab"),
                              sl->name, path, target->line + 1);
    } else if (count > 1) {
        /* TRANSLATORS: the last %s is the language server, e.g. clangd */
        msg = g_strdup_printf(decl ? _("Declaration of %1$s: %2$s:%3$d, 1 of %4$u (%5$s)")
                                   : _("Definition of %1$s: %2$s:%3$d, 1 of %4$u (%5$s)"),
                              sl->name, path, target->line + 1, count, by);
    } else {
        /* TRANSLATORS: the last %s is the language server, e.g. clangd */
        msg = g_strdup_printf(decl ? _("Declaration of %1$s: %2$s:%3$d (%4$s)") : _("Definition of %1$s: %2$s:%3$d (%4$s)"),
                              sl->name, path, target->line + 1, by);
    }
    say(sl->message, dest != NULL ? dest : tab, msg);
    g_free(msg);
    g_free(server);
    g_free(root);
    server_lookup_free(sl);
}

gboolean code_nav_goto_definition(AppData* app, EditorTab* tab, gboolean declaration,
                                  CodeNavMessageFunc message) {
    if (app == NULL || tab == NULL || tab->buffer == NULL) return FALSE;
    if (!code_nav_is_code(tab)) {
        say(message, tab, _("No code navigation for this file type"));
        return FALSE;
    }
    GtkTextIter it, start, end;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    start = it;
    gtk_text_iter_set_line_offset(&start, 0);
    end = start;
    if (!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
    gchar* text = gtk_text_iter_get_slice(&start, &end);
    gint ws = 0, we = 0;
    gboolean found = code_word_at(text, -1, gtk_text_iter_get_line_index(&it), tab_lang(tab), &ws, &we);
    if (!found) {
        g_free(text);
        say(message, tab, _("No identifier under the cursor"));
        return FALSE;
    }
    gchar* name = g_strndup(text + ws, (gsize)(we - ws));
    gint line = gtk_text_iter_get_line(&it);
    /* The language server first, when one serves this file */
    CodeNav* nav = nav_of(app);
    if (nav->cancellable != NULL) {
        g_cancellable_cancel(nav->cancellable);
        g_clear_object(&nav->cancellable);
    }
    ServerLookup* sl = g_new0(ServerLookup, 1);
    sl->serial = ++nav->serial;
    sl->name = g_strdup(name);
    sl->declaration = declaration;
    sl->line = line;
    sl->start = ws;
    sl->end = we;
    sl->origin = g_canonicalize_filename(tab->path, NULL);
    sl->message = message;
    if (!code_intel_definition(app, tab, line, ws, declaration, on_server_definition, sl)) {
        server_lookup_free(sl);
        start_lookup(app, tab, name, declaration, TRUE, line, ws, message);
    }
    g_free(name);
    g_free(text);
    return TRUE;
}

gboolean code_nav_goto_symbol(AppData* app, EditorTab* tab, const gchar* name, gboolean declaration,
                              CodeNavMessageFunc message) {
    if (app == NULL || tab == NULL || tab->buffer == NULL) return FALSE;
    gchar* clean = g_strstrip(g_strdup(name != NULL ? name : ""));
    gboolean ok = FALSE;
    if (clean[0] == '\0') {
        say(message, tab, _("Argument required: the name to look up"));
    } else if (!code_nav_is_code(tab)) {
        say(message, tab, _("No code navigation for this file type"));
    } else {
        gint line = 0, col = 0;
        cursor_of(tab, &line, &col);
        start_lookup(app, tab, clean, declaration, FALSE, line, col, message);
        ok = TRUE;
    }
    g_free(clean);
    return ok;
}

/* Show a history location; FALSE when its file cannot be opened */
static gboolean show_location(AppData* app, const JumpLocation* loc) {
    return editor_open_at(app, loc->path, loc->line, loc->column) != NULL;
}

static gboolean walk_history(AppData* app, EditorTab* tab, gboolean back, CodeNavMessageFunc message) {
    CodeNav* nav = nav_of(app);
    gchar* path = NULL;
    gint line = 0, col = 0;
    if (tab != NULL && tab->path != NULL && tab->buffer != NULL) {
        path = g_canonicalize_filename(tab->path, NULL);
        cursor_of(tab, &line, &col);
    }
    for (;;) {
        const JumpLocation* loc = back ? jump_list_back(nav->jumps, path, line, col) : jump_list_forward(nav->jumps);
        if (loc == NULL) {
            say(message, tab, back ? _("No previous location") : _("No next location"));
            g_free(path);
            return FALSE;
        }
        gchar* where = g_strdup(loc->path);
        gint where_line = loc->line;
        if (show_location(app, loc)) {
            EditorTab* dest = editor_find_tab(app, where);
            gchar* base = g_path_get_basename(where);
            gchar* msg = g_strdup_printf(back ? _("Back to %1$s:%2$d") : _("Forward to %1$s:%2$d"), base, where_line + 1);
            say(message, dest, msg);
            g_free(msg);
            g_free(base);
            g_free(where);
            g_free(path);
            return TRUE;
        }
        /* The file is gone: forget it and try the next location */
        jump_list_forget_file(nav->jumps, where);
        g_free(where);
    }
}

gboolean code_nav_back(AppData* app, EditorTab* tab, CodeNavMessageFunc message) {
    if (app == NULL) return FALSE;
    return walk_history(app, tab, TRUE, message);
}

gboolean code_nav_forward(AppData* app, EditorTab* tab, CodeNavMessageFunc message) {
    if (app == NULL) return FALSE;
    return walk_history(app, tab, FALSE, message);
}
