/* Org-mode editing in editor tabs showing an Org file: visibility cycling,
 * structure editing, TODO keywords, priorities, tags, lists, checkboxes,
 * tables, timestamps, scheduling, clocking, links and sparse trees, with
 * the Emacs Org-mode keys. The editing logic lives in org_doc / org_table /
 * org_time; this file maps keys to commands and applies their result to the
 * buffer (one undo step, folds and cursor kept). */
#include "editor_modes_private.h"
#include "editor.h"
#include "core/file_types.h"
#include "org_doc.h"
#include "org_table.h"
#include "org_time.h"

#include <string.h>

#define FOLD_TAG "org_folded"
#define GLOBAL_STATE_KEY "org_editor_global_fold"

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

gboolean orgmode_editor_is_org(EditorTab* tab) {
    return tab != NULL && tab->buffer != NULL &&
           (tab->mode == EDITOR_MODE_ORGMODE ||
            (!tab->text_mode && tab->name != NULL && is_org_file(tab->name)));
}

static EditorModeState* state_of(EditorTab* tab) {
    return tab->mode_state;
}

static gboolean emacs_like(EditorTab* tab) {
    return tab->mode == EDITOR_MODE_EMACS || tab->mode == EDITOR_MODE_ORGMODE;
}

static void org_echo(EditorTab* tab, const gchar* msg) {
    EditorModeState* st = state_of(tab);
    if (tab->mode == EDITOR_MODE_VIM) {
        vim_echo(tab, msg);
    } else if (tab->mode == EDITOR_MODE_NOTEPAD) {
        if (st != NULL && st->np_status_label != NULL) {
            gtk_label_set_text(GTK_LABEL(st->np_status_label), msg != NULL ? msg : "");
        }
    } else {
        emacs_echo(tab, msg);
    }
}

static GPtrArray* buffer_lines(EditorTab* tab) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(tab->buffer, &s, &e);
    gchar* text = gtk_text_buffer_get_text(tab->buffer, &s, &e, TRUE);
    GPtrArray* lines = org_lines_new(text);
    g_free(text);
    return lines;
}

static OrgPos cursor_pos(EditorTab* tab) {
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    OrgPos pos = {gtk_text_iter_get_line(&it), gtk_text_iter_get_line_offset(&it)};
    return pos;
}

static gchar* line_text(EditorTab* tab, gint line) {
    GtkTextIter s, e;
    gtk_text_buffer_get_iter_at_line(tab->buffer, &s, line);
    e = s;
    if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
    return gtk_text_buffer_get_text(tab->buffer, &s, &e, TRUE);
}

static gboolean has_selection(EditorTab* tab) {
    return gtk_text_buffer_get_has_selection(tab->buffer);
}

static void place_cursor(EditorTab* tab, gint line, gint col) {
    GtkTextIter it;
    gint count = gtk_text_buffer_get_line_count(tab->buffer);
    if (line >= count) line = count - 1;
    gtk_text_buffer_get_iter_at_line_offset(tab->buffer, &it, MAX(line, 0), MAX(col, 0));
    gtk_text_buffer_place_cursor(tab->buffer, &it);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(tab->view),
                                       gtk_text_buffer_get_insert(tab->buffer));
}

static gboolean is_modifier_key(guint keyval) {
    return (keyval >= GDK_KEY_Shift_L && keyval <= GDK_KEY_Hyper_R) ||
           keyval == GDK_KEY_ISO_Level3_Shift || keyval == GDK_KEY_ISO_Level5_Shift ||
           keyval == GDK_KEY_Mode_switch || keyval == GDK_KEY_Num_Lock;
}

/* ------------------------------------------------------------------------- */
/* Folding: an invisible tag on whole lines                                  */
/* ------------------------------------------------------------------------- */

static GtkTextTag* fold_tag(GtkTextBuffer* b) {
    GtkTextTag* t = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(b), FOLD_TAG);
    if (t == NULL) t = gtk_text_buffer_create_tag(b, FOLD_TAG, "invisible", TRUE, NULL);
    return t;
}

static void line_start_iter(GtkTextBuffer* b, gint line, GtkTextIter* it) {
    if (line >= gtk_text_buffer_get_line_count(b)) {
        gtk_text_buffer_get_end_iter(b, it);
    } else {
        gtk_text_buffer_get_iter_at_line(b, it, line);
    }
}

/* Hide or show lines [from, to) */
static void set_lines_hidden(EditorTab* tab, gint from, gint to, gboolean hide) {
    if (to <= from) return;
    GtkTextIter s, e;
    line_start_iter(tab->buffer, from, &s);
    line_start_iter(tab->buffer, to, &e);
    if (hide) gtk_text_buffer_apply_tag(tab->buffer, fold_tag(tab->buffer), &s, &e);
    else gtk_text_buffer_remove_tag(tab->buffer, fold_tag(tab->buffer), &s, &e);
}

static gboolean line_hidden(EditorTab* tab, gint line) {
    if (line < 0 || line >= gtk_text_buffer_get_line_count(tab->buffer)) return FALSE;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(tab->buffer, &it, line);
    return gtk_text_iter_has_tag(&it, fold_tag(tab->buffer));
}

static void show_all(EditorTab* tab) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(tab->buffer, &s, &e);
    gtk_text_buffer_remove_tag(tab->buffer, fold_tag(tab->buffer), &s, &e);
}

/* Folding hid the cursor: move it to the visible line above */
static void fix_cursor(EditorTab* tab) {
    OrgPos pos = cursor_pos(tab);
    if (!line_hidden(tab, pos.line)) return;
    gint line = pos.line;
    while (line > 0 && line_hidden(tab, line)) line--;
    EditorModeState* st = state_of(tab);
    st->org_revealing = TRUE;
    place_cursor(tab, line, 0);
    st->org_revealing = FALSE;
}

/* Show the entry under the cursor and the headings above it (org-reveal) */
static void reveal_cursor(EditorTab* tab) {
    OrgPos pos = cursor_pos(tab);
    if (!line_hidden(tab, pos.line)) return;
    GPtrArray* lines = buffer_lines(tab);
    gint h = org_heading_at(lines, pos.line);
    if (h < 0) {
        set_lines_hidden(tab, pos.line, pos.line + 1, FALSE);
    } else {
        set_lines_hidden(tab, h, MAX(org_entry_end(lines, h), pos.line + 1), FALSE);
        for (gint p = org_heading_parent(lines, h); p >= 0; p = org_heading_parent(lines, p)) {
            set_lines_hidden(tab, p, p + 1, FALSE);
        }
    }
    g_ptr_array_unref(lines);
}

static void on_org_cursor_moved(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    EditorTab* tab = data;
    EditorModeState* st = state_of(tab);
    if (st == NULL || st->org_revealing || !orgmode_editor_is_org(tab)) return;
    GtkTextTag* t = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(tab->buffer), FOLD_TAG);
    if (t == NULL) return;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, gtk_text_buffer_get_insert(tab->buffer));
    if (!gtk_text_iter_has_tag(&it, t)) return;
    st->org_revealing = TRUE;
    reveal_cursor(tab);
    st->org_revealing = FALSE;
}

static void set_global_visibility(EditorTab* tab, GPtrArray* lines, gint state);

void orgmode_editor_init(EditorTab* tab) {
    g_signal_connect(tab->buffer, "notify::cursor-position", G_CALLBACK(on_org_cursor_moved), tab);
    orgmode_editor_reset_visibility(tab);
}

