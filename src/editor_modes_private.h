#ifndef EDITOR_MODES_PRIVATE_H
#define EDITOR_MODES_PRIVATE_H

/* Internal header shared by editor_modes.c and the per-mode files
 * (editor_notepad.c, editor_emacs.c, editor_vim.c, editor_org.c). Other modules must
 * use editor_modes.h instead. */
#include "editor_modes.h"

typedef struct VimEngine VimEngine;

/* Emacs key prefix state */
typedef enum {
    EMACS_PREFIX_NONE = 0,
    EMACS_PREFIX_CTRL_X,
    EMACS_PREFIX_META_G,
    EMACS_PREFIX_CTRL_H
} EmacsPrefix;

/* Emacs minibuffer mode */
typedef enum {
    EMACS_MINI_NONE = 0,
    EMACS_MINI_ISEARCH_FWD,
    EMACS_MINI_ISEARCH_BWD,
    EMACS_MINI_QUERY_REPLACE_FIND,
    EMACS_MINI_QUERY_REPLACE_REP,
    EMACS_MINI_WRITE_FILE,
    EMACS_MINI_GOTO_LINE,
    EMACS_MINI_MX,
    EMACS_MINI_CALLBACK /* emacs_prompt(): the answer goes to a callback */
} EmacsMiniMode;

/* Org-mode prefix keys (editor_org.c) */
typedef enum {
    ORG_PREFIX_NONE = 0,
    ORG_PREFIX_C_C,      /* C-c */
    ORG_PREFIX_C_C_C_X,  /* C-c C-x */
    ORG_PREFIX_SPARSE,   /* C-c / */
    ORG_PREFIX_PRIORITY  /* C-c , */
} OrgPrefix;

/* Answer of emacs_prompt(): text typed, data given to emacs_prompt() */
typedef void (*EmacsPromptFunc)(EditorTab* tab, const gchar* text, gchar* data);

