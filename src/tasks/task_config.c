/* The tasks a project defines itself (see task_config.h) */
#include "tasks/task_config.h"
#include "core/i18n.h"
#include <string.h>

#define TASK_CONFIG_ERROR (g_quark_from_static_string("task-config-error"))

typedef enum { VAL_STRING, VAL_BOOL, VAL_ARRAY, VAL_TABLE } ValKind;

typedef struct {
    ValKind kind;
    gchar* str;        /* VAL_STRING */
    gboolean boolean;  /* VAL_BOOL */
    GPtrArray* items;  /* VAL_ARRAY: gchar*; VAL_TABLE: "name=value" */
} Value;

typedef struct {
    const gchar* p;
    gint line;
    GError** error;
} Parser;

static void value_clear(Value* v) {
    g_free(v->str);
    if (v->items != NULL) g_ptr_array_unref(v->items);
    memset(v, 0, sizeof *v);
}

static gboolean fail(Parser* ps, const gchar* message) {
    if (ps->error != NULL && *ps->error == NULL) {
        /* TRANSLATORS: %1$d is a line of .lookinfiles/tasks.toml, %2$s what is wrong there */
        g_set_error(ps->error, TASK_CONFIG_ERROR, 0, _("line %1$d: %2$s"), ps->line, message);
    }
    return FALSE;
}

/* Spaces and tabs */
static void skip_blank(Parser* ps) {
    while (*ps->p == ' ' || *ps->p == '\t') ps->p++;
}

/* Spaces, newlines and comments (inside an array) */
static void skip_space_lines(Parser* ps) {
    for (;;) {
        skip_blank(ps);
        if (*ps->p == '#') {
            while (*ps->p != '\0' && *ps->p != '\n') ps->p++;
        }
        if (*ps->p == '\r' && ps->p[1] == '\n') ps->p++;
        if (*ps->p != '\n') return;
        ps->p++;
        ps->line++;
    }
}

/* The end of a statement: blanks, a comment, then the end of the line */
static gboolean end_of_line(Parser* ps) {
    skip_blank(ps);
    if (*ps->p == '#') {
        while (*ps->p != '\0' && *ps->p != '\n') ps->p++;
    }
    if (*ps->p == '\r') ps->p++;
    if (*ps->p == '\0') return TRUE;
    if (*ps->p != '\n') return fail(ps, _("unexpected text after the value"));
    ps->p++;
    ps->line++;
    return TRUE;
}

static gboolean read_string(Parser* ps, gchar** out) {
    gchar quote = *ps->p;
    if (quote != '"' && quote != '\'') return fail(ps, _("a string was expected"));
    ps->p++;
    GString* s = g_string_new(NULL);
    while (*ps->p != quote) {
        if (*ps->p == '\0' || *ps->p == '\n') {
            g_string_free(s, TRUE);
            return fail(ps, _("the string is not closed on its line"));
        }
        if (quote == '"' && *ps->p == '\\') {
            ps->p++;
            switch (*ps->p) {
            case 'n': g_string_append_c(s, '\n'); break;
            case 't': g_string_append_c(s, '\t'); break;
            case 'r': g_string_append_c(s, '\r'); break;
            case '"': g_string_append_c(s, '"'); break;
            case '\\': g_string_append_c(s, '\\'); break;
            case 'u': {
                gunichar c = 0;
                for (gint i = 1; i <= 4; i++) {
                    gint d = g_ascii_xdigit_value(ps->p[i]);
                    if (d < 0) {
                        g_string_free(s, TRUE);
                        return fail(ps, _("bad \\u escape"));
                    }
                    c = c * 16 + (gunichar)d;
                }
                g_string_append_unichar(s, c);
                ps->p += 4;
                break;
            }
            default:
                g_string_free(s, TRUE);
                return fail(ps, _("unknown escape in the string"));
            }
            ps->p++;
            continue;
        }
        g_string_append_c(s, *ps->p++);
    }
    ps->p++;
    *out = g_string_free(s, FALSE);
    return TRUE;
}

