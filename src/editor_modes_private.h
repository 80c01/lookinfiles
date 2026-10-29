#ifndef EDITOR_MODES_PRIVATE_H
#define EDITOR_MODES_PRIVATE_H

/* Internal header shared by editor_modes.c and the per-mode files
 * (editor_notepad.c, editor_emacs.c, editor_vim.c, editor_org.c). Other modules must
 * use editor_modes.h instead. */
#include "editor_modes.h"

/* Sub-modes for Vim */
typedef enum {
    VIM_SUB_NORMAL = 0,
    VIM_SUB_INSERT,
    VIM_SUB_VISUAL,
    VIM_SUB_VISUAL_LINE,
    VIM_SUB_COMMAND,
    VIM_SUB_SEARCH_FWD,
    VIM_SUB_SEARCH_BWD
} VimSubMode;

/* Pending operators for Vim */
typedef enum {
    VIM_OP_NONE = 0,
    VIM_OP_DELETE,
    VIM_OP_YANK,
    VIM_OP_CHANGE,
    VIM_OP_G,
    VIM_OP_Z,
    VIM_OP_REPLACE_CHAR,
    VIM_OP_INDENT,
    VIM_OP_UNINDENT
} VimPendingOp;

/* Emacs key prefix state */
typedef enum {
    EMACS_PREFIX_NONE = 0,
    EMACS_PREFIX_CTRL_X,
    EMACS_PREFIX_META_G
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
typedef struct {
    EditorTab* tab;
    AppData* app;

    gboolean tags_initialized;

    /* --- Notepad UI & State --- */
    GtkWidget* np_box;
    GtkWidget* np_find_bar;
    GtkWidget* np_find_entry;
    GtkWidget* np_replace_entry;
    GtkWidget* np_replace_box;
    GtkWidget* np_status_label;
    GtkWidget* np_info_label;
    GtkWidget* np_save_bar;
    GtkWidget* np_save_entry;

    /* --- Emacs UI & State --- */
    GtkWidget* emacs_box;
    GtkWidget* emacs_modeline;
    GtkWidget* emacs_ml_status;
    GtkWidget* emacs_ml_bufname;
    GtkWidget* emacs_ml_modes;
    GtkWidget* emacs_ml_pos;
    GtkWidget* emacs_ml_linecol;
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
    EmacsPromptFunc emacs_prompt_cb; /* EMACS_MINI_CALLBACK */
    gchar* emacs_prompt_data;        /* owned, given to the callback */
    GPtrArray* emacs_completions;    /* Tab completion candidates (gchar*), or NULL */
    gboolean emacs_complete_word;    /* complete the last word (after ':' or ' ') only */

    /* --- Org-mode (editor_org.c) --- */
    OrgPrefix org_prefix;
    gboolean org_revealing;          /* re-entrancy guard of the cursor reveal */

    /* --- Vim UI & State --- */
    GtkWidget* vim_box;
    GtkWidget* vim_statusline;
    GtkWidget* vim_sl_mode;
    GtkWidget* vim_sl_file;
    GtkWidget* vim_sl_ruler;
    GtkWidget* vim_commandline;
    GtkWidget* vim_cl_prompt;
    GtkWidget* vim_cl_entry;
    GtkWidget* vim_cl_echo;

    VimSubMode vim_submode;
    VimPendingOp vim_pending_op;
    gint vim_count;
    gchar* vim_last_search;
    gboolean vim_search_fwd;
    GtkTextMark* vim_visual_start_mark;  /* visual mode anchor ('v' / 'V' position) */
    GtkTextMark* vim_visual_cursor_mark; /* visual mode cursor (the selection covers it) */

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

/* Mode state transitions used when switching modes */
void emacs_close_minibuffer(EditorTab* tab);
/* End the incremental search: accept leaves the cursor at the match */
void emacs_isearch_exit(EditorTab* tab, gboolean accept);
void vim_enter_submode(EditorTab* tab, VimSubMode submode);
void vim_cursor_get(EditorTab* tab, GtkTextIter* it);

#endif /* EDITOR_MODES_PRIVATE_H */
