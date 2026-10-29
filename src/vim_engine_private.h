#ifndef VIM_ENGINE_PRIVATE_H
#define VIM_ENGINE_PRIVATE_H

/* Internal header of the Vim engine (vim_engine.c, vim_engine_ex.c) */
#include "vim_engine.h"
#include "vim_parse.h"

#define VIM_MAXCOL G_MAXINT

typedef enum { REG_CHAR = 0, REG_LINE, REG_BLOCK } RegType;

typedef struct {
    gchar* text;
    RegType type;
} VimRegister;

typedef enum { REG_YANK = 0, REG_DELETE } RegKind;

/* One buffer change, for the engine's own undo list */
typedef struct {
    gboolean insert;
    gint offset;      /* char offset */
    gint chars;       /* length in chars */
    gchar* text;
} VimUndoOp;

/* Changes of one Vim command (an Insert session is one step) */
typedef struct {
    GArray* ops;      /* VimUndoOp */
} VimUndoStep;

/* A range of text an operator applies to */
typedef struct {
    gint start;       /* char offsets, [start, end) */
    gint end;
    RegType type;     /* charwise, linewise (whole lines) or blockwise */
    gint line1, line2;  /* lines (block and linewise) */
    gint vcol1, vcol2;  /* block columns, inclusive (vcol2 VIM_MAXCOL: to the line end) */
} VimRange;

struct VimEngine {
    GtkTextBuffer* buf;
    VimHost host;
    gpointer hd;
    VimOptions opt;
    VimMode mode;

    GArray* pending;            /* gunichar: keys of the command being typed */

    /* Command line */
    gunichar cl_prompt;         /* ':' '/' '?' (0: closed) */
    gboolean cl_internal;       /* typed through vim_engine_feed (replay) */
    GString* cl_text;           /* internal command line text */
    VimMode cl_return_mode;     /* mode under the command line */
    gboolean cl_has_op;         /* operator waiting for a / ? motion */
    VimCmd cl_op;
    gint cl_origin;             /* cursor offset when the command line opened */
    gint hist_pos;
    gchar* cl_prefill;          /* text the command line opened with ('<,'>) */
    gint global_depth;          /* :global running */

    /* Visual mode */
    GtkTextMark* vis_anchor;
    gboolean vis_eol;           /* $ in Visual block mode */
    VimMode last_vis_mode;
    gboolean last_vis_eol;
    gint last_vis_cursor_off;   /* offsets for gv (marks '< '> hold the range) */
    gint last_vis_anchor_off;

    /* Column memory for j / k */
    gint curswant;
    gboolean want_eol;
    gint last_cursor_off;       /* cursor after the last command (-1: unknown) */

    /* Undo grouping and Insert session */
    gboolean change_open;
    GString* ins_keys;          /* what was typed in this Insert session */
    gint ins_count;
    gunichar ins_kind;
    GtkTextMark* ins_start;
    gboolean ins_ctrl_o;        /* Ctrl-O: back to Insert after one command */
    gboolean ins_ctrl_r;        /* Ctrl-R: next key names a register */
    gboolean ins_ctrl_v;        /* Ctrl-V: next key is inserted literally */
    gboolean ins_repeating;     /* replaying the session for a count */
    GString* replace_stack;     /* Replace mode: characters overwritten (0: appended) */
    /* Keyword completion (Ctrl-N / Ctrl-P) */
    GPtrArray* compl_words;
    gint compl_index;
    gchar* compl_prefix;
    gint compl_start;           /* offset where the completed word starts */

    /* Block insert (Visual block I / A / c) */
    gboolean blk_active;
    gint blk_first, blk_last, blk_vcol;
    gboolean blk_append, blk_eol;

    /* Dot repeat */
    GString* dot;               /* keys without count and register */
    gint dot_count;
    gunichar dot_reg;
    gboolean dot_visual;
    VimMode dot_vis_mode;
    gint dot_vis_lines;         /* lines - 1 */
    gint dot_vis_cols;          /* chars (single line) / end column / block width */
    GString* dot_pending;       /* command that entered Insert mode, waiting for Esc */
    gboolean dot_pending_visual;
    gint replaying;             /* > 0 while keys are replayed */

    /* Registers, marks, jumps */
    VimRegister regs[128];
    gchar* clip_own;            /* text we put on the clipboard ourselves */
    GtkTextMark* marks[128];
    GPtrArray* jumps;           /* GtkTextMark* */
    gint jump_pos;