void orgmode_editor_reset_visibility(EditorTab* tab) {
    if (!orgmode_editor_is_org(tab)) {
        show_all(tab); /* text-mode: nothing stays hidden */
        return;
    }
    /* Like Emacs: the file opens folded unless #+STARTUP says otherwise */
    GPtrArray* lines = buffer_lines(tab);
    set_global_visibility(tab, lines, (gint)org_startup_visibility(lines));
    fix_cursor(tab);
    g_ptr_array_unref(lines);
}

/* TAB on a heading (FOLDED -> CHILDREN -> SUBTREE), a drawer or a block */
static gboolean cycle_at(EditorTab* tab, GPtrArray* lines, gint line) {
    gint last = line;
    OrgFoldKind kind = org_fold_kind(lines, line, &last);
    if (kind == ORG_FOLD_NONE) return FALSE;
    if (last <= line) {
        org_echo(tab, "EMPTY ENTRY");
        return TRUE;
    }
    gint end = last + 1;
    if (kind != ORG_FOLD_HEADING) {
        gboolean folded = line_hidden(tab, line + 1);
        set_lines_hidden(tab, line + 1, end, !folded);
        org_echo(tab, folded ? "Shown" : "Folded");
    } else if (line_hidden(tab, line + 1)) {
        set_lines_hidden(tab, line + 1, end, FALSE);
        gboolean children = FALSE;
        for (gint j = line + 1; j < end; j++) {
            gint dl;
            if (j < org_entry_end(lines, line) && org_fold_kind(lines, j, &dl) == ORG_FOLD_DRAWER) {
                set_lines_hidden(tab, j + 1, dl + 1, TRUE);
            }
            if (org_heading_level(org_line(lines, j)) == 0 || org_heading_parent(lines, j) != line) continue;
            children = TRUE;
            set_lines_hidden(tab, j + 1, org_subtree_end(lines, j), TRUE);
        }
        org_echo(tab, children ? "CHILDREN" : "SUBTREE (NO CHILDREN)");
    } else {
        gboolean any_hidden = FALSE;
        for (gint j = line + 2; j < end && !any_hidden; j++) any_hidden = line_hidden(tab, j);
        set_lines_hidden(tab, line + 1, end, !any_hidden);
        org_echo(tab, any_hidden ? "SUBTREE" : "FOLDED");
    }
    fix_cursor(tab);
    return TRUE;
}

/* Global visibility: 0 SHOW ALL, 1 OVERVIEW, 2 CONTENTS (OrgStartupVisibility) */
static void set_global_visibility(EditorTab* tab, GPtrArray* lines, gint state) {
    g_object_set_data(G_OBJECT(tab->buffer), GLOBAL_STATE_KEY, GINT_TO_POINTER(state));
    show_all(tab);
    for (gint i = 0; state != 0 && i < (gint)lines->len; i++) {
        gint level = org_heading_level(org_line(lines, i));
        if (level == 0) continue;
        if (state == 1 && level == 1) set_lines_hidden(tab, i + 1, org_subtree_end(lines, i), TRUE);
        if (state == 2) set_lines_hidden(tab, i + 1, org_entry_end(lines, i), TRUE);
    }
}

/* Shift+TAB: OVERVIEW -> CONTENTS -> SHOW ALL */
static void global_cycle(EditorTab* tab) {
    gint state = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tab->buffer), GLOBAL_STATE_KEY));
    state = (state + 1) % 3;
    GPtrArray* lines = buffer_lines(tab);
    set_global_visibility(tab, lines, state);
    static const gchar* NAMES[] = {"SHOW ALL", "OVERVIEW", "CONTENTS"};
    org_echo(tab, NAMES[state]);
    fix_cursor(tab);
    g_ptr_array_unref(lines);
}

/* ------------------------------------------------------------------------- */
/* Running a command on the buffer lines                                     */
/* ------------------------------------------------------------------------- */

typedef gboolean (*TableFn)(GPtrArray* lines, OrgPos* pos);
typedef gboolean (*TableDirFn)(GPtrArray* lines, OrgPos* pos, gint dir, gchar** msg);

typedef struct {
    OrgTodoConfig* cfg;
    GDateTime* now;
    gint h;             /* heading at or above the cursor, -1 if none */
    gint arg;           /* direction, flags */
    gchar ch;           /* priority letter, flag */
    const gchar* text;  /* tags, keyword, inserted text */
    const gchar* text2;
    OrgSwap swap;       /* blocks a move swapped */
    gboolean swapped;
    TableFn table_fn;
    TableDirFn table_dir_fn;
} OrgArgs;

typedef gboolean (*OrgCmd)(GPtrArray* lines, OrgPos* pos, OrgArgs* a, gchar** msg);

/* Replace the bytes that differ between a and c, a starting at char offset base */
static void replace_diff(GtkTextBuffer* b, gint base, const gchar* a, const gchar* c) {
    gsize la = strlen(a), lc = strlen(c);
    gsize p = 0;
    while (p < la && p < lc && a[p] == c[p]) p++;
    while (p > 0 && (((guchar)a[p] & 0xC0) == 0x80 || ((guchar)c[p] & 0xC0) == 0x80)) p--;
    gsize s = 0;
    while (s < la - p && s < lc - p && a[la - 1 - s] == c[lc - 1 - s]) s++;
    while (s > 0 && ((s < la && ((guchar)a[la - s] & 0xC0) == 0x80) ||
                     (s < lc && ((guchar)c[lc - s] & 0xC0) == 0x80))) {
        s--;
    }
    GtkTextIter is, ie;
    gtk_text_buffer_get_iter_at_offset(b, &is, base + (gint)g_utf8_pointer_to_offset(a, a + p));
    gtk_text_buffer_get_iter_at_offset(b, &ie, base + (gint)g_utf8_pointer_to_offset(a, a + la - s));
    if (!gtk_text_iter_equal(&is, &ie)) gtk_text_buffer_delete(b, &is, &ie);
    if (lc - s > p) gtk_text_buffer_insert(b, &is, c + p, (gint)(lc - s - p));
}

/* Write the new lines over the old ones, touching only what changed, so
 * that folds and marks elsewhere stay */
static void apply_lines(EditorTab* tab, GPtrArray* old, GPtrArray* nw) {
    GtkTextBuffer* b = tab->buffer;
    if (old->len == nw->len) {
        for (gint i = (gint)old->len - 1; i >= 0; i--) {
            const gchar* a = org_line(old, i);
            const gchar* c = org_line(nw, i);
            if (strcmp(a, c) == 0) continue;
            GtkTextIter it;
            gtk_text_buffer_get_iter_at_line(b, &it, i);
            replace_diff(b, gtk_text_iter_get_offset(&it), a, c);
        }
        return;
    }
    gchar* ta = org_lines_join(old);
    gchar* tc = org_lines_join(nw);
    replace_diff(b, 0, ta, tc);
    g_free(ta);
    g_free(tc);
}

static GPtrArray* lines_copy(GPtrArray* lines) {
    GPtrArray* c = g_ptr_array_new_full(lines->len, g_free);
    for (guint i = 0; i < lines->len; i++) g_ptr_array_add(c, g_strdup(g_ptr_array_index(lines, i)));
    return c;
}

/* Run cmd on the buffer. remap: the command may swap blocks of lines (moves):
 * their folds travel with them. */
