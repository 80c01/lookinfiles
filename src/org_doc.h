#ifndef ORG_DOC_H
#define ORG_DOC_H

/* Org-mode document structure and editing commands on an array of lines.
 * Pure GLib (no widgets): the editor turns its buffer into lines, runs a
 * command, and writes back the lines that changed.
 *
 * Commands take the lines (GPtrArray of gchar*, owned, g_free) and the
 * cursor (OrgPos, column in characters), change both in place and return
 * TRUE; on FALSE nothing changed and *msg (when not NULL) may hold an
 * explanation to show the user (newly allocated). */
#include <glib.h>

typedef struct {
    gint line;
    gint col; /* characters */
} OrgPos;

/* Two adjacent blocks of lines that a move command swapped:
 * [start, mid) and [mid, end) became [start, start + end - mid) and the rest. */
typedef struct {
    gint start, mid, end;
} OrgSwap;

/* TODO keywords: #+TODO / #+SEQ_TODO / #+TYP_TODO lines, default "TODO | DONE",
 * and #+STARTUP: logdone */
typedef struct {
    GPtrArray* sequences; /* GPtrArray* of keyword sequences (GPtrArray of gchar*) */
    GHashTable* done;     /* keywords meaning done */
    gboolean log_done;    /* add CLOSED: [timestamp] when an entry is done */
} OrgTodoConfig;

typedef struct {
    gint level;        /* number of stars, 0 when not a heading */
    gchar* keyword;    /* TODO keyword or NULL */
    gchar priority;    /* 'A'..'Z' or 0 */
    gchar* title;      /* text without keyword, priority and tags */
    gchar* tags;       /* ":a:b:" or NULL */
} OrgHeading;

typedef struct {
    gint indent;       /* leading whitespace characters */
    gchar bullet[12];  /* "-", "+", "*", "1." or "1)" as written */
    gboolean ordered;
    gint number;
    gchar checkbox;    /* ' ', 'X', '-' or 0 when none */
    gint content;      /* byte offset of the item text */
} OrgItem;

typedef enum {
    ORG_FOLD_NONE = 0,
    ORG_FOLD_HEADING, /* subtree under a heading */
    ORG_FOLD_DRAWER,  /* :PROPERTIES: ... :END: */
    ORG_FOLD_BLOCK    /* #+begin_x ... #+end_x */
} OrgFoldKind;

/* ---- Lines ---- */
GPtrArray* org_lines_new(const gchar* text);
gchar* org_lines_join(GPtrArray* lines);
const gchar* org_line(GPtrArray* lines, gint i); /* "" when out of range */

/* ---- TODO configuration ---- */
OrgTodoConfig* org_todo_config_new(GPtrArray* lines);
void org_todo_config_free(OrgTodoConfig* cfg);
gboolean org_todo_is_keyword(const OrgTodoConfig* cfg, const gchar* word);
gboolean org_todo_is_done(const OrgTodoConfig* cfg, const gchar* word);

/* ---- Headings ---- */
gint org_heading_level(const gchar* line);
gboolean org_heading_parse(const gchar* line, const OrgTodoConfig* cfg, OrgHeading* h);
void org_heading_clear(OrgHeading* h);
/* Heading line from its parts; tags end at column 77 when they fit */
gchar* org_heading_build(const OrgHeading* h);
/* Heading at or above line, -1 if none */
gint org_heading_at(GPtrArray* lines, gint line);
/* Parent heading of heading line h, -1 if none */
gint org_heading_parent(GPtrArray* lines, gint h);
/* End (exclusive) of the subtree of heading h / of its own entry text */
gint org_subtree_end(GPtrArray* lines, gint h);
gint org_entry_end(GPtrArray* lines, gint h);
/* Next / previous heading from line (dir +1 / -1); same_level: siblings only
 * (stops at the parent). -1 if none. */
gint org_heading_next(GPtrArray* lines, gint line, gint dir, gboolean same_level);
/* Heading whose title is title (keyword, priority and tags ignored) */
gint org_heading_find_title(GPtrArray* lines, const OrgTodoConfig* cfg, const gchar* title);

/* What TAB folds on line, and the last line (inclusive) it hides */
OrgFoldKind org_fold_kind(GPtrArray* lines, gint line, gint* last);

/* ---- Structure editing ---- */
gboolean org_promote(GPtrArray* lines, OrgPos* pos, gint dir, gboolean subtree, gchar** msg);
gboolean org_move_subtree(GPtrArray* lines, OrgPos* pos, gint dir, OrgSwap* swap, gchar** msg);
/* New heading: respect_content puts it after the subtree; todo adds the first
 * TODO keyword. On a heading at column 0 it is inserted above. */
gboolean org_insert_heading(GPtrArray* lines, OrgPos* pos, const OrgTodoConfig* cfg,
                            gboolean respect_content, gboolean todo);
/* Turn the line into a heading, or a heading into plain text */
gboolean org_toggle_heading(GPtrArray* lines, OrgPos* pos);

