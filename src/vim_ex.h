#ifndef VIM_EX_H
#define VIM_EX_H

/* Ex command line parsing: ":[range]command[!] [args]". Pure: addresses
 * are resolved by the caller (vim_engine), which owns the buffer. */
#include <glib.h>

typedef enum {
    VIM_ADDR_NONE = 0,
    VIM_ADDR_NUMBER,       /* 12 */
    VIM_ADDR_CURRENT,      /* . */
    VIM_ADDR_LAST,         /* $ */
    VIM_ADDR_MARK,         /* 'a, '<, '> */
    VIM_ADDR_SEARCH_FWD,   /* /pat/ */
    VIM_ADDR_SEARCH_BWD,   /* ?pat? */
    VIM_ADDR_SEARCH_NEXT,  /* \/ : next match of the last pattern */
    VIM_ADDR_SEARCH_PREV   /* \? */
} VimAddrKind;

typedef struct {
    VimAddrKind kind;
    gint number;      /* VIM_ADDR_NUMBER (1-based) */
    gunichar mark;    /* VIM_ADDR_MARK */
    gchar* pattern;   /* VIM_ADDR_SEARCH_* (NULL: last pattern) */
    gint offset;      /* sum of the +N / -N that follow */
} VimAddr;

typedef struct {
    gint naddr;           /* 0, 1 or 2 addresses */
    VimAddr addr[2];
    gboolean semicolon;   /* "a;b": b is relative to a */
    gboolean whole;       /* % */
    gchar* name;          /* full command name, "" when only a range was typed */
    gchar* typed;         /* the name as typed */
    gboolean bang;
    gchar* args;          /* text after the name, leading blanks removed */
} VimExCmd;

/* Parse one command line. FALSE with *error set (owned) on a syntax error
 * or an unknown command. The name is expanded from its abbreviation
 * ("s" -> "substitute", "noh" -> "nohlsearch", "w" -> "write"). */
gboolean vim_ex_parse(const gchar* line, VimExCmd* cmd, gchar** error);
void vim_ex_cmd_clear(VimExCmd* cmd);

/* Split a :s / :g argument "/pat/rep/flags" (any non-alphanumeric
 * delimiter, \ escapes it). pattern / replacement / rest may be NULL
 * when missing. Returns FALSE when no delimiter starts the text. */
gboolean vim_ex_split_pattern(const gchar* args, gchar** pattern, gchar** replacement,
                              gchar** rest, gboolean with_replacement);

/* Names of the Ex commands, for completion (NULL-terminated, static) */
const gchar* const* vim_ex_command_names(void);

#endif /* VIM_EX_H */