static gboolean org_run(EditorTab* tab, OrgCmd cmd, OrgArgs* args, gboolean remap) {
    GPtrArray* lines = buffer_lines(tab);
    GPtrArray* orig = lines_copy(lines);
    OrgPos pos = cursor_pos(tab);
    OrgArgs local = {0};
    if (args != NULL) local = *args;
    local.cfg = org_todo_config_new(lines);
    local.now = g_date_time_new_now_local();
    local.h = org_heading_at(lines, pos.line);

    gboolean* hidden = NULL;
    if (remap) {
        hidden = g_new0(gboolean, lines->len);
        for (guint i = 0; i < lines->len; i++) hidden[i] = line_hidden(tab, (gint)i);
    }

    gchar* msg = NULL;
    gboolean ok = cmd(lines, &pos, &local, &msg);
    if (ok) {
        EditorModeState* st = state_of(tab);
        st->org_revealing = TRUE;
        gtk_text_buffer_begin_user_action(tab->buffer);
        apply_lines(tab, orig, lines);
        gtk_text_buffer_end_user_action(tab->buffer);
        if (remap && local.swapped) {
            OrgSwap* sw = &local.swap;
            gint first = sw->end - sw->mid;
            for (gint j = sw->start; j < sw->end; j++) {
                gint src = j < sw->start + first ? sw->mid + (j - sw->start)
                                                 : sw->start + (j - sw->start - first);
                set_lines_hidden(tab, j, j + 1, hidden[src]);
            }
        }
        place_cursor(tab, pos.line, pos.col);
        st->org_revealing = FALSE;
        reveal_cursor(tab);
        editor_tab_label_update(tab);
    }
    if (msg != NULL) org_echo(tab, msg);
    g_free(msg);
    g_free(hidden);
    org_todo_config_free(local.cfg);
    g_date_time_unref(local.now);
    g_ptr_array_unref(orig);
    g_ptr_array_unref(lines);
    return ok;
}

/* ---- Commands (OrgCmd) ---- */

static gboolean need_heading(OrgArgs* a, gchar** msg) {
    if (a->h >= 0) return TRUE;
    *msg = g_strdup("Before first heading");
    return FALSE;
}

static gboolean cmd_promote(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return org_promote(l, p, a->arg, a->ch != 0, msg);
}
static gboolean cmd_move_subtree(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return a->swapped = org_move_subtree(l, p, a->arg, &a->swap, msg);
}
static gboolean cmd_insert_heading(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)msg;
    return org_insert_heading(l, p, a->cfg, (a->arg & 1) != 0, (a->arg & 2) != 0);
}
static gboolean cmd_toggle_heading(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)a;
    (void)msg;
    return org_toggle_heading(l, p);
}
static gboolean cmd_todo(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return need_heading(a, msg) && org_todo_cycle(l, p, a->h, a->cfg, a->arg, a->now, msg);
}
static gboolean cmd_priority(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return need_heading(a, msg) && org_priority(l, p, a->h, a->cfg, a->arg, a->ch);
}
static gboolean cmd_tags(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return need_heading(a, msg) && org_set_tags(l, p, a->h, a->cfg, a->text);
}
static gboolean cmd_planning(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return need_heading(a, msg) && org_set_planning(l, p, a->h, a->text, a->text2);
}
static gboolean cmd_clock_in(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return need_heading(a, msg) && org_clock_in(l, p, a->h, a->now, msg);
}
static gboolean cmd_clock_out(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return org_clock_out(l, p, a->now, msg);
}
static gboolean cmd_stats(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)p;
    gboolean changed = org_update_statistics(l, a->cfg);
    *msg = g_strdup(changed ? "Statistics cookies updated" : "Statistics cookies are up to date");
    return changed;
}
static gboolean cmd_insert_item(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)msg;
    return org_insert_item(l, p, a->arg != 0);
}
static gboolean cmd_indent_item(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return org_indent_item(l, p, a->arg, msg);
}
static gboolean cmd_move_item(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return a->swapped = org_move_item(l, p, a->arg, &a->swap, msg);
}
static gboolean cmd_bullet(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)msg;
    return org_cycle_bullet(l, p, a->arg);
}
static gboolean cmd_ctrl_c_minus(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)a;
    (void)msg;
    if (org_table_is_line(org_line(l, p->line))) return org_table_insert_hline(l, p);
    return org_ctrl_c_minus(l, p);
}
static gboolean cmd_checkbox(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    if (!org_toggle_checkbox(l, p, msg)) return FALSE;
    org_update_statistics(l, a->cfg);
    return TRUE;
}
static gboolean cmd_template(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)a;
    (void)msg;
    return org_expand_template(l, p);
}

static gboolean cmd_table(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)msg;
    return a->table_fn(l, p);
}
static gboolean cmd_table_dir(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    return a->table_dir_fn(l, p, a->arg, msg);
}

static gboolean run_table(EditorTab* tab, TableFn fn) {
    OrgArgs a = {0};
    a.table_fn = fn;
    return org_run(tab, cmd_table, &a, FALSE);
}

static gboolean run_table_dir(EditorTab* tab, TableDirFn fn, gint dir) {
    OrgArgs a = {0};
    a.table_dir_fn = fn;
    a.arg = dir;
    return org_run(tab, cmd_table_dir, &a, FALSE);
}

/* Shift the timestamp under the cursor: ch 'd' moves the day, else the
 * field under the cursor */
static gboolean cmd_timestamp_shift(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)msg;
    const gchar* line = org_line(l, p->line);
    gint b = org_col_to_byte(line, p->col);
    gint s, e;
    OrgTimestamp ts;
    if (!org_timestamp_find(line, b, &s, &e, &ts)) return FALSE;
    OrgTsField f = a->ch == 'd' ? ORG_TS_FIELD_DAY : org_timestamp_field_at(&ts, b - s);
    org_timestamp_add(&ts, f, f == ORG_TS_FIELD_MINUTE ? 5 * a->arg : a->arg);
    gchar* t = org_timestamp_format(&ts);
    gchar* nl = g_strdup_printf("%.*s%s%s", s, line, t, line + e);
    g_free(t);
    g_free(g_ptr_array_index(l, p->line));
    g_ptr_array_index(l, p->line) = nl;
    return TRUE;
}

