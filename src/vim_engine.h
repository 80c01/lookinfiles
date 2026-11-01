#ifndef VIM_ENGINE_H
#define VIM_ENGINE_H

/* Vim editing engine on a GtkTextBuffer, without widgets: modes, the
 * operator / motion grammar, text objects, registers, marks, counts, dot
 * repeat, macros, search, Visual (char, line, block) and Ex commands.
 *
 * The host (editor_vim.c) feeds keys and draws the result; it implements a
 * few callbacks for what needs the view or the application: messages, the
 * command line entry, scrolling, the clipboard, :w / :q.
 *
 * In Insert mode, text keys are left to the host's text view (input
 * methods, completion): vim_engine_feed() returns FALSE for them. The
 * engine records what the view inserted from the buffer signals, so dot
 * repeat and macros replay it. */
#include <gtk/gtk.h>

typedef struct VimEngine VimEngine;

typedef enum {
    VIM_MODE_NORMAL = 0,
    VIM_MODE_INSERT,
    VIM_MODE_REPLACE,
    VIM_MODE_VISUAL,
    VIM_MODE_VISUAL_LINE,
    VIM_MODE_VISUAL_BLOCK,
    VIM_MODE_CMDLINE,          /* ':' '/' '?' being typed */
    VIM_MODE_OPERATOR_PENDING  /* reported only: d, c, y... waiting for a motion */
} VimMode;

typedef enum {
    VIM_SCROLL_CURSOR_CENTER = 0,  /* zz z. */
    VIM_SCROLL_CURSOR_TOP,         /* zt z<CR> */
    VIM_SCROLL_CURSOR_BOTTOM,      /* zb z- */
    VIM_SCROLL_LINES               /* scroll the view by n lines (negative: up) */
} VimScroll;

typedef struct {
    /* One-line message in the command line area (error: shown in red) */
    void (*message)(gpointer data, const gchar* text, gboolean error);
    /* Mode, pending keys or recording state changed: redraw the status line */
    void (*state_changed)(gpointer data);
    /* Show the command line entry with prompt (':' '/' '?') and text */
    void (*cmdline_open)(gpointer data, gunichar prompt, const gchar* text);
    /* Ex commands the engine does not run itself (write, quit, edit,
     * tabnext, set number...). Return TRUE when done; *error: message. */
    gboolean (*ex_command)(gpointer data, const gchar* name, gboolean bang,
                           const gchar* args, gchar** error);
    /* Scroll the view */
    void (*scroll)(gpointer data, VimScroll how, gint lines);
    /* First and last fully visible lines (0-based); FALSE when unknown */
    gboolean (*visible_lines)(gpointer data, gint* first, gint* last);
    /* Put text on the system clipboard ("+ and "* registers) */
    void (*clipboard_set)(gpointer data, const gchar* text);
    /* Name of the file for "% and Ctrl-G */
    const gchar* (*file_name)(gpointer data);
    /* gd, gD, Ctrl-]: go to the definition (declaration) of word, the
     * identifier under the cursor. TRUE when the host looks it up (code
     * files); FALSE: gd / gD search the buffer as Vim does. */
    gboolean (*goto_definition)(gpointer data, const gchar* word, gboolean declaration);
    /* Ctrl-O / Tab past the ends of the buffer's jump list: continue in the
     * host's history across files. TRUE when it moved. */
    gboolean (*jump_history)(gpointer data, gboolean older);
    /* K: show what word (the identifier under the cursor) is. TRUE when
     * the host shows something. */
    gboolean (*keyword_help)(gpointer data, const gchar* word);
    /* gr: list the references of the identifier under the cursor. TRUE when
     * the host looks them up. */
    gboolean (*references)(gpointer data);
} VimHost;

typedef struct {
    gboolean ignorecase;
    gboolean smartcase;
    gboolean hlsearch;
    gboolean incsearch;
    gboolean wrapscan;
    gboolean expandtab;
    gboolean autoindent;
    gboolean unnamedplus;   /* clipboard=unnamedplus: yanks also go to "+ */
    gint shiftwidth;
    gint tabstop;
    gint softtabstop;       /* Tab / BS width in Insert mode (0: off, < 0: shiftwidth) */
    gint textwidth;         /* gq width (0: 79) */
} VimOptions;

VimEngine* vim_engine_new(GtkTextBuffer* buffer, const VimHost* host, gpointer host_data);
void vim_engine_free(VimEngine* engine);
/* The data passed to the host callbacks: the views of a split file share one
 * engine (one undo history), the focused view hosts it */
void vim_engine_set_host_data(VimEngine* engine, gpointer host_data);
gpointer vim_engine_get_host_data(VimEngine* engine);

/* Feed one key (vim_keys.h). FALSE: the host's text view should handle
 * the key (text typed in Insert mode, or keys Vim does not use). */
gboolean vim_engine_feed(VimEngine* engine, gunichar key);
/* Feed a key string as if typed (tests, :normal, macros); text typed in
 * Insert mode is inserted by the engine itself */
void vim_engine_feed_keys(VimEngine* engine, const gchar* keys);

VimMode vim_engine_mode(VimEngine* engine);
/* Mode text for the status line: "NORMAL", "-- INSERT --", "-- VISUAL LINE --"... */
const gchar* vim_engine_mode_label(VimEngine* engine);
/* Keys of the command being typed (showcmd), "" when none */
gchar* vim_engine_pending_keys(VimEngine* engine);
/* Register being recorded into (q), 0 when not recording */
gunichar vim_engine_recording(VimEngine* engine);
/* The cursor (in Visual mode, the moving end of the selection) */
void vim_engine_get_cursor(VimEngine* engine, GtkTextIter* iter);

/* Drop the keys of a command being typed (count, operator, g, z...), keep
 * the mode; TRUE if there were some */
gboolean vim_engine_cancel_pending(VimEngine* engine);

/* Leave any mode for Normal (mode switch, tab focus changes) */
void vim_engine_reset(VimEngine* engine);

/* Command line typed in the host entry */
void vim_engine_cmdline_submit(VimEngine* engine, const gchar* text);
void vim_engine_cmdline_cancel(VimEngine* engine);
/* Text changed while typing (incremental search) */
void vim_engine_cmdline_changed(VimEngine* engine, const gchar* text);
/* History entry older (dir < 0) or newer (dir > 0) than the shown one,
 * starting with prefix; NULL when there is none */
gchar* vim_engine_cmdline_history(VimEngine* engine, gint dir, const gchar* prefix);
/* Completions of the last word of the Ex command line text (command
 * names, :set options), sorted; the word starts at *word_start. NULL when
 * nothing matches, else free with g_strfreev() */
gchar** vim_engine_cmdline_matches(VimEngine* engine, const gchar* text, gsize* word_start);

/* Run one Ex command line (without the leading ':') */
void vim_engine_ex(VimEngine* engine, const gchar* line);

/* The system clipboard changed (text read by the host) */
void vim_engine_clipboard_changed(VimEngine* engine, const gchar* text);

VimOptions* vim_engine_options(VimEngine* engine);

/* Register contents (NULL when empty); linewise when it holds whole lines */
const gchar* vim_engine_register(VimEngine* engine, gunichar name, gboolean* linewise);

#endif /* VIM_ENGINE_H */