static gboolean read_key(Parser* ps, gchar** out) {
    if (*ps->p == '"' || *ps->p == '\'') return read_string(ps, out);
    const gchar* start = ps->p;
    while (g_ascii_isalnum(*ps->p) || *ps->p == '_' || *ps->p == '-') ps->p++;
    if (ps->p == start) return fail(ps, _("a key was expected"));
    *out = g_strndup(start, (gsize)(ps->p - start));
    return TRUE;
}

static gboolean read_value(Parser* ps, Value* v) {
    memset(v, 0, sizeof *v);
    if (*ps->p == '"' || *ps->p == '\'') {
        v->kind = VAL_STRING;
        return read_string(ps, &v->str);
    }
    if (strncmp(ps->p, "true", 4) == 0 || strncmp(ps->p, "false", 5) == 0) {
        v->kind = VAL_BOOL;
        v->boolean = ps->p[0] == 't';
        ps->p += v->boolean ? 4 : 5;
        return TRUE;
    }
    if (*ps->p == '[') {
        v->kind = VAL_ARRAY;
        v->items = g_ptr_array_new_with_free_func(g_free);
        ps->p++;
        skip_space_lines(ps);
        while (*ps->p != ']') {
            gchar* item = NULL;
            if (!read_string(ps, &item)) return fail(ps, _("an array holds strings only"));
            g_ptr_array_add(v->items, item);
            skip_space_lines(ps);
            if (*ps->p == ',') {
                ps->p++;
                skip_space_lines(ps);
            } else if (*ps->p != ']') {
                return fail(ps, _("a comma or ] was expected in the array"));
            }
        }
        ps->p++;
        return TRUE;
    }
    if (*ps->p == '{') {
        v->kind = VAL_TABLE;
        v->items = g_ptr_array_new_with_free_func(g_free);
        ps->p++;
        skip_blank(ps);
        while (*ps->p != '}') {
            gchar* key = NULL;
            gchar* val = NULL;
            if (!read_key(ps, &key)) return FALSE;
            skip_blank(ps);
            if (*ps->p != '=') {
                g_free(key);
                return fail(ps, _("= was expected after the key"));
            }
            ps->p++;
            skip_blank(ps);
            if (!read_string(ps, &val)) {
                g_free(key);
                return FALSE;
            }
            g_ptr_array_add(v->items, g_strconcat(key, "=", val, NULL));
            g_free(key);
            g_free(val);
            skip_blank(ps);
            if (*ps->p == ',') {
                ps->p++;
                skip_blank(ps);
            } else if (*ps->p != '}') {
                return fail(ps, _("a comma or } was expected in the table"));
            }
        }
        ps->p++;
        return TRUE;
    }
    return fail(ps, _("a value was expected (a string, true, false, [...] or {...})"));
}

/* ------------------------------------------------------------------------- */
/* Tasks                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    gint line; /* of its [[task]] */
    gchar* name;
    gchar* command;
    GPtrArray* args;
    gchar* cwd;
    GPtrArray* env;
    gchar* matcher;
    gboolean is_default;
} TaskTable;

static void table_free(gpointer p) {
    TaskTable* t = p;
    g_free(t->name);
    g_free(t->command);
    if (t->args != NULL) g_ptr_array_unref(t->args);
    g_free(t->cwd);
    if (t->env != NULL) g_ptr_array_unref(t->env);
    g_free(t->matcher);
    g_free(t);
}