/* ---- TODO, priority, tags ---- */
/* Cycle the keyword of heading line h (dir +1 / -1): logs CLOSED, applies
 * repeaters, updates statistics cookies. */
gboolean org_todo_cycle(GPtrArray* lines, OrgPos* pos, gint h, const OrgTodoConfig* cfg,
                        gint dir, GDateTime* now, gchar** msg);
/* Set the keyword of heading h (NULL removes it), same side effects */
gboolean org_todo_set(GPtrArray* lines, OrgPos* pos, gint h, const OrgTodoConfig* cfg,
                      const gchar* keyword, GDateTime* now, gchar** msg);
/* Priority of heading h: dir +1 raises (none -> A -> none), -1 lowers;
 * set: 'A'..'Z' or ' ' to remove (dir ignored when set != 0) */
gboolean org_priority(GPtrArray* lines, OrgPos* pos, gint h, const OrgTodoConfig* cfg,
                      gint dir, gchar set);
/* Tags of heading h as ":a:b:" (NULL if none) / replace them ("a b", "a:b",
 * ":a:b:"; empty removes). Re-aligns the tags. */
gchar* org_get_tags(GPtrArray* lines, gint h, const OrgTodoConfig* cfg);
gboolean org_set_tags(GPtrArray* lines, OrgPos* pos, gint h, const OrgTodoConfig* cfg,
                      const gchar* tags);

/* ---- Planning, clock ---- */
/* Set SCHEDULED / DEADLINE / CLOSED of heading h to ts_text (NULL removes) */
gboolean org_set_planning(GPtrArray* lines, OrgPos* pos, gint h, const gchar* keyword,
                          const gchar* ts_text);
/* Start a clock on heading h (stops a running one first) / stop the running
 * clock. *msg reports what happened. */
gboolean org_clock_in(GPtrArray* lines, OrgPos* pos, gint h, GDateTime* now, gchar** msg);
gboolean org_clock_out(GPtrArray* lines, OrgPos* pos, GDateTime* now, gchar** msg);
/* Line of the running clock, -1 if none */
gint org_clock_running(GPtrArray* lines);

/* ---- Lists and checkboxes ---- */
gboolean org_item_parse(const gchar* line, OrgItem* it);
/* End (exclusive) of item i with its continuation lines and sub-items */
gint org_item_end(GPtrArray* lines, gint i);
/* Item at or containing line, -1 if none */
gint org_item_at(GPtrArray* lines, gint line);
gint org_item_parent(GPtrArray* lines, gint i);
gboolean org_insert_item(GPtrArray* lines, OrgPos* pos, gboolean checkbox);
gboolean org_indent_item(GPtrArray* lines, OrgPos* pos, gint dir, gchar** msg);
gboolean org_move_item(GPtrArray* lines, OrgPos* pos, gint dir, OrgSwap* swap, gchar** msg);
/* Cycle the bullet of the whole list ("-" "+" "*" "1." "1)") */
gboolean org_cycle_bullet(GPtrArray* lines, OrgPos* pos, gint dir);
/* Renumber the ordered list containing item i */
void org_renumber_list(GPtrArray* lines, gint i);
/* Toggle the checkbox of the item on the cursor line (sub-items follow,
 * parents become [-] / [X]), then update cookies */
gboolean org_toggle_checkbox(GPtrArray* lines, OrgPos* pos, gchar** msg);
/* Turn a plain line into "- " item, or cycle an item's bullet */
gboolean org_ctrl_c_minus(GPtrArray* lines, OrgPos* pos);
/* Recompute every [n/m] and [p%] cookie; TRUE if one changed */
gboolean org_update_statistics(GPtrArray* lines, const OrgTodoConfig* cfg);

/* ---- Links ---- */
/* Link under byte column col: [[target][desc]], [[target]], <url> or a bare
 * http(s)/file URL. Returns the target (newly allocated) or NULL. */
gchar* org_link_at(const gchar* line, gint col);
gchar* org_link_format(const gchar* target, const gchar* desc);
/* Line an internal link points to: "*Heading", "#custom-id", or a
 * <<target>> / #+NAME: / heading title. -1 if not found. */
gint org_link_find_internal(GPtrArray* lines, const OrgTodoConfig* cfg, const gchar* target);

/* ---- Templates ---- */
/* "<s" (+ language), "<e", "<q", "<c", "<v", "<x", "<C", "<l", "<h", "<a" alone
 * on the line with the cursor at its end: expand into a #+begin/#+end block */
gboolean org_expand_template(GPtrArray* lines, OrgPos* pos);

/* ---- Utilities ---- */
gint org_utf8_width(const gchar* s);
/* Byte offset of character column col in s (clamped) and back */
gint org_col_to_byte(const gchar* s, gint col);
gint org_byte_to_col(const gchar* s, gint byte);

#endif /* ORG_DOC_H */