/* Insert text at the cursor; ch 't': replace the timestamp under the cursor */
static gboolean cmd_insert_text(GPtrArray* l, OrgPos* p, OrgArgs* a, gchar** msg) {
    (void)msg;
    const gchar* line = org_line(l, p->line);
    gint b = org_col_to_byte(line, p->col);
    gint s = b, e = b;
    if (a->ch == 't') org_timestamp_find(line, b, &s, &e, NULL);
    gchar* nl = g_strdup_printf("%.*s%s%s", s, line, a->text, line + e);
    p->col = org_byte_to_col(nl, s + (gint)strlen(a->text));
    g_free(g_ptr_array_index(l, p->line));
    g_ptr_array_index(l, p->line) = nl;
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Prompts (Emacs minibuffer)                                                */
/* ------------------------------------------------------------------------- */

static gboolean can_prompt(EditorTab* tab) {
    if (emacs_like(tab)) return TRUE;
    org_echo(tab, "This Org command asks a question: use the Emacs or Org-mode editor mode");
    return FALSE;
}

/* data: "SCHEDULED", "DEADLINE", "<" (active timestamp) or "[" (inactive) */
static void on_date_answer(EditorTab* tab, const gchar* text, gchar* data) {
    /* A leading '@' asks for the time of day too (C-u C-c . / C-u C-c !) */
    gboolean with_time = data[0] == '@';
    if (with_time) data++;
    gboolean planning = data[0] != '<' && data[0] != '[';
    OrgArgs a = {0};
    if (planning && (g_ascii_strcasecmp(text, "none") == 0 || g_ascii_strcasecmp(text, "remove") == 0)) {
        a.text = data;
        a.text2 = NULL;
        if (org_run(tab, cmd_planning, &a, FALSE)) {
            gchar* m = g_strdup_printf("%s removed", data);
            org_echo(tab, m);
            g_free(m);
        }
        return;
    }
    OrgTimestamp ts = {0};
    ts.active = data[0] != '[';
    GDateTime* now = g_date_time_new_now_local();
    gboolean ok = org_timestamp_read(text, now, &ts);
    if (ok && with_time && !ts.has_time) {
        ts.has_time = TRUE;
        ts.hour = g_date_time_get_hour(now);
        ts.minute = g_date_time_get_minute(now);
    }
    g_date_time_unref(now);
    if (!ok) {
        gchar* m = g_strdup_printf("Cannot read the date \"%s\"", text);
        org_echo(tab, m);
        g_free(m);
        return;
    }
    gchar* t = org_timestamp_format(&ts);
    if (planning) {
        a.text = data;
        a.text2 = t;
        if (org_run(tab, cmd_planning, &a, FALSE)) {
            gchar* m = g_strdup_printf("%s %s", data, t);
            org_echo(tab, m);
            g_free(m);
        }
    } else {
        a.text = t;
        a.ch = 't';
        org_run(tab, cmd_insert_text, &a, FALSE);
    }
    g_free(t);
}

static void prompt_date(EditorTab* tab, const gchar* what) {
    if (!can_prompt(tab)) return;
    gchar* prompt;
    gboolean with_time = FALSE;
    if (what[0] == '<' || what[0] == '[') {
        with_time = state_of(tab)->emacs_universal_arg;
        prompt = g_strdup(with_time ? "Date and time (now, +3, +2w, mon, 2026-11-02 10:00): "
                                    : "Date (today, +3, +2w, mon, 2026-11-02 10:00): ");
    } else {
        gchar* low = g_ascii_strdown(what, -1);
        low[0] = g_ascii_toupper(low[0]);
        prompt = g_strdup_printf("%s (today, +3, +2w, mon, 2026-11-02 10:00, none): ", low);
        g_free(low);
    }
    emacs_prompt(tab, prompt, "", on_date_answer, g_strconcat(with_time ? "@" : "", what, NULL));
    g_free(prompt);
}

static void on_tags_answer(EditorTab* tab, const gchar* text, gchar* data) {
    (void)data;
    OrgArgs a = {0};
    a.text = text;
    org_run(tab, cmd_tags, &a, FALSE);
}

static void prompt_tags(EditorTab* tab) {
    if (!can_prompt(tab)) return;
    GPtrArray* lines = buffer_lines(tab);
    gint h = org_heading_at(lines, cursor_pos(tab).line);
    if (h < 0) {
        org_echo(tab, "Before first heading");
        g_ptr_array_unref(lines);
        return;
    }
    OrgTodoConfig* cfg = org_todo_config_new(lines);
    gchar* tags = org_get_tags(lines, h, cfg);
    gchar* initial = tags != NULL ? g_strndup(tags + 1, strlen(tags) - 2) : g_strdup("");
    emacs_prompt(tab, "Tags (a:b or a b): ", initial, on_tags_answer, NULL);
    emacs_prompt_set_completions(tab, org_collect_tags(lines, cfg), TRUE);
    g_free(initial);
    g_free(tags);
    org_todo_config_free(cfg);
    g_ptr_array_unref(lines);
}

static void on_link_desc_answer(EditorTab* tab, const gchar* text, gchar* target) {
    gchar* link = org_link_format(target, text);
    OrgArgs a = {0};
    a.text = link;
    org_run(tab, cmd_insert_text, &a, FALSE);
    g_free(link);
}

static void on_link_answer(EditorTab* tab, const gchar* text, gchar* data) {
    (void)data;
    gchar* target = g_strstrip(g_strdup(text));
    if (target[0] == '\0') {
        g_free(target);
        org_echo(tab, "No link inserted");
        return;
    }
    emacs_prompt(tab, "Description: ", "", on_link_desc_answer, target);
}

static void prompt_link(EditorTab* tab) {
    if (!can_prompt(tab)) return;
    emacs_prompt(tab, "Link (URL, file:path, *Heading): ", "", on_link_answer, NULL);
    /* Tab completes internal links to the headings of the file */
    GPtrArray* lines = buffer_lines(tab);
    OrgTodoConfig* cfg = org_todo_config_new(lines);
    GPtrArray* targets = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < lines->len; i++) {
        OrgHeading h;
        if (!org_heading_parse(org_line(lines, (gint)i), cfg, &h)) continue;
        if (h.title != NULL && h.title[0] != '\0') g_ptr_array_add(targets, g_strdup_printf("*%s", h.title));
        org_heading_clear(&h);
    }
    emacs_prompt_set_completions(tab, targets, FALSE);
    org_todo_config_free(cfg);
    g_ptr_array_unref(lines);
}

/* ------------------------------------------------------------------------- */
/* Links, navigation, sparse trees                                           */
/* ------------------------------------------------------------------------- */

static void goto_line(EditorTab* tab, gint line) {
    place_cursor(tab, line, 0); /* the cursor handler reveals it */
}

static void open_link(EditorTab* tab) {
    OrgPos pos = cursor_pos(tab);
    gchar* l = line_text(tab, pos.line);
    gchar* target = org_link_at(l, org_col_to_byte(l, pos.col));
    g_free(l);
    if (target == NULL) {
        org_echo(tab, "No link under the cursor");
        return;
    }
    AppData* app = tab->app;
    if (strstr(target, "://") != NULL || g_str_has_prefix(target, "mailto:")) {
        GtkUriLauncher* launcher = gtk_uri_launcher_new(target);
        gtk_uri_launcher_launch(launcher, GTK_WINDOW(app->window), NULL, NULL, NULL);
        g_object_unref(launcher);
        org_echo(tab, target);
        g_free(target);
        return;
    }
    gboolean is_file = g_str_has_prefix(target, "file:") || target[0] == '/' ||
                       g_str_has_prefix(target, "./") || g_str_has_prefix(target, "../") ||
                       g_str_has_prefix(target, "~/");
    if (!is_file) {
        GPtrArray* lines = buffer_lines(tab);
        OrgTodoConfig* cfg = org_todo_config_new(lines);
        gint line = org_link_find_internal(lines, cfg, target);
        org_todo_config_free(cfg);
        g_ptr_array_unref(lines);
        if (line >= 0) {
            goto_line(tab, line);
            g_free(target);
            return;
        }
    }
    gchar* path = g_strdup(g_str_has_prefix(target, "file:") ? target + 5 : target);
    gchar* search = strstr(path, "::");
    if (search != NULL) *search = '\0';
    if (g_str_has_prefix(path, "~/")) {
        gchar* p = g_build_filename(g_get_home_dir(), path + 2, NULL);
        g_free(path);
        path = p;
    } else if (!g_path_is_absolute(path) && tab->path != NULL) {
        gchar* dir = g_path_get_dirname(tab->path);
        gchar* p = g_build_filename(dir, path, NULL);
        g_free(dir);
        g_free(path);
        path = p;
    }
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        gchar* ctype = g_content_type_guess(path, NULL, 0, NULL);
        gboolean text = g_content_type_is_a(ctype, "text/plain") || is_org_file(path);
        g_free(ctype);
        if (text) {
            gchar* base = g_path_get_basename(path);
            editor_open_file(app, base, path);
            g_free(base);
        } else {
            gchar* uri = g_filename_to_uri(path, NULL, NULL);
            GtkUriLauncher* launcher = gtk_uri_launcher_new(uri);
            gtk_uri_launcher_launch(launcher, GTK_WINDOW(app->window), NULL, NULL, NULL);
            g_object_unref(launcher);
            g_free(uri);
        }
    } else if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        gchar* uri = g_filename_to_uri(path, NULL, NULL);
        GtkUriLauncher* launcher = gtk_uri_launcher_new(uri);
        gtk_uri_launcher_launch(launcher, GTK_WINDOW(app->window), NULL, NULL, NULL);
        g_object_unref(launcher);
        g_free(uri);
    } else {
        gchar* m = g_strdup_printf("No match for link \"%s\"", target);
        org_echo(tab, m);
        g_free(m);
    }
    g_free(path);
    g_free(target);
}

