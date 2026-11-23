/* The messages in the output of a build or test command (see task_parse.h) */
#include "tasks/task_parse.h"
#include <string.h>

struct TaskParser {
    gchar* base;         /* the command's directory */
    GPtrArray* dirs;     /* directories entered (make, ninja), innermost last */
    GPtrArray* messages; /* TaskMessage* */
    GHashTable* seen;    /* keys of the messages kept */
    guint line_no;       /* lines read */
    guint counts[TASK_MSG_NOTE + 1];

    /* rustc: the head of a message waiting for its "-->" location */
    TaskMsgKind rust_kind;
    gchar* rust_text;
    /* A panic whose text is on the next line */
    TaskMessage* panic;
    /* eslint: the file of the rows that follow */
    gchar* eslint_file;
    /* Python traceback: the last frame, and the last one in the project */
    gboolean in_traceback;
    gchar* frame_file;
    gint frame_line;
    gchar* project_frame_file;
    gint project_frame_line;

    GRegex* re_dir;     /* make / ninja: Entering / Leaving directory */
    GRegex* re_place;   /* file:line[:col]: text */
    GRegex* re_linker;  /* ld: text */
    GRegex* re_rust;    /* error[E0308]: text */
    GRegex* re_arrow;   /* --> file:line:col */
    GRegex* re_panic;   /* panicked at file:line:col */
    GRegex* re_tsc;     /* file(line,col): error TS1: text */
    GRegex* re_tsc2;    /* file:line:col - error TS1: text */
    GRegex* re_eslint;  /*   line:col  error  text */
    GRegex* re_frame;   /*   File "x.py", line 12 */
    GPtrArray* matchers; /* GRegex* of task_parser_add_matcher() */
};

static void message_free(gpointer m) {
    TaskMessage* msg = m;
    g_free(msg->file);
    g_free(msg->text);
    g_free(msg);
}

static GRegex* regex(const gchar* pattern) {
    GRegex* re = g_regex_new(pattern, G_REGEX_OPTIMIZE, 0, NULL);
    g_assert(re != NULL);
    return re;
}

TaskParser* task_parser_new(const gchar* dir) {
    TaskParser* p = g_new0(TaskParser, 1);
    p->base = g_canonicalize_filename(dir != NULL ? dir : ".", NULL);
    p->dirs = g_ptr_array_new_with_free_func(g_free);
    p->messages = g_ptr_array_new_with_free_func(message_free);
    p->seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    p->matchers = g_ptr_array_new_with_free_func((GDestroyNotify)g_regex_unref);
    p->re_dir = regex("^(?:\\S*?make(?:\\.exe)?(?:\\[\\d+\\])?|ninja): (Entering|Leaving) directory [`'\xe2\x80\x98\"](.*)['\xe2\x80\x99\"]$");
    p->re_place = regex("^\\s*((?:[A-Za-z]:)?[^\\s:\"'()<>|*?]+):(\\d+):(?:(\\d+):)?\\s*"
                        "(?:(fatal error|error|warning|note|remark)\\s*:\\s*)?(.*)$");
    p->re_linker = regex("^(?:\\S*[/\\\\])?(?:ld|ld\\.\\w+|collect2|lld)(?:\\.exe)?: (.*)$");
    p->re_rust = regex("^(error|warning)(?:\\[\\w+\\])?: (.+)$");
    p->re_arrow = regex("^\\s*--> (.+?):(\\d+):(\\d+)$");
    p->re_panic = regex("panicked at (?:'(.*)', )?(.+?):(\\d+):(\\d+):?$");
    p->re_tsc = regex("^(.+?)\\((\\d+),(\\d+)\\): (error|warning) (.+)$");
    p->re_tsc2 = regex("^(.+?):(\\d+):(\\d+) - (error|warning) (.+)$");
    p->re_eslint = regex("^\\s+(\\d+):(\\d+)\\s+(error|warning)\\s+(.+?)\\s*$");
    p->re_frame = regex("^\\s+File \"(.+)\", line (\\d+)");
    return p;
}