    /* Search */
    gchar* last_pat;
    gboolean last_fwd;
    gboolean hl_active;         /* hlsearch shown (until :noh) */
    gboolean buffer_dirty;      /* buffer changed since the highlight */
    gchar* sub_pat;
    gchar* sub_rep;
    gchar* sub_flags;
    gunichar find_char;
    gunichar find_kind;         /* f F t T */

    /* Macros */
    gunichar rec_reg;
    GString* rec;
    gboolean rec_started;       /* the key that started recording is not recorded */
    gunichar last_macro;
    gchar* last_ex;
    gint macro_depth;

    GPtrArray* hist_cmd;
    GPtrArray* hist_search;

    GtkTextTag* tag_visual;
    GtkTextTag* tag_search;
    GtkTextTag* tag_incsearch;
    gulong sig_insert;
    gulong sig_delete;
    gint self_edit;             /* > 0: changes made by the engine are not recorded */
    GPtrArray* undo_steps;      /* VimUndoStep*, steps [0, undo_pos) are applied */
    gint undo_pos;
    VimUndoStep* undo_cur;      /* step being recorded */
    gboolean undoing;           /* applying undo / redo: not recorded */
    gboolean ext_action;        /* a user action of someone else (Org command, paste) is open */
    gint saved_pos;             /* undo_pos when the buffer was saved (-1: none) */
    gboolean beeped;            /* the last command failed (stops a running macro) */
    gboolean updating_visual;   /* re-entrancy guard of the Visual highlight */
};

/* vim_engine.c */
void vim_e_message(VimEngine* e, gboolean error, const gchar* fmt, ...) G_GNUC_PRINTF(3, 4);
void vim_e_set_mode(VimEngine* e, VimMode mode);
void vim_e_cursor(VimEngine* e, GtkTextIter* it);
void vim_e_set_cursor(VimEngine* e, const GtkTextIter* it);
void vim_e_set_cursor_line(VimEngine* e, gint line, gboolean first_nonblank);
gint vim_e_cursor_line(VimEngine* e);
gint vim_e_last_line(VimEngine* e);
void vim_e_begin_change(VimEngine* e);
void vim_e_end_change(VimEngine* e);
void vim_e_set_mark(VimEngine* e, gunichar name, const GtkTextIter* it);
gboolean vim_e_get_mark(VimEngine* e, gunichar name, GtkTextIter* it);
void vim_e_push_jump(VimEngine* e);
void vim_e_reg_set(VimEngine* e, gunichar name, const gchar* text, RegType type, RegKind kind);
gboolean vim_e_reg_get(VimEngine* e, gunichar name, VimRegister* out);
gchar* vim_e_line_text(VimEngine* e, gint line);
void vim_e_delete_lines(VimEngine* e, gint l1, gint l2, gunichar reg, gboolean to_register);
void vim_e_put_lines(VimEngine* e, gint after_line, const gchar* text);
void vim_e_shift_lines(VimEngine* e, gint l1, gint l2, gint amount);
void vim_e_join_lines(VimEngine* e, gint l1, gint l2, gboolean spaces);
void vim_e_align_lines(VimEngine* e, gint l1, gint l2, gchar how, gint width);
void vim_e_feed_internal(VimEngine* e, const gchar* keys);
void vim_e_run_motion_after_search(VimEngine* e, const GtkTextIter* target);
void vim_e_visual_bounds(VimEngine* e, GtkTextIter* start, GtkTextIter* end);
void vim_e_update_visual(VimEngine* e);
void vim_e_insert_text(VimEngine* e, GtkTextIter* at, const gchar* text);
void vim_e_delete(VimEngine* e, GtkTextIter* s, GtkTextIter* t);
void vim_e_state_changed(VimEngine* e);

/* vim_engine_ex.c */
void vim_e_cmdline_open(VimEngine* e, gunichar prompt, const gchar* text);
gboolean vim_e_search(VimEngine* e, const gchar* pattern, gboolean forward, gint count,
                      const GtkTextIter* from, GtkTextIter* match, gboolean quiet);
void vim_e_highlight_search(VimEngine* e);
void vim_e_history_add(VimEngine* e, gunichar prompt, const gchar* text);
gboolean vim_e_word_under_cursor(VimEngine* e, GtkTextIter* ws, GtkTextIter* we);
void vim_e_substitute_repeat(VimEngine* e, gint l1, gint l2, gboolean keep_flags, gboolean whole);

#endif /* VIM_ENGINE_PRIVATE_H */