typedef enum { NAV_NEXT, NAV_PREV, NAV_FORWARD, NAV_BACKWARD, NAV_UP } OrgNav;

static void navigate(EditorTab* tab, OrgNav nav) {
    GPtrArray* lines = buffer_lines(tab);
    gint line = cursor_pos(tab).line;
    gint target = -1;
    switch (nav) {
        case NAV_NEXT:
        case NAV_PREV: {
            gint dir = nav == NAV_NEXT ? 1 : -1;
            target = org_heading_next(lines, line, dir, FALSE);
            while (target >= 0 && line_hidden(tab, target)) {
                target = org_heading_next(lines, target, dir, FALSE);
            }
            break;
        }
        case NAV_FORWARD: target = org_heading_next(lines, line, 1, TRUE); break;
        case NAV_BACKWARD: target = org_heading_next(lines, line, -1, TRUE); break;
        case NAV_UP: {
            gint h = org_heading_at(lines, line);
            target = h >= 0 ? org_heading_parent(lines, h) : -1;
            break;
        }
    }
    g_ptr_array_unref(lines);
    if (target < 0) {
        org_echo(tab, nav == NAV_UP ? "Already at top level of the outline" : "No more headings");
        return;
    }
    goto_line(tab, target);
}

/* Show only the headings that match (TODO entries, or a regexp) and the
 * headings above them */
static void sparse_tree(EditorTab* tab, GRegex* re) {
    GPtrArray* lines = buffer_lines(tab);
    OrgTodoConfig* cfg = org_todo_config_new(lines);
    gint n = (gint)lines->len;
    gboolean* keep = g_new0(gboolean, n);
    gint count = 0;
    gboolean before_first = TRUE;
    for (gint i = 0; i < n; i++) {
        const gchar* l = org_line(lines, i);
        if (org_heading_level(l) > 0) before_first = FALSE;
        if (before_first || (i == n - 1 && l[0] == '\0')) {
            keep[i] = TRUE;
            continue;
        }
        if (org_heading_level(l) == 0) continue;
        gboolean match = FALSE;
        if (re == NULL) {
            OrgHeading h;
            org_heading_parse(l, cfg, &h);
            match = h.keyword != NULL && !org_todo_is_done(cfg, h.keyword);
            org_heading_clear(&h);
        } else {
            gint ee = org_entry_end(lines, i);
            for (gint j = i; j < ee; j++) {
                if (g_regex_match(re, org_line(lines, j), 0, NULL)) {
                    match = TRUE;
                    keep[j] = TRUE;
                }
            }
        }
        if (!match) continue;
        count++;
        keep[i] = TRUE;
        for (gint p = org_heading_parent(lines, i); p >= 0 && !keep[p]; p = org_heading_parent(lines, p)) {
            keep[p] = TRUE;
        }
    }
    show_all(tab);
    for (gint i = 0; i < n;) {
        gint j = i;
        while (j < n && keep[j] == keep[i]) j++;
        if (!keep[i]) set_lines_hidden(tab, i, j, TRUE);
        i = j;
    }
    fix_cursor(tab);
    gchar* m = re == NULL ? g_strdup_printf("%d TODO entries found", count)
                          : g_strdup_printf("%d match%s found", count, count == 1 ? "" : "es");
    org_echo(tab, m);
    g_free(m);
    g_free(keep);
    org_todo_config_free(cfg);
    g_ptr_array_unref(lines);
}

static void on_regexp_answer(EditorTab* tab, const gchar* text, gchar* data) {
    (void)data;
    if (text[0] == '\0') return;
    GError* err = NULL;
    GRegex* re = g_regex_new(text, G_REGEX_CASELESS, 0, &err);
    if (re == NULL) {
        org_echo(tab, err->message);
        g_error_free(err);
        return;
    }
    sparse_tree(tab, re);
    g_regex_unref(re);
}

/* ------------------------------------------------------------------------- */
/* Actions (keys and M-x names)                                              */
/* ------------------------------------------------------------------------- */

typedef enum {
    ACT_CYCLE, ACT_GLOBAL_CYCLE, ACT_SHOW_ALL,
    ACT_TODO, ACT_TODO_PREV, ACT_PRIORITY_UP, ACT_PRIORITY_DOWN, ACT_TAGS,
    ACT_SCHEDULE, ACT_DEADLINE, ACT_TIMESTAMP, ACT_TIMESTAMP_INACTIVE,
    ACT_INSERT_HEADING, ACT_INSERT_HEADING_AFTER, ACT_INSERT_TODO_HEADING,
    ACT_INSERT_TODO_HEADING_AFTER, ACT_TOGGLE_HEADING,
    ACT_PROMOTE, ACT_DEMOTE, ACT_PROMOTE_SUBTREE, ACT_DEMOTE_SUBTREE,
    ACT_MOVE_UP, ACT_MOVE_DOWN,
    ACT_TOGGLE_CHECKBOX, ACT_CTRL_C_CTRL_C, ACT_CTRL_C_MINUS, ACT_STATISTICS,
    ACT_CLOCK_IN, ACT_CLOCK_OUT,
    ACT_INSERT_LINK, ACT_OPEN_LINK,
    ACT_NEXT_HEADING, ACT_PREV_HEADING, ACT_FORWARD_SAME, ACT_BACKWARD_SAME, ACT_UP_HEADING,
    ACT_TODO_TREE, ACT_REGEXP_TREE, ACT_TABLE_ALIGN
} OrgAction;