void task_parser_free(TaskParser* p) {
    if (p == NULL) return;
    g_free(p->base);
    g_ptr_array_unref(p->dirs);
    g_ptr_array_unref(p->messages);
    g_hash_table_unref(p->seen);
    g_ptr_array_unref(p->matchers);
    g_free(p->rust_text);
    if (p->panic != NULL) message_free(p->panic);
    g_free(p->eslint_file);
    g_free(p->frame_file);
    g_free(p->project_frame_file);
    GRegex* res[] = {p->re_dir, p->re_place, p->re_linker, p->re_rust, p->re_arrow,
                     p->re_panic, p->re_tsc, p->re_tsc2, p->re_eslint, p->re_frame};
    for (guint i = 0; i < G_N_ELEMENTS(res); i++) g_regex_unref(res[i]);
    g_free(p);
}

const GPtrArray* task_parser_messages(const TaskParser* p) {
    return p->messages;
}

guint task_parser_count(const TaskParser* p, TaskMsgKind kind) {
    return (guint)kind <= TASK_MSG_NOTE ? p->counts[kind] : 0;
}

/* ------------------------------------------------------------------------- */
/* Lines                                                                     */
/* ------------------------------------------------------------------------- */

gchar* task_parse_clean_line(const gchar* line) {
    if (line == NULL) return g_strdup("");
    gchar* valid = g_utf8_make_valid(line, -1);
    GString* out = g_string_sized_new(strlen(valid));
    for (const gchar* s = valid; *s != '\0'; s++) {
        if (*s == '\x1b') {
            if (s[1] == '[') {
                /* CSI: parameters, then one final byte */
                s += 2;
                while (*s != '\0' && !(*s >= 0x40 && *s <= 0x7e)) s++;
                if (*s == '\0') break;
            } else if (s[1] == ']') {
                /* OSC: up to BEL or ESC \ */
                s += 2;
                while (*s != '\0' && *s != '\a' && !(*s == '\x1b' && s[1] == '\\')) s++;
                if (*s == '\0') break;
                if (*s == '\x1b') s++;
            } else if (s[1] != '\0') {
                s++;
            }
            continue;
        }
        if (*s == '\r') {
            /* A trailing carriage return ends the line; another one starts it again */
            if (s[1] != '\0') g_string_truncate(out, 0);
            continue;
        }
        g_string_append_c(out, *s);
    }
    g_free(valid);
    return g_string_free(out, FALSE);
}

/* The directory the tool is in */
static const gchar* current_dir(const TaskParser* p) {
    return p->dirs->len > 0 ? g_ptr_array_index(p->dirs, p->dirs->len - 1) : p->base;
}

static gchar* resolve(const TaskParser* p, const gchar* file) {
    if (file == NULL || file[0] == '\0') return NULL;
    return g_canonicalize_filename(file, current_dir(p));
}

/* Add a message (file as written by the tool); FALSE when it was seen already */
static gboolean add_message(TaskParser* p, TaskMsgKind kind, const gchar* file, gint line, gint column,
                            const gchar* text, guint output_line) {
    gchar* path = resolve(p, file);
    gchar* stripped = g_strstrip(g_strdup(text != NULL ? text : ""));
    gchar* key = g_strdup_printf("%d\x1f%s\x1f%d\x1f%d\x1f%s", kind, path != NULL ? path : "", line, column, stripped);
    if (g_hash_table_contains(p->seen, key)) {
        g_free(key);
        g_free(stripped);
        g_free(path);
        return FALSE;
    }
    g_hash_table_add(p->seen, key);
    TaskMessage* m = g_new0(TaskMessage, 1);
    m->kind = kind;
    m->file = path;
    m->line = line;
    m->column = column;
    m->text = stripped;
    m->output_line = output_line;
    g_ptr_array_add(p->messages, m);
    p->counts[kind]++;
    return TRUE;
}

static gint match_int(GMatchInfo* mi, gint group) {
    gchar* s = g_match_info_fetch(mi, group);
    gint v = s != NULL && s[0] != '\0' ? (gint)g_ascii_strtoll(s, NULL, 10) : 0;
    g_free(s);
    return v;
}

static TaskMsgKind kind_of(const gchar* word) {
    if (word == NULL || word[0] == '\0') return TASK_MSG_ERROR;
    if (g_ascii_strcasecmp(word, "warning") == 0) return TASK_MSG_WARNING;
    if (g_ascii_strcasecmp(word, "note") == 0 || g_ascii_strcasecmp(word, "remark") == 0) return TASK_MSG_NOTE;
    return TASK_MSG_ERROR;
}