/* Put value v (taken) in t under key */
static gboolean set_key(Parser* ps, TaskTable* t, const gchar* key, Value* v) {
    ValKind want = g_strcmp0(key, "args") == 0      ? VAL_ARRAY
                   : g_strcmp0(key, "env") == 0     ? VAL_TABLE
                   : g_strcmp0(key, "default") == 0 ? VAL_BOOL
                                                    : VAL_STRING;
    static const gchar* const KEYS[] = {"name", "command", "args", "cwd", "env", "matcher", "default"};
    gboolean known = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(KEYS); i++) known = known || g_strcmp0(KEYS[i], key) == 0;
    if (!known) {
        gchar* msg = g_strdup_printf(_("unknown key %s (name, command, args, cwd, env, matcher, default)"), key);
        fail(ps, msg);
        g_free(msg);
        return FALSE;
    }
    if (v->kind != want) {
        const gchar* what = want == VAL_ARRAY ? _("an array of strings")
                            : want == VAL_TABLE ? _("a table of strings")
                            : want == VAL_BOOL  ? _("true or false")
                                                : _("a string");
        gchar* msg = g_strdup_printf(_("%1$s must be %2$s"), key, what);
        fail(ps, msg);
        g_free(msg);
        return FALSE;
    }
    gboolean seen = g_strcmp0(key, "name") == 0      ? t->name != NULL
                    : g_strcmp0(key, "command") == 0 ? t->command != NULL
                    : g_strcmp0(key, "args") == 0    ? t->args != NULL
                    : g_strcmp0(key, "cwd") == 0     ? t->cwd != NULL
                    : g_strcmp0(key, "env") == 0     ? t->env != NULL
                    : g_strcmp0(key, "matcher") == 0 ? t->matcher != NULL
                                                     : FALSE;
    if (seen) {
        gchar* msg = g_strdup_printf(_("%s is given twice"), key);
        fail(ps, msg);
        g_free(msg);
        return FALSE;
    }
    if (g_strcmp0(key, "name") == 0) t->name = g_steal_pointer(&v->str);
    else if (g_strcmp0(key, "command") == 0) t->command = g_steal_pointer(&v->str);
    else if (g_strcmp0(key, "args") == 0) t->args = g_steal_pointer(&v->items);
    else if (g_strcmp0(key, "cwd") == 0) t->cwd = g_steal_pointer(&v->str);
    else if (g_strcmp0(key, "env") == 0) t->env = g_steal_pointer(&v->items);
    else if (g_strcmp0(key, "matcher") == 0) t->matcher = g_steal_pointer(&v->str);
    else t->is_default = v->boolean;
    return TRUE;
}

/* A complete task: a name, one command, a matcher that compiles */
static gboolean check_table(Parser* ps, TaskTable* t) {
    ps->line = t->line;
    if (t->name == NULL || t->name[0] == '\0') return fail(ps, _("the task has no name"));
    gboolean has_args = t->args != NULL && t->args->len > 0;
    if ((t->command != NULL) == has_args) return fail(ps, _("give the task either command or args"));
    if (t->command != NULL && t->command[0] == '\0') return fail(ps, _("the command is empty"));
    if (t->matcher != NULL) {
        GError* re_error = NULL;
        GRegex* re = g_regex_new(t->matcher, 0, 0, &re_error);
        if (re == NULL) {
            gchar* msg = g_strdup_printf(_("the matcher is not a valid regular expression: %s"), re_error->message);
            fail(ps, msg);
            g_free(msg);
            g_error_free(re_error);
            return FALSE;
        }
        gboolean groups = g_regex_get_string_number(re, "file") >= 0 && g_regex_get_string_number(re, "line") >= 0;
        g_regex_unref(re);
        if (!groups) return fail(ps, _("the matcher needs the groups (?<file>...) and (?<line>...)"));
    }
    return TRUE;
}

static TaskDef* table_task(const TaskTable* t, const gchar* dir) {
    GPtrArray* argv = g_ptr_array_new();
    if (t->command != NULL) {
#ifdef G_OS_WIN32
        g_ptr_array_add(argv, "cmd.exe");
        g_ptr_array_add(argv, "/d");
        g_ptr_array_add(argv, "/s");
        g_ptr_array_add(argv, "/c");
#else
        g_ptr_array_add(argv, "sh");
        g_ptr_array_add(argv, "-c");
#endif
        g_ptr_array_add(argv, t->command);
    } else {
        for (guint i = 0; i < t->args->len; i++) g_ptr_array_add(argv, g_ptr_array_index(t->args, i));
    }
    g_ptr_array_add(argv, NULL);
    gchar* cwd = t->cwd != NULL ? g_canonicalize_filename(t->cwd, dir) : g_strdup(dir);
    TaskDef* def = task_def_new(t->name, (const gchar* const*)argv->pdata, cwd);
    g_free(cwd);
    g_ptr_array_unref(argv);
    if (t->command != NULL) {
        def->detail = g_strdup(t->command);
    } else {
        g_ptr_array_add(t->args, NULL);
        def->detail = g_strjoinv(" ", (gchar**)t->args->pdata);
        g_ptr_array_remove_index(t->args, t->args->len - 1);
    }
    if (t->env != NULL && t->env->len > 0) {
        g_ptr_array_add(t->env, NULL);
        def->env = g_strdupv((gchar**)t->env->pdata);
        g_ptr_array_remove_index(t->env, t->env->len - 1);
    }
    def->matcher = g_strdup(t->matcher);
    return def;
}

