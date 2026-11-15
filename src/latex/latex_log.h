#ifndef LATEX_LOG_H
#define LATEX_LOG_H

/* The messages of a TeX log (.log of pdfLaTeX, XeLaTeX, LuaLaTeX, Tectonic):
 * errors (with -file-line-error "./file.tex:12: message", else "! message"
 * then "l.12"), LaTeX, package and class warnings ("on input line 12"),
 * overfull and underfull boxes. Lines wrapped by TeX at 79 characters are
 * joined again; the file of a message without one is the innermost file
 * open at that point (the "(./file.tex" ... ")" nesting of the log). A
 * message repeated (same kind, place and text) is kept once.
 *
 * Pure GLib, unit-tested. */
#include <glib.h>

typedef enum {
    LATEX_MSG_ERROR,
    LATEX_MSG_WARNING,
    LATEX_MSG_BADBOX,
} LatexMsgKind;

typedef struct {
    LatexMsgKind kind;
    gchar* file;    /* as written in the log ("./chapter.tex"), or NULL */
    gint line;      /* 1-based, 0 when unknown */
    gchar* text;    /* the message */
    gchar* context; /* error: the source after "l.12", or NULL */
} LatexMessage;

/* The messages of log (len bytes, -1: NUL-terminated), in log order: an
 * array of LatexMessage* that frees them */
GPtrArray* latex_log_parse(const gchar* log, gssize len);

/* Messages of kind among msgs */
guint latex_log_count(const GPtrArray* msgs, LatexMsgKind kind);

#endif /* LATEX_LOG_H */
