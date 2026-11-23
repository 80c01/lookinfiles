#ifndef TASK_PARSE_H
#define TASK_PARSE_H

/* The messages in the output of a build or test command, read one line at a
 * time as it comes (the Build tab, task_page.h):
 *  - gcc, clang, go, pytest and most tools: "file:line[:col]: [error|warning|
 *    note:] text" (also behind "/usr/bin/ld: ");
 *  - rustc / cargo: "error[E0308]: text" then "  --> file:line:col"; panics
 *    ("panicked at file:line:col");
 *  - tsc: "file(line,col): error TS2322: text" and "file:line:col - error ...";
 *  - eslint (stylish): a file alone on a line, then "  line:col  error  text";
 *  - Python tracebacks: the exception at the last frame in the project
 *    (else the last frame).
 * Relative files are resolved against the directory the tool was in: the
 * command's, changed by "make[1]: Entering directory '...'" / "Leaving
 * directory" and "ninja: Entering directory `...'". Colours (ANSI escapes)
 * and the progress before a carriage return are dropped. A message repeated
 * (same kind, place and text) is kept once.
 *
 * Pure GLib, unit-tested. */
#include <glib.h>

typedef enum {
    TASK_MSG_ERROR,
    TASK_MSG_WARNING,
    TASK_MSG_NOTE,
} TaskMsgKind;

typedef struct {
    TaskMsgKind kind;
    gchar* file;       /* absolute and canonical, or NULL when none is named */
    gint line;         /* 1-based, 0 when unknown */
    gint column;       /* 1-based, 0 when unknown */
    gchar* text;       /* the message */
    guint output_line; /* 0-based line of the output it was read on */
} TaskMessage;

typedef struct TaskParser TaskParser;

/* A parser of the output of a command run in dir (absolute) */
TaskParser* task_parser_new(const gchar* dir);
void task_parser_free(TaskParser* p);

/* A regular expression tried on each line before the built-in ones, with
 * the named groups file and line (required), column, severity ("error",
 * "warning", "note"...) and message (the whole line when missing). FALSE
 * when it does not compile or lacks file or line. */
gboolean task_parser_add_matcher(TaskParser* p, const gchar* pattern);

/* Read the next line of the output (without its newline; any encoding is
 * made valid UTF-8). Returns how many messages it added. */
guint task_parser_feed(TaskParser* p, const gchar* line);

/* The output ended: the message waiting for more lines is added. Returns how
 * many messages it added. */
guint task_parser_finish(TaskParser* p);

/* The messages read so far, in output order (TaskMessage*, owned by p) */
const GPtrArray* task_parser_messages(const TaskParser* p);

/* Messages of kind among the messages read */
guint task_parser_count(const TaskParser* p, TaskMsgKind kind);

/* line without its ANSI escapes and the text before its last carriage
 * return (progress lines), as valid UTF-8 */
gchar* task_parse_clean_line(const gchar* line);

#endif /* TASK_PARSE_H */