gchar* task_config_path(const gchar* dir) {
    return g_build_filename(dir, ".lookinfiles", "tasks.toml", NULL);
}

GPtrArray* task_config_parse(const gchar* text, const gchar* dir, GError** error) {
    Parser ps = {text != NULL ? text : "", 1, error};
    GPtrArray* tables = g_ptr_array_new_with_free_func(table_free);
    gboolean ok = TRUE;
    /* A byte order mark is not text */
    if (g_str_has_prefix(ps.p, "\xef\xbb\xbf")) ps.p += 3;
    while (ok && *ps.p != '\0') {
        skip_blank(&ps);
        if (*ps.p == '#' || *ps.p == '\n' || *ps.p == '\r' || *ps.p == '\0') {
            ok = end_of_line(&ps);
            continue;
        }
        if (*ps.p == '[') {
            if (strncmp(ps.p, "[[", 2) != 0) {
                ok = fail(&ps, _("only [[task]] tables are known"));
                break;
            }
            ps.p += 2;
            skip_blank(&ps);
            gchar* name = NULL;
            ok = read_key(&ps, &name);
            skip_blank(&ps);
            if (ok && (g_strcmp0(name, "task") != 0 || strncmp(ps.p, "]]", 2) != 0)) {
                ok = fail(&ps, _("only [[task]] tables are known"));
            }
            g_free(name);
            if (!ok) break;
            ps.p += 2;
            TaskTable* t = g_new0(TaskTable, 1);
            t->line = ps.line;
            g_ptr_array_add(tables, t);
            ok = end_of_line(&ps);
            continue;
        }
        gchar* key = NULL;
        if (!read_key(&ps, &key)) {
            ok = FALSE;
            break;
        }
        if (tables->len == 0) {
            g_free(key);
            ok = fail(&ps, _("a key before the first [[task]]"));
            break;
        }
        skip_blank(&ps);
        if (*ps.p != '=') {
            g_free(key);
            ok = fail(&ps, _("= was expected after the key"));
            break;
        }
        ps.p++;
        skip_blank(&ps);
        Value v;
        ok = read_value(&ps, &v) && set_key(&ps, g_ptr_array_index(tables, tables->len - 1), key, &v);
        value_clear(&v);
        g_free(key);
        if (ok) ok = end_of_line(&ps);
    }
    for (guint i = 0; ok && i < tables->len; i++) ok = check_table(&ps, g_ptr_array_index(tables, i));
    if (!ok) {
        g_ptr_array_unref(tables);
        return NULL;
    }
    GPtrArray* tasks = g_ptr_array_new_with_free_func((GDestroyNotify)task_def_free);
    gint first_default = -1;
    for (guint i = 0; i < tables->len; i++) {
        TaskTable* t = g_ptr_array_index(tables, i);
        if (t->is_default && first_default < 0) first_default = (gint)i;
    }
    if (first_default >= 0) g_ptr_array_add(tasks, table_task(g_ptr_array_index(tables, first_default), dir));
    for (guint i = 0; i < tables->len; i++) {
        if ((gint)i != first_default) g_ptr_array_add(tasks, table_task(g_ptr_array_index(tables, i), dir));
    }
    g_ptr_array_unref(tables);
    return tasks;
}