static const struct {
    const gchar* name;
    OrgAction action;
} COMMANDS[] = {
    {"org-cycle", ACT_CYCLE},
    {"org-global-cycle", ACT_GLOBAL_CYCLE},
    {"org-shifttab", ACT_GLOBAL_CYCLE},
    {"org-show-all", ACT_SHOW_ALL},
    {"org-fold-show-all", ACT_SHOW_ALL},
    {"org-todo", ACT_TODO},
    {"org-priority-up", ACT_PRIORITY_UP},
    {"org-priority-down", ACT_PRIORITY_DOWN},
    {"org-set-tags-command", ACT_TAGS},
    {"org-schedule", ACT_SCHEDULE},
    {"org-deadline", ACT_DEADLINE},
    {"org-time-stamp", ACT_TIMESTAMP},
    {"org-timestamp", ACT_TIMESTAMP},
    {"org-time-stamp-inactive", ACT_TIMESTAMP_INACTIVE},
    {"org-timestamp-inactive", ACT_TIMESTAMP_INACTIVE},
    {"org-insert-heading", ACT_INSERT_HEADING},
    {"org-insert-heading-respect-content", ACT_INSERT_HEADING_AFTER},
    {"org-insert-todo-heading", ACT_INSERT_TODO_HEADING},
    {"org-insert-todo-heading-respect-content", ACT_INSERT_TODO_HEADING_AFTER},
    {"org-toggle-heading", ACT_TOGGLE_HEADING},
    {"org-promote-subtree", ACT_PROMOTE_SUBTREE},
    {"org-demote-subtree", ACT_DEMOTE_SUBTREE},
    {"org-do-promote", ACT_PROMOTE},
    {"org-do-demote", ACT_DEMOTE},
    {"org-move-subtree-up", ACT_MOVE_UP},
    {"org-move-subtree-down", ACT_MOVE_DOWN},
    {"org-toggle-checkbox", ACT_TOGGLE_CHECKBOX},
    {"org-ctrl-c-ctrl-c", ACT_CTRL_C_CTRL_C},
    {"org-ctrl-c-minus", ACT_CTRL_C_MINUS},
    {"org-update-statistics-cookies", ACT_STATISTICS},
    {"org-clock-in", ACT_CLOCK_IN},
    {"org-clock-out", ACT_CLOCK_OUT},
    {"org-insert-link", ACT_INSERT_LINK},
    {"org-open-at-point", ACT_OPEN_LINK},
    {"org-next-visible-heading", ACT_NEXT_HEADING},
    {"org-previous-visible-heading", ACT_PREV_HEADING},
    {"org-forward-heading-same-level", ACT_FORWARD_SAME},
    {"org-backward-heading-same-level", ACT_BACKWARD_SAME},
    {"outline-up-heading", ACT_UP_HEADING},
    {"org-up-heading", ACT_UP_HEADING},
    {"org-show-todo-tree", ACT_TODO_TREE},
    {"org-sparse-tree", ACT_REGEXP_TREE},
    {"org-occur", ACT_REGEXP_TREE},
    {"org-table-align", ACT_TABLE_ALIGN},
};

/* Context of the cursor line */
typedef struct {
    gboolean heading, table, item;
} LineCtx;

static LineCtx line_ctx(EditorTab* tab) {
    LineCtx c = {0};
    gchar* l = line_text(tab, cursor_pos(tab).line);
    OrgItem it;
    c.heading = org_heading_level(l) > 0;
    c.table = org_table_is_line(l);
    c.item = !c.heading && org_item_parse(l, &it);
    g_free(l);
    return c;
}

static gboolean run_simple(EditorTab* tab, OrgCmd cmd, gint arg, gchar ch, gboolean remap) {
    OrgArgs a = {0};
    a.arg = arg;
    a.ch = ch;
    return org_run(tab, cmd, &a, remap);
}

/* C-c C-c: what makes sense on the cursor line */
static void ctrl_c_ctrl_c(EditorTab* tab) {
    LineCtx c = line_ctx(tab);
    if (c.table) {
        run_table(tab, org_table_align);
        return;
    }
    GPtrArray* lines = buffer_lines(tab);
    gint line = cursor_pos(tab).line;
    gint item = org_item_at(lines, line);
    OrgItem it;
    gboolean checkbox = item >= 0 && org_item_parse(org_line(lines, item), &it) && it.checkbox != 0;
    const gchar* l = org_line(lines, line);
    gboolean cookie = strchr(l, '[') != NULL && (strstr(l, "/]") != NULL || strstr(l, "%]") != NULL);
    gboolean keyword = g_str_has_prefix(l, "#+");
    g_ptr_array_unref(lines);
    if (checkbox) run_simple(tab, cmd_checkbox, 0, 0, FALSE);
    else if (cookie) run_simple(tab, cmd_stats, 0, 0, FALSE);
    else if (c.heading) prompt_tags(tab);
    else if (keyword) org_echo(tab, "Local setup has been refreshed");
    else org_echo(tab, "C-c C-c can do nothing useful here");
}

static gboolean do_action(EditorTab* tab, OrgAction act) {
    switch (act) {
        case ACT_CYCLE: {
            GPtrArray* lines = buffer_lines(tab);
            gboolean ok = cycle_at(tab, lines, cursor_pos(tab).line);
            g_ptr_array_unref(lines);
            if (!ok) org_echo(tab, "Nothing to fold here");
            return TRUE;
        }
        case ACT_GLOBAL_CYCLE: global_cycle(tab); return TRUE;
        case ACT_SHOW_ALL: {
            show_all(tab);
            g_object_set_data(G_OBJECT(tab->buffer), GLOBAL_STATE_KEY, GINT_TO_POINTER(0));
            org_echo(tab, "SHOW ALL");
            return TRUE;
        }
        case ACT_TODO: return run_simple(tab, cmd_todo, 1, 0, FALSE);
        case ACT_TODO_PREV: return run_simple(tab, cmd_todo, -1, 0, FALSE);
        case ACT_PRIORITY_UP: return run_simple(tab, cmd_priority, 1, 0, FALSE);
        case ACT_PRIORITY_DOWN: return run_simple(tab, cmd_priority, -1, 0, FALSE);
        case ACT_TAGS: prompt_tags(tab); return TRUE;
        case ACT_SCHEDULE: prompt_date(tab, "SCHEDULED"); return TRUE;
        case ACT_DEADLINE: prompt_date(tab, "DEADLINE"); return TRUE;
        case ACT_TIMESTAMP: prompt_date(tab, "<"); return TRUE;
        case ACT_TIMESTAMP_INACTIVE: prompt_date(tab, "["); return TRUE;
        case ACT_INSERT_HEADING: return run_simple(tab, cmd_insert_heading, 0, 0, FALSE);
        case ACT_INSERT_HEADING_AFTER: return run_simple(tab, cmd_insert_heading, 1, 0, FALSE);
        case ACT_INSERT_TODO_HEADING: return run_simple(tab, cmd_insert_heading, 2, 0, FALSE);
        case ACT_INSERT_TODO_HEADING_AFTER: return run_simple(tab, cmd_insert_heading, 3, 0, FALSE);
        case ACT_TOGGLE_HEADING: return run_simple(tab, cmd_toggle_heading, 0, 0, FALSE);
        case ACT_PROMOTE: return run_simple(tab, cmd_promote, -1, 0, FALSE);
        case ACT_DEMOTE: return run_simple(tab, cmd_promote, 1, 0, FALSE);
        case ACT_PROMOTE_SUBTREE: return run_simple(tab, cmd_promote, -1, 1, FALSE);
        case ACT_DEMOTE_SUBTREE: return run_simple(tab, cmd_promote, 1, 1, FALSE);
        case ACT_MOVE_UP: return run_simple(tab, cmd_move_subtree, -1, 0, TRUE);
        case ACT_MOVE_DOWN: return run_simple(tab, cmd_move_subtree, 1, 0, TRUE);
        case ACT_TOGGLE_CHECKBOX: return run_simple(tab, cmd_checkbox, 0, 0, FALSE);
        case ACT_CTRL_C_CTRL_C: ctrl_c_ctrl_c(tab); return TRUE;
        case ACT_CTRL_C_MINUS: return run_simple(tab, cmd_ctrl_c_minus, 0, 0, FALSE);
        case ACT_STATISTICS: run_simple(tab, cmd_stats, 0, 0, FALSE); return TRUE;
        case ACT_CLOCK_IN: return run_simple(tab, cmd_clock_in, 0, 0, FALSE);
        case ACT_CLOCK_OUT: return run_simple(tab, cmd_clock_out, 0, 0, FALSE);
        case ACT_INSERT_LINK: prompt_link(tab); return TRUE;
        case ACT_OPEN_LINK: open_link(tab); return TRUE;
        case ACT_NEXT_HEADING: navigate(tab, NAV_NEXT); return TRUE;
        case ACT_PREV_HEADING: navigate(tab, NAV_PREV); return TRUE;
        case ACT_FORWARD_SAME: navigate(tab, NAV_FORWARD); return TRUE;
        case ACT_BACKWARD_SAME: navigate(tab, NAV_BACKWARD); return TRUE;
        case ACT_UP_HEADING: navigate(tab, NAV_UP); return TRUE;
        case ACT_TODO_TREE: sparse_tree(tab, NULL); return TRUE;
        case ACT_REGEXP_TREE:
            if (can_prompt(tab)) emacs_prompt(tab, "Regexp: ", "", on_regexp_answer, NULL);
            return TRUE;
        case ACT_TABLE_ALIGN:
            if (!run_table(tab, org_table_align)) org_echo(tab, "Not in a table");
            return TRUE;
    }
    return FALSE;
}