/* Mode-specific data for one EditorTab */
typedef struct EditorModeState {
    EditorTab* tab;
    AppData* app;

    gboolean tags_initialized;

    /* --- Notepad UI & State --- */
    GtkWidget* np_box;
    GtkWidget* np_find_bar;
    GtkWidget* np_find_entry;
    GtkWidget* np_replace_entry;
    GtkWidget* np_replace_box;
    GtkWidget* np_file_label;   /* branch, relative path, modified mark */
    GtkWidget* np_status_label;
    GtkWidget* np_info_label;
    GtkWidget* np_save_bar;
    GtkWidget* np_save_entry;
    GtkWidget* np_goto_bar;
    GtkWidget* np_goto_entry;
    gboolean np_chord_k;      /* Ctrl+K pressed: waiting for the second key (Ctrl+I) */
    /* Find bar: current match (char offsets, -1 for none), shown with a tag
     * while the bar is open; the cursor when the bar opened (typing searches
     * from there) */
    gint np_ms, np_me;
    gint np_origin;
    gboolean np_setting;      /* the find entry text is set by the program */
    gboolean np_replacing;    /* the buffer is changed by Replace */

    /* --- Emacs UI & State --- */
    GtkWidget* emacs_box;
    GtkWidget* emacs_modeline;
    GtkWidget* emacs_ml_status;
    GtkWidget* emacs_ml_bufname;
    GtkWidget* emacs_ml_modes;
    GtkWidget* emacs_ml_pos;
    GtkWidget* emacs_ml_linecol;
    GtkWidget* emacs_ml_vc;     /* "Git-main", as Emacs VC */
    GtkWidget* emacs_minibuffer;
    GtkWidget* emacs_mb_prompt;
    GtkWidget* emacs_mb_entry;
    GtkWidget* emacs_mb_echo;

    EmacsPrefix emacs_prefix;
    EmacsMiniMode emacs_mini_mode;
    gboolean emacs_mark_active;
    GtkTextMark* emacs_mark;
    gchar* emacs_last_search;
    gchar* emacs_qr_find;
    gchar* emacs_qr_rep;
    gint emacs_qr_count;
    GtkTextMark* emacs_search_start_mark;
    /* Incremental search (C-s / C-r): buffer offsets, the buffer does not
     * change while searching */
    GArray* emacs_is_steps;          /* EmacsIsearchStep history, popped by DEL */
    gint emacs_is_origin;            /* cursor when the search started */
    gint emacs_is_ms, emacs_is_me;   /* current match */
    gboolean emacs_is_has;           /* a match is shown */
    gboolean emacs_is_failing;       /* the last search found nothing */
    gboolean emacs_is_wrapped;       /* searched again from the other end */
    gboolean emacs_is_forward;
    gboolean emacs_is_setting;       /* the entry text is being set by the search itself */
    gboolean emacs_block_on;         /* block cursor drawn (Emacs and Org-mode modes) */
    GtkWidget* emacs_block_eol;      /* block shown at a line end (text view overlay) */
    EmacsPromptFunc emacs_prompt_cb; /* EMACS_MINI_CALLBACK */
    gchar* emacs_prompt_data;        /* owned, given to the callback */
    GPtrArray* emacs_completions;    /* Tab completion candidates (gchar*), or NULL */
    gboolean emacs_complete_word;    /* complete the last word (after ':' or ' ') only */
    gboolean emacs_universal_arg;    /* C-u typed: the next command gets a prefix argument */
    gboolean emacs_last_set_mark;    /* previous command was C-SPC (C-SPC C-SPC toggles) */
    gint emacs_recenter_step;        /* next C-l position: 0 middle, 1 top, 2 bottom */

    /* --- Org-mode (editor_org.c) --- */
    OrgPrefix org_prefix;
    gboolean org_revealing;          /* re-entrancy guard of the cursor reveal */

    /* --- Vim UI & State --- */
    GtkWidget* vim_box;
    GtkWidget* vim_statusline;
    GtkWidget* vim_sl_mode;
    GtkWidget* vim_sl_file;
    GtkWidget* vim_sl_ruler;
    GtkWidget* vim_sl_info;     /* diagnostics, filetype, server, encoding and format */
    GtkWidget* vim_commandline;
    GtkWidget* vim_cl_prompt;
    GtkWidget* vim_cl_entry;
    GtkWidget* vim_cl_echo;

    GtkWidget* vim_sl_showcmd;
    VimEngine* vim;                  /* the editing engine (vim_engine.h), NULL outside Vim mode */
    gboolean vim_cl_setting;         /* the entry text is set by us, not typed */
    gboolean vim_cl_ctrl_r;          /* Ctrl-R typed in the command line: a register follows */
    gboolean vim_echo_sticky;        /* a multi-line listing is shown until the next key */
    gboolean vim_ctrl_w;             /* Normal-mode Ctrl+W typed: window command waiting */
    gchar* vim_hist_prefix;          /* text typed before browsing the history */
    GtkWidget* vim_wild_label;       /* wildmenu: Tab completions shown over the statusline */
    gchar** vim_wild;                /* completions of the word being completed, or NULL */
    gchar* vim_wild_orig;            /* command line text before the first Tab */
    gsize vim_wild_at;               /* byte offset of the completed word in it */
    gint vim_wild_index;             /* shown completion, -1 for the original text */
    gint vim_wild_first;             /* first completion visible in the wildmenu */
    GCancellable* vim_clip_cancel;
    GdkClipboard* vim_clipboard;     /* watched for text copied elsewhere ("+ register) */
    gulong vim_clip_handler;

} EditorModeState;

/* Mode bar helpers: keep bars from imposing a minimum width (editor_modes.c) */
void editor_bar_label_shrinkable(GtkWidget* label, PangoEllipsizeMode mode);
GtkWidget* editor_bar_clip(GtkWidget* child);