/* A file name a tool would write: with a folder or an extension, never a
 * plain word ("Note", "12" of a time of day) */
static gboolean looks_like_file(const gchar* file) {
    if (file == NULL || file[0] == '\0') return FALSE;
    if (strchr(file, '/') != NULL || strchr(file, '\\') != NULL) return TRUE;
    const gchar* dot = strrchr(file, '.');
    if (dot == NULL || dot == file || dot[1] == '\0') return FALSE;
    for (const gchar* c = dot + 1; *c != '\0'; c++) {
        if (!g_ascii_isalnum(*c) && *c != '_' && *c != '+' && *c != '-') return FALSE;
    }
    return g_ascii_isalpha(dot[1]);
}

/* The rustc head waiting for a location is dropped: without one it is a
 * summary ("could not compile", "aborting due to 2 previous errors") */
static void drop_rust_head(TaskParser* p) {
    g_clear_pointer(&p->rust_text, g_free);
}

static guint flush_panic(TaskParser* p, const gchar* text) {
    if (p->panic == NULL) return 0;
    TaskMessage* m = p->panic;
    p->panic = NULL;
    gboolean added = add_message(p, TASK_MSG_ERROR, m->file, m->line, m->column,
                                 text != NULL && text[0] != '\0' ? text : m->text, m->output_line);
    message_free(m);
    return added ? 1 : 0;
}

static guint end_traceback(TaskParser* p, const gchar* text, guint output_line) {
    guint added = 0;
    const gchar* file = p->project_frame_file != NULL ? p->project_frame_file : p->frame_file;
    gint line = p->project_frame_file != NULL ? p->project_frame_line : p->frame_line;
    if (file != NULL && text != NULL) added = add_message(p, TASK_MSG_ERROR, file, line, 0, text, output_line) ? 1 : 0;
    p->in_traceback = FALSE;
    g_clear_pointer(&p->frame_file, g_free);
    g_clear_pointer(&p->project_frame_file, g_free);
    return added;
}

/* A frame of a Python traceback; in the project when under the command's
 * directory and not in an installed package */
static void add_frame(TaskParser* p, const gchar* file, gint line) {
    if (file[0] == '<') return; /* "<frozen importlib._bootstrap>", "<string>" */
    g_free(p->frame_file);
    p->frame_file = g_strdup(file);
    p->frame_line = line;
    gchar* path = resolve(p, file);
    gsize n = strlen(p->base);
    gboolean in_project = path != NULL && strncmp(path, p->base, n) == 0 && (path[n] == '/' || path[n] == '\\') &&
                          strstr(path, "site-packages") == NULL && strstr(path, "dist-packages") == NULL;
    if (in_project) {
        g_free(p->project_frame_file);
        p->project_frame_file = g_strdup(file);
        p->project_frame_line = line;
    }
    g_free(path);
}

static gboolean match(GRegex* re, const gchar* text, GMatchInfo** mi) {
    if (g_regex_match(re, text, 0, mi)) return TRUE;
    g_match_info_free(*mi);
    *mi = NULL;
    return FALSE;
}

/* "file:line[:col]: [kind:] text": TRUE when text is one */
static gboolean feed_place(TaskParser* p, const gchar* text, guint out_line, guint* added, TaskMsgKind fallback) {
    GMatchInfo* mi = NULL;
    if (!match(p->re_place, text, &mi)) return FALSE;
    gchar* file = g_match_info_fetch(mi, 1);
    gboolean ok = looks_like_file(file);
    if (ok) {
        gchar* word = g_match_info_fetch(mi, 4);
        gchar* body = g_match_info_fetch(mi, 5);
        TaskMsgKind kind = word != NULL && word[0] != '\0' ? kind_of(word) : fallback;
        /* Python warnings: "x.py:12: DeprecationWarning: text" */
        if (word == NULL || word[0] == '\0') {
            const gchar* c = body;
            while (g_ascii_isalnum(*c)) c++;
            if (c - body >= 7 && strncmp(c - 7, "Warning", 7) == 0 && *c == ':') kind = TASK_MSG_WARNING;
        }
        if (add_message(p, kind, file, match_int(mi, 2), match_int(mi, 3), body, out_line)) (*added)++;
        g_free(body);
        g_free(word);
    }
    g_free(file);
    g_match_info_free(mi);
    return ok;
}

