/* Code navigation in editor tabs (see code_nav.h) */
#include "code_nav.h"
#include "code_index.h"
#include "code_scan.h"
#include "editor.h"
#include "jump_list.h"
#include <string.h>

#define CODE_NAV_HISTORY 100

typedef struct {
    JumpList* jumps;
    GCancellable* cancellable;  /* lookup in progress */
    guint serial;               /* of the latest lookup: older answers are dropped */
} CodeNav;

/* One lookup, from the request to the answer */
typedef struct {
    AppData* app;
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
    /* Answer */
    gchar* root;
    GPtrArray* matches;
    gboolean truncated;
    gboolean local_found;
    gint local_line, local_column;
} Lookup;

static CodeNav* nav_of(AppData* app) {
    if (app->code_nav == NULL) {
        CodeNav* nav = g_new0(CodeNav, 1);
        nav->jumps = jump_list_new(CODE_NAV_HISTORY);
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

static void lookup_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    Lookup* lk = data;
    lk->root = code_index_project_root(lk->origin);
    gboolean files_truncated = FALSE, find_truncated = FALSE;
    GPtrArray* files = code_index_project_files(lk->root, code_lang_family(lk->lang), cancellable,
                                                &files_truncated);
    CodeQuery q = {lk->name, lk->lang, lk->declaration, lk->origin, lk->texts};
    lk->matches = code_index_find(&q, files, cancellable, &find_truncated);
    lk->truncated = files_truncated || find_truncated;
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
        const gchar* what = target->symbol->declaration ? "Declaration" : "Definition";
        const gchar* path = shown_path(target->path, lk->root);
        if (dest == NULL) {
            msg = g_strdup_printf("%s of %s is in %s:%d, which cannot be opened in an editor tab", what,
                                  lk->name, path, target->symbol->line + 1);
        } else if (count > 1) {
            msg = g_strdup_printf("%s of %s (%s): %s:%d, 1 of %u%s", what, lk->name,
                                  code_symbol_kind_name(target->symbol->kind), path, target->symbol->line + 1,
                                  count, lk->truncated ? " (partial search)" : "");
        } else {
            msg = g_strdup_printf("%s of %s (%s): %s:%d%s", what, lk->name,
                                  code_symbol_kind_name(target->symbol->kind), path, target->symbol->line + 1,
                                  lk->truncated ? " (partial search)" : "");
        }
        say(lk->message, dest != NULL ? dest : lk->tab, msg);
        g_free(msg);
        return;
    }
    if (on_symbol) {
        gchar* msg = g_strdup_printf("%s is defined here (no other %s found)", lk->name,
                                     lk->declaration ? "declaration" : "definition");
        say(lk->message, lk->tab, msg);
        g_free(msg);
        return;
    }
    if (lk->local_found) {
        EditorTab* dest = jump(app, lk->tab, lk->origin, lk->local_line, lk->local_column);
        gchar* msg = g_strdup_printf("No definition of %s found: first use in this scope", lk->name);
        say(lk->message, dest != NULL ? dest : lk->tab, msg);
        g_free(msg);
        return;
    }
    gchar* msg = g_strdup_printf("No definition found for %s%s", lk->name,
                                 lk->truncated ? " (partial search: project too large)" : "");
    say(lk->message, lk->tab, msg);
    g_free(msg);
}

static void start_lookup(AppData* app, EditorTab* tab, const gchar* name, gboolean declaration,
                         gboolean at_cursor, gint line, gint column, CodeNavMessageFunc message) {
    CodeNav* nav = nav_of(app);
    if (nav->cancellable != NULL) {
        g_cancellable_cancel(nav->cancellable);
        g_clear_object(&nav->cancellable);
    }
    nav->cancellable = g_cancellable_new();

    Lookup* lk = g_new0(Lookup, 1);
    lk->app = app;
    lk->serial = ++nav->serial;
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

    GTask* task = g_task_new(NULL, nav->cancellable, lookup_done, NULL);
    g_task_set_task_data(task, lk, lookup_free);
    g_task_run_in_thread(task, lookup_thread);
    g_object_unref(task);
}

gboolean code_nav_goto_definition(AppData* app, EditorTab* tab, gboolean declaration,
                                  CodeNavMessageFunc message) {
    if (app == NULL || tab == NULL || tab->buffer == NULL) return FALSE;
    if (!code_nav_is_code(tab)) {
        say(message, tab, "No code navigation for this file type");
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
        say(message, tab, "No identifier under the cursor");
        return FALSE;
    }
    gchar* name = g_strndup(text + ws, (gsize)(we - ws));
    start_lookup(app, tab, name, declaration, TRUE, gtk_text_iter_get_line(&it), ws, message);
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
        say(message, tab, "Argument required: the name to look up");
    } else if (!code_nav_is_code(tab)) {
        say(message, tab, "No code navigation for this file type");
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
            say(message, tab, back ? "No previous location" : "No next location");
            g_free(path);
            return FALSE;
        }
        gchar* where = g_strdup(loc->path);
        gint where_line = loc->line;
        if (show_location(app, loc)) {
            EditorTab* dest = editor_find_tab(app, where);
            gchar* base = g_path_get_basename(where);
            gchar* msg = g_strdup_printf("%s %s:%d", back ? "Back to" : "Forward to", base, where_line + 1);
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