/* Per-mode UI construction (called once from editor_modes_init_tab) */
void notepad_build_ui(EditorTab* tab, EditorModeState* st);
void emacs_build_ui(EditorTab* tab, EditorModeState* st);
void vim_build_ui(EditorTab* tab, EditorModeState* st);

/* Per-mode key handlers (dispatched by editor_modes_handle_key) */
gboolean notepad_handle_key(EditorTab* tab, EditorModeState* st,
                            guint keyval, GdkModifierType state);
gboolean emacs_handle_key(EditorTab* tab, EditorModeState* st,
                          guint keyval, GdkModifierType state);
gboolean vim_handle_key(EditorTab* tab, EditorModeState* st,
                        guint keyval, GdkModifierType state);

/* Org-mode editing (editor_org.c), active in tabs showing an Org file */
gboolean orgmode_editor_is_org(EditorTab* tab);
void orgmode_editor_init(EditorTab* tab);
/* Fold as at startup in Org-mode, unfold everything otherwise */
void orgmode_editor_reset_visibility(EditorTab* tab);
gboolean orgmode_editor_handle_key(AppData* app, EditorTab* tab, guint keyval, GdkModifierType state);
/* TRUE when tab needs keyval although the window binds it (pending prefix,
 * Alt+Left/Right on a heading, item or table) */
gboolean orgmode_editor_claims_key(EditorTab* tab, guint keyval, GdkModifierType state);
/* Run an Org command by its Emacs name ("org-todo", "org-cycle"...) */
gboolean orgmode_editor_command(EditorTab* tab, const gchar* name);

/* Tab completion for the open minibuffer prompt: candidates (gchar*, taken
 * over); last_word completes only the text after the last ':' or ' ' */
void emacs_prompt_set_completions(EditorTab* tab, GPtrArray* candidates, gboolean last_word);
/* Org command names for M-x completion (when tab shows an Org file) */
void orgmode_editor_add_command_names(EditorTab* tab, GPtrArray* names);

/* Ask a question in the Emacs minibuffer; cb gets the answer (data is
 * owned by the prompt and freed after the callback or on cancel) */
void emacs_prompt(EditorTab* tab, const gchar* prompt, const gchar* initial,
                  EmacsPromptFunc cb, gchar* data);

/* Close the Notepad bars and drop the match highlight (leaving Notepad mode) */
void notepad_reset(EditorTab* tab);

/* Mode state transitions used when switching modes */
void emacs_close_minibuffer(EditorTab* tab);
/* Draw an Emacs block cursor in the tab (TRUE) or GTK's thin caret (FALSE) */
void emacs_block_cursor_set(EditorTab* tab, gboolean on);
/* End the incremental search: accept leaves the cursor at the match */
void emacs_isearch_exit(EditorTab* tab, gboolean accept);
/* Vim mode (editor_vim.c): create the engine when the tab enters Vim mode,
 * free it when it leaves */
void vim_activate(EditorTab* tab);
void vim_deactivate(EditorTab* tab);
void vim_free(EditorTab* tab);
/* Cursor of the Vim engine (the moving end of a Visual selection) */
void vim_cursor_get(EditorTab* tab, GtkTextIter* it);
/* Insert or Replace mode: typed text goes into the buffer */
gboolean vim_is_inserting(EditorTab* tab);
/* Ctrl keys the window binds that Vim needs (Ctrl-R) */
gboolean vim_claims_key(EditorTab* tab, guint keyval, GdkModifierType state);
/* Drop a half-typed command (Ctrl+W, 2d, g...); TRUE if there was one */
gboolean vim_cancel_pending(EditorTab* tab);
/* Next (1) / previous (-1) tab of the tab's notebook, focus in the new page */
void editor_modes_step_tab(EditorTab* tab, gint step);

#endif /* EDITOR_MODES_PRIVATE_H */