gboolean task_parser_add_matcher(TaskParser* p, const gchar* pattern) {
    GRegex* re = pattern != NULL ? g_regex_new(pattern, G_REGEX_OPTIMIZE, 0, NULL) : NULL;
    if (re == NULL) return FALSE;
    if (g_regex_get_string_number(re, "file") < 0 || g_regex_get_string_number(re, "line") < 0) {
        g_regex_unref(re);
        return FALSE;
    }
    g_ptr_array_add(p->matchers, re);
    return TRUE;
}

static gint named_int(GMatchInfo* mi, const gchar* name) {
    gchar* s = g_match_info_fetch_named(mi, name);
    gint v = s != NULL && s[0] != '\0' ? (gint)g_ascii_strtoll(s, NULL, 10) : 0;
    g_free(s);
    return v;
}

/* The matchers of the task: TRUE when one took text */
static gboolean feed_matchers(TaskParser* p, const gchar* text, guint out_line, guint* added) {
    for (guint i = 0; i < p->matchers->len; i++) {
        GMatchInfo* mi = NULL;
        if (!match(g_ptr_array_index(p->matchers, i), text, &mi)) continue;
        gchar* file = g_match_info_fetch_named(mi, "file");
        gint line = named_int(mi, "line");
        gboolean ok = file != NULL && file[0] != '\0' && line > 0;
        if (ok) {
            gchar* word = g_match_info_fetch_named(mi, "severity");
            gchar* body = g_match_info_fetch_named(mi, "message");
            gchar* lower = word != NULL ? g_ascii_strdown(word, -1) : NULL;
            TaskMsgKind kind = lower == NULL || lower[0] == '\0'   ? TASK_MSG_ERROR
                               : g_str_has_prefix(lower, "warn")    ? TASK_MSG_WARNING
                               : g_str_has_prefix(lower, "note") || g_str_has_prefix(lower, "info") ||
                                       g_str_has_prefix(lower, "hint") || g_str_has_prefix(lower, "remark")
                                   ? TASK_MSG_NOTE
                                   : TASK_MSG_ERROR;
            if (add_message(p, kind, file, line, named_int(mi, "column"), body != NULL && body[0] != '\0' ? body : text,
                            out_line)) {
                (*added)++;
            }
            g_free(lower);
            g_free(body);
            g_free(word);
        }
        g_free(file);
        g_match_info_free(mi);
        if (ok) return TRUE;
    }
    return FALSE;
}