void orgmode_editor_add_command_names(EditorTab* tab, GPtrArray* names) {
    if (!orgmode_editor_is_org(tab)) return;
    for (guint i = 0; i < G_N_ELEMENTS(COMMANDS); i++) g_ptr_array_add(names, g_strdup(COMMANDS[i].name));
}

gboolean orgmode_editor_command(EditorTab* tab, const gchar* name) {
    if (!orgmode_editor_is_org(tab)) return FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(COMMANDS); i++) {
        if (g_strcmp0(COMMANDS[i].name, name) == 0) return do_action(tab, COMMANDS[i].action);
    }
    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* Keys                                                                      */
/* ------------------------------------------------------------------------- */

static void undefined_key(EditorTab* tab, const gchar* prefix, guint keyval, gboolean ctrl) {
    const gchar* name = gdk_keyval_name(keyval);
    gchar* m = g_strdup_printf("%s %s%s is undefined", prefix, ctrl ? "C-" : "", name != NULL ? name : "?");
    org_echo(tab, m);
    g_free(m);
}

/* Second key of C-c, C-c C-x, C-c /, C-c , */
static gboolean handle_prefix(EditorTab* tab, EditorModeState* st, guint keyval, GdkModifierType state) {
    OrgPrefix prefix = st->org_prefix;
    st->org_prefix = ORG_PREFIX_NONE;
    org_echo(tab, ""); /* commands below may echo their own message */
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    guint k = gdk_keyval_to_lower(keyval);
    if ((ctrl && k == GDK_KEY_g) || keyval == GDK_KEY_Escape) {
        org_echo(tab, "Quit");
        return TRUE;
    }
    if (prefix == ORG_PREFIX_PRIORITY) {
        if (keyval == GDK_KEY_space) {
            run_simple(tab, cmd_priority, 0, ' ', FALSE);
        } else if (k >= GDK_KEY_a && k <= GDK_KEY_z) {
            run_simple(tab, cmd_priority, 0, (gchar)('A' + (k - GDK_KEY_a)), FALSE);
        } else {
            org_echo(tab, "Priority must be a letter (SPC removes it)");
        }
        return TRUE;
    }
    if (prefix == ORG_PREFIX_SPARSE) {
        if (k == GDK_KEY_t) do_action(tab, ACT_TODO_TREE);
        else if (k == GDK_KEY_slash || k == GDK_KEY_r) do_action(tab, ACT_REGEXP_TREE);
        else undefined_key(tab, "C-c /", keyval, ctrl);
        return TRUE;
    }
    if (prefix == ORG_PREFIX_C_C_C_X) {
        if (k == GDK_KEY_i) do_action(tab, ACT_CLOCK_IN);
        else if (k == GDK_KEY_o) do_action(tab, ACT_CLOCK_OUT);
        else undefined_key(tab, "C-c C-x", keyval, ctrl);
        return TRUE;
    }
    /* C-c <key> */
    switch (k) {
        case GDK_KEY_c:
            if (ctrl) do_action(tab, ACT_CTRL_C_CTRL_C);
            else undefined_key(tab, "C-c", keyval, ctrl);
            return TRUE;
        case GDK_KEY_t: do_action(tab, ACT_TODO); return TRUE;
        case GDK_KEY_s: do_action(tab, ACT_SCHEDULE); return TRUE;
        case GDK_KEY_d: do_action(tab, ACT_DEADLINE); return TRUE;
        case GDK_KEY_period: do_action(tab, ACT_TIMESTAMP); return TRUE;
        case GDK_KEY_exclam: do_action(tab, ACT_TIMESTAMP_INACTIVE); return TRUE;
        case GDK_KEY_l: do_action(tab, ACT_INSERT_LINK); return TRUE;
        case GDK_KEY_o: do_action(tab, ACT_OPEN_LINK); return TRUE;
        case GDK_KEY_n: do_action(tab, ACT_NEXT_HEADING); return TRUE;
        case GDK_KEY_p: do_action(tab, ACT_PREV_HEADING); return TRUE;
        case GDK_KEY_f: do_action(tab, ACT_FORWARD_SAME); return TRUE;
        case GDK_KEY_b: do_action(tab, ACT_BACKWARD_SAME); return TRUE;
        case GDK_KEY_u: do_action(tab, ACT_UP_HEADING); return TRUE;
        case GDK_KEY_q: do_action(tab, ACT_TAGS); return TRUE;
        case GDK_KEY_minus: do_action(tab, ACT_CTRL_C_MINUS); return TRUE;
        case GDK_KEY_asterisk: do_action(tab, ACT_TOGGLE_HEADING); return TRUE;
        case GDK_KEY_numbersign: do_action(tab, ACT_STATISTICS); return TRUE;
        case GDK_KEY_comma:
            st->org_prefix = ORG_PREFIX_PRIORITY;
            org_echo(tab, "Priority A-C (SPC removes it): ");
            return TRUE;
        case GDK_KEY_slash:
            st->org_prefix = ORG_PREFIX_SPARSE;
            org_echo(tab, "Sparse tree: [t]odo entries, [/] regexp");
            return TRUE;
        case GDK_KEY_x:
            st->org_prefix = ORG_PREFIX_C_C_C_X;
            org_echo(tab, "C-c C-x-");
            return TRUE;
        default:
            undefined_key(tab, "C-c", keyval, ctrl);
            return TRUE;
    }
}

gboolean orgmode_editor_claims_key(EditorTab* tab, guint keyval, GdkModifierType state) {
    if (!orgmode_editor_is_org(tab) || tab->mode_state == NULL) return FALSE;
    if (state_of(tab)->org_prefix != ORG_PREFIX_NONE) return TRUE;
    if ((state & GDK_ALT_MASK) && !(state & GDK_CONTROL_MASK) &&
        (keyval == GDK_KEY_Left || keyval == GDK_KEY_Right)) {
        LineCtx c = line_ctx(tab);
        if (c.heading || c.table || c.item) return TRUE;
        GPtrArray* lines = buffer_lines(tab);
        gboolean in_item = org_item_at(lines, cursor_pos(tab).line) >= 0;
        g_ptr_array_unref(lines);
        return in_item;
    }
    return FALSE;
}