guint task_parser_feed(TaskParser* p, const gchar* raw) {
    gchar* text = task_parse_clean_line(raw);
    guint out_line = p->line_no++;
    guint added = 0;
    GMatchInfo* mi = NULL;

    /* A panic's text is the line after it */
    if (p->panic != NULL) {
        added += flush_panic(p, text);
        g_free(text);
        return added;
    }

    /* The task's own matchers come first */
    if (feed_matchers(p, text, out_line, &added)) {
        g_free(text);
        return added;
    }

    /* Python traceback: frames, then the exception unindented */
    if (g_str_has_prefix(text, "Traceback (most recent call last):")) {
        p->in_traceback = TRUE;
        g_free(text);
        return added;
    }
    if (p->in_traceback) {
        if (match(p->re_frame, text, &mi)) {
            gchar* file = g_match_info_fetch(mi, 1);
            add_frame(p, file, match_int(mi, 2));
            g_free(file);
            g_match_info_free(mi);
        } else if (text[0] != '\0' && !g_ascii_isspace(text[0])) {
            added += end_traceback(p, text, out_line);
        }
        g_free(text);
        return added;
    }

    /* make / ninja changing directory */
    if (match(p->re_dir, text, &mi)) {
        gchar* verb = g_match_info_fetch(mi, 1);
        gchar* dir = g_match_info_fetch(mi, 2);
        if (g_strcmp0(verb, "Entering") == 0) {
            g_ptr_array_add(p->dirs, g_canonicalize_filename(dir, current_dir(p)));
        } else if (p->dirs->len > 0) {
            g_ptr_array_remove_index(p->dirs, p->dirs->len - 1);
        }
        g_free(dir);
        g_free(verb);
        g_match_info_free(mi);
        g_free(text);
        return added;
    }

    /* rustc: the head, then its location */
    if (match(p->re_rust, text, &mi)) {
        g_free(p->rust_text);
        gchar* word = g_match_info_fetch(mi, 1);
        p->rust_kind = kind_of(word);
        p->rust_text = g_match_info_fetch(mi, 2);
        g_free(word);
        g_match_info_free(mi);
        g_free(text);
        return added;
    }
    if (match(p->re_arrow, text, &mi)) {
        if (p->rust_text != NULL) {
            gchar* file = g_match_info_fetch(mi, 1);
            if (add_message(p, p->rust_kind, file, match_int(mi, 2), match_int(mi, 3), p->rust_text, out_line)) added++;
            g_free(file);
            drop_rust_head(p);
        }
        g_match_info_free(mi);
        g_free(text);
        return added;
    }
    if (text[0] == '\0') {
        drop_rust_head(p);
        g_clear_pointer(&p->eslint_file, g_free);
        g_free(text);
        return added;
    }

    /* Rust panics: "thread 'main' panicked at src/main.rs:2:5:" */
    if (match(p->re_panic, text, &mi)) {
        gchar* inline_text = g_match_info_fetch(mi, 1);
        gchar* file = g_match_info_fetch(mi, 2);
        if (inline_text != NULL && inline_text[0] != '\0') {
            if (add_message(p, TASK_MSG_ERROR, file, match_int(mi, 3), match_int(mi, 4), inline_text, out_line)) added++;
        } else {
            p->panic = g_new0(TaskMessage, 1);
            p->panic->file = g_strdup(file);
            p->panic->line = match_int(mi, 3);
            p->panic->column = match_int(mi, 4);
            p->panic->text = g_strdup("panicked");
            p->panic->output_line = out_line;
        }
        g_free(file);
        g_free(inline_text);
        g_match_info_free(mi);
        g_free(text);
        return added;
    }

    /* tsc */
    if (match(p->re_tsc, text, &mi) || match(p->re_tsc2, text, &mi)) {
        gchar* file = g_match_info_fetch(mi, 1);
        if (looks_like_file(file) && strchr(file, ' ') == NULL) {
            gchar* word = g_match_info_fetch(mi, 4);
            gchar* body = g_match_info_fetch(mi, 5);
            if (add_message(p, kind_of(word), file, match_int(mi, 2), match_int(mi, 3), body, out_line)) added++;
            g_free(body);
            g_free(word);
            g_free(file);
            g_match_info_free(mi);
            g_free(text);
            return added;
        }
        g_free(file);
        g_match_info_free(mi);
    }

    /* eslint: the rows under their file */
    if (p->eslint_file != NULL && match(p->re_eslint, text, &mi)) {
        gchar* word = g_match_info_fetch(mi, 3);
        gchar* body = g_match_info_fetch(mi, 4);
        if (add_message(p, kind_of(word), p->eslint_file, match_int(mi, 1), match_int(mi, 2), body, out_line)) added++;
        g_free(body);
        g_free(word);
        g_match_info_free(mi);
        g_free(text);
        return added;
    }
    if (g_path_is_absolute(text) && strchr(text, ':') == (g_ascii_isalpha(text[0]) && text[1] == ':' ? text + 1 : NULL)) {
        g_free(p->eslint_file);
        p->eslint_file = g_strdup(text);
        g_free(text);
        return added;
    }

    /* The linker: "/usr/bin/ld: main.c:5: undefined reference to `f'" */
    if (match(p->re_linker, text, &mi)) {
        gchar* rest = g_match_info_fetch(mi, 1);
        feed_place(p, rest, out_line, &added, TASK_MSG_ERROR);
        g_free(rest);
        g_match_info_free(mi);
        g_free(text);
        return added;
    }

    feed_place(p, text, out_line, &added, TASK_MSG_ERROR);
    g_free(text);
    return added;
}

guint task_parser_finish(TaskParser* p) {
    guint added = flush_panic(p, NULL);
    drop_rust_head(p);
    if (p->in_traceback) added += end_traceback(p, NULL, p->line_no);
    return added;
}