/* Alt (+Shift) + arrows: structure editing */
static gboolean handle_meta_arrow(EditorTab* tab, guint keyval, gboolean shift) {
    LineCtx c = line_ctx(tab);
    gboolean horizontal = keyval == GDK_KEY_Left || keyval == GDK_KEY_Right;
    gint dir = (keyval == GDK_KEY_Right || keyval == GDK_KEY_Down) ? 1 : -1;
    if (c.table) {
        if (horizontal) {
            if (!shift) return run_table_dir(tab, org_table_move_column, dir), TRUE;
            run_table(tab, dir > 0 ? org_table_insert_column : org_table_delete_column);
            return TRUE;
        }
        if (!shift) return run_table_dir(tab, org_table_move_row, dir), TRUE;
        run_table(tab, dir > 0 ? org_table_insert_row : org_table_delete_row);
        return TRUE;
    }
    if (c.heading) {
        if (horizontal) {
            do_action(tab, dir > 0 ? (shift ? ACT_DEMOTE_SUBTREE : ACT_DEMOTE)
                                   : (shift ? ACT_PROMOTE_SUBTREE : ACT_PROMOTE));
            return TRUE;
        }
        if (shift) return FALSE;
        do_action(tab, dir > 0 ? ACT_MOVE_DOWN : ACT_MOVE_UP);
        return TRUE;
    }
    GPtrArray* lines = buffer_lines(tab);
    gboolean in_item = org_item_at(lines, cursor_pos(tab).line) >= 0;
    g_ptr_array_unref(lines);
    if (in_item) {
        if (horizontal) return run_simple(tab, cmd_indent_item, dir, 0, FALSE), TRUE;
        if (shift) return FALSE;
        run_simple(tab, cmd_move_item, dir, 0, TRUE);
        return TRUE;
    }
    return FALSE;
}

/* Shift + arrows: timestamps, TODO keywords, priorities, bullets */
static gboolean handle_shift_arrow(EditorTab* tab, guint keyval) {
    if (has_selection(tab)) return FALSE;
    gboolean horizontal = keyval == GDK_KEY_Left || keyval == GDK_KEY_Right;
    gint dir = (keyval == GDK_KEY_Right || keyval == GDK_KEY_Up) ? 1 : -1;
    OrgPos pos = cursor_pos(tab);
    gchar* l = line_text(tab, pos.line);
    gboolean on_ts = org_timestamp_find(l, org_col_to_byte(l, pos.col), NULL, NULL, NULL);
    g_free(l);
    if (on_ts) return run_simple(tab, cmd_timestamp_shift, dir, horizontal ? 'd' : 0, FALSE), TRUE;
    LineCtx c = line_ctx(tab);
    if (c.heading) {
        if (horizontal) do_action(tab, dir > 0 ? ACT_TODO : ACT_TODO_PREV);
        else do_action(tab, dir > 0 ? ACT_PRIORITY_UP : ACT_PRIORITY_DOWN);
        return TRUE;
    }
    if (c.item && horizontal) return run_simple(tab, cmd_bullet, dir, 0, FALSE), TRUE;
    return FALSE;
}

/* TAB without modifiers */
static gboolean handle_tab(EditorTab* tab, gboolean can_insert) {
    LineCtx c = line_ctx(tab);
    if (c.table) return run_table(tab, org_table_next_field), TRUE;
    if (can_insert && run_simple(tab, cmd_template, 0, 0, FALSE)) return TRUE;
    GPtrArray* lines = buffer_lines(tab);
    OrgPos pos = cursor_pos(tab);
    gboolean done = cycle_at(tab, lines, pos.line);
    if (!done && c.item && can_insert) {
        /* An empty item: TAB changes its level */
        OrgItem it;
        const gchar* l = org_line(lines, pos.line);
        if (org_item_parse(l, &it) && l[it.content] == '\0' && pos.col == org_utf8_width(l)) {
            done = run_simple(tab, cmd_indent_item, 1, 0, FALSE) ||
                   run_simple(tab, cmd_indent_item, -1, 0, FALSE);
        }
    }
    g_ptr_array_unref(lines);
    return done;
}

gboolean orgmode_editor_handle_key(AppData* app, EditorTab* tab, guint keyval, GdkModifierType state) {
    (void)app;
    if (!orgmode_editor_is_org(tab) || tab->mode_state == NULL) return FALSE;
    EditorModeState* st = state_of(tab);
    if (st->emacs_prefix != EMACS_PREFIX_NONE || is_modifier_key(keyval)) return FALSE;

    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    gboolean can_insert = !(tab->mode == EDITOR_MODE_VIM && !vim_is_inserting(tab));

    if (st->org_prefix != ORG_PREFIX_NONE) return handle_prefix(tab, st, keyval, state);

    if (emacs_like(tab) && ctrl && !alt && !shift && (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
        st->org_prefix = ORG_PREFIX_C_C;
        org_echo(tab, "C-c-");
        return TRUE;
    }

    /* Visibility, tables, templates */
    if (keyval == GDK_KEY_ISO_Left_Tab || (keyval == GDK_KEY_Tab && shift && !ctrl && !alt)) {
        if (line_ctx(tab).table) run_table(tab, org_table_prev_field);
        else global_cycle(tab);
        return TRUE;
    }
    if (keyval == GDK_KEY_Tab && !ctrl && !alt && !shift) return handle_tab(tab, can_insert);

    gboolean ret = keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter;
    if (ret && !ctrl && !alt && !shift && can_insert && line_ctx(tab).table) {
        return run_table(tab, org_table_next_row), TRUE;
    }

    /* M-RET / M-S-RET: new heading or item; C-RET / C-S-RET: after the subtree */
    if (ret && alt && !ctrl) {
        GPtrArray* lines = buffer_lines(tab);
        gboolean in_item = org_item_at(lines, cursor_pos(tab).line) >= 0 && !line_ctx(tab).heading;
        g_ptr_array_unref(lines);
        if (in_item) run_simple(tab, cmd_insert_item, shift ? 1 : 0, 0, FALSE);
        else do_action(tab, shift ? ACT_INSERT_TODO_HEADING : ACT_INSERT_HEADING);
        return TRUE;
    }
    if (ret && ctrl && !alt) {
        do_action(tab, shift ? ACT_INSERT_TODO_HEADING_AFTER : ACT_INSERT_HEADING_AFTER);
        return TRUE;
    }

    gboolean arrow = keyval == GDK_KEY_Left || keyval == GDK_KEY_Right ||
                     keyval == GDK_KEY_Up || keyval == GDK_KEY_Down;
    if (arrow && alt && !ctrl) return handle_meta_arrow(tab, keyval, shift);
    if (arrow && shift && !alt && !ctrl) return handle_shift_arrow(tab, keyval);

    /* Ctrl+Space on a checkbox item: toggle it (elsewhere: set the mark) */
    if (keyval == GDK_KEY_space && ctrl && !alt && !shift) {
        GPtrArray* lines = buffer_lines(tab);
        gint item = org_item_at(lines, cursor_pos(tab).line);
        OrgItem it;
        gboolean box = item >= 0 && org_item_parse(org_line(lines, item), &it) && it.checkbox != 0;
        g_ptr_array_unref(lines);
        if (box) return run_simple(tab, cmd_checkbox, 0, 0, FALSE), TRUE;
    }
    return FALSE;
}
