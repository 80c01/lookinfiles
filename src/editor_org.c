/* Org-mode editing in editor tabs: heading folding, global visibility cycling,
 * TODO cycling, checkbox toggling and heading insertion (Org files only). */
#include "editor_modes_private.h"
#include "orgmode.h"
#include "file_types.h"

gboolean orgmode_editor_toggle_heading_fold(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return FALSE;

    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cur, gtk_text_buffer_get_insert(tab->buffer));
    gint cur_line = gtk_text_iter_get_line(&cur);

    GtkTextIter line_start;
    gtk_text_buffer_get_iter_at_line(tab->buffer, &line_start, cur_line);
    GtkTextIter line_end = line_start;
    gtk_text_iter_forward_to_line_end(&line_end);
    gchar* line_text = gtk_text_buffer_get_text(tab->buffer, &line_start, &line_end, FALSE);
    gint hlevel = orgmode_heading_level(line_text);
    g_free(line_text);

    if (hlevel <= 0) {
        return FALSE; /* Not on a heading */
    }

    /* Ensure tags exist in editor buffer */
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(tab->buffer);
    GtkTextTag* fold_tag = gtk_text_tag_table_lookup(table, "org_folded");
    if (fold_tag == NULL) {
        fold_tag = gtk_text_buffer_create_tag(tab->buffer, "org_folded", "invisible", TRUE, NULL);
    }
    GtkTextTag* fold_hdr_tag = gtk_text_tag_table_lookup(table, "org_heading_folded");
    if (fold_hdr_tag == NULL) {
        fold_hdr_tag = gtk_text_buffer_create_tag(tab->buffer, "org_heading_folded",
                                                  "weight", PANGO_WEIGHT_BOLD,
                                                  "foreground", "#4fc1ff",
                                                  NULL);
    }

    /* Subtree starts at the next line */
    GtkTextIter sub_start = line_start;
    if (!gtk_text_iter_forward_line(&sub_start)) {
        return TRUE; /* Heading at EOF with no body */
    }

    /* Find where this subtree ends (next heading with level <= hlevel, or EOF) */
    GtkTextIter scan = sub_start;
    GtkTextIter sub_end = sub_start;
    gboolean has_content = FALSE;

    while (!gtk_text_iter_is_end(&scan)) {
        GtkTextIter ls = scan;
        GtkTextIter le = ls;
        gtk_text_iter_forward_to_line_end(&le);
        gchar* lt = gtk_text_buffer_get_text(tab->buffer, &ls, &le, FALSE);
        gint cur_lev = orgmode_heading_level(lt);
        g_free(lt);

        if (cur_lev > 0 && cur_lev <= hlevel) {
            sub_end = ls;
            has_content = TRUE;
            break;
        }
        has_content = TRUE;
        if (!gtk_text_iter_forward_line(&scan)) {
            gtk_text_buffer_get_end_iter(tab->buffer, &sub_end);
            break;
        }
        sub_end = scan;
    }

    if (!has_content || gtk_text_iter_equal(&sub_start, &sub_end)) {
        return TRUE;
    }

    /* Check if currently folded */
    gboolean is_folded = gtk_text_iter_has_tag(&sub_start, fold_tag);
    if (is_folded) {
        /* Unfold */
        gtk_text_buffer_remove_tag(tab->buffer, fold_tag, &sub_start, &sub_end);
        gtk_text_buffer_remove_tag(tab->buffer, fold_hdr_tag, &line_start, &line_end);
        emacs_echo(tab, "SUBTREE (unfolded)");
    } else {
        /* Fold */
        gtk_text_buffer_apply_tag(tab->buffer, fold_tag, &sub_start, &sub_end);
        gtk_text_buffer_apply_tag(tab->buffer, fold_hdr_tag, &line_start, &line_end);
        emacs_echo(tab, "FOLDED");
    }

    return TRUE;
}

void orgmode_editor_cycle_global_fold(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return;

    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(tab->buffer);
    GtkTextTag* fold_tag = gtk_text_tag_table_lookup(table, "org_folded");
    if (fold_tag == NULL) {
        fold_tag = gtk_text_buffer_create_tag(tab->buffer, "org_folded", "invisible", TRUE, NULL);
    }
    GtkTextTag* fold_hdr_tag = gtk_text_tag_table_lookup(table, "org_heading_folded");
    if (fold_hdr_tag == NULL) {
        fold_hdr_tag = gtk_text_buffer_create_tag(tab->buffer, "org_heading_folded",
                                                  "weight", PANGO_WEIGHT_BOLD,
                                                  "foreground", "#4fc1ff",
                                                  NULL);
    }

    gint state = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tab->buffer), "org_editor_global_fold"));
    state = (state + 1) % 3;
    g_object_set_data(G_OBJECT(tab->buffer), "org_editor_global_fold", GINT_TO_POINTER(state));

    /* Remove all fold tags from entire buffer first */
    GtkTextIter b_start, b_end;
    gtk_text_buffer_get_bounds(tab->buffer, &b_start, &b_end);
    gtk_text_buffer_remove_tag(tab->buffer, fold_tag, &b_start, &b_end);
    gtk_text_buffer_remove_tag(tab->buffer, fold_hdr_tag, &b_start, &b_end);

    if (state == 0) {
        /* State 0: SHOW ALL */
        emacs_echo(tab, "SHOW ALL (global visibility)");
        return;
    }

    if (state == 1) {
        /* State 1: OVERVIEW (only top-level headings visible) */
        GtkTextIter scan;
        gtk_text_buffer_get_start_iter(tab->buffer, &scan);
        while (!gtk_text_iter_is_end(&scan)) {
            GtkTextIter ls = scan;
            GtkTextIter le = ls;
            gtk_text_iter_forward_to_line_end(&le);
            gchar* text = gtk_text_buffer_get_text(tab->buffer, &ls, &le, FALSE);
            gint lev = orgmode_heading_level(text);
            g_free(text);

            if (lev == 1) {
                GtkTextIter sub_start = ls;
                if (gtk_text_iter_forward_line(&sub_start)) {
                    GtkTextIter sub_scan = sub_start;
                    GtkTextIter sub_end = sub_start;
                    while (!gtk_text_iter_is_end(&sub_scan)) {
                        GtkTextIter hls = sub_scan;
                        GtkTextIter hle = hls;
                        gtk_text_iter_forward_to_line_end(&hle);
                        gchar* ht = gtk_text_buffer_get_text(tab->buffer, &hls, &hle, FALSE);
                        gint hlev = orgmode_heading_level(ht);
                        g_free(ht);
                        if (hlev == 1) {
                            sub_end = hls;
                            break;
                        }
                        if (!gtk_text_iter_forward_line(&sub_scan)) {
                            gtk_text_buffer_get_end_iter(tab->buffer, &sub_end);
                            break;
                        }
                        sub_end = sub_scan;
                    }
                    if (!gtk_text_iter_equal(&sub_start, &sub_end)) {
                        gtk_text_buffer_apply_tag(tab->buffer, fold_tag, &sub_start, &sub_end);
                        gtk_text_buffer_apply_tag(tab->buffer, fold_hdr_tag, &ls, &le);
                    }
                }
            }
            if (!gtk_text_iter_forward_line(&scan)) break;
        }
        emacs_echo(tab, "OVERVIEW (top-level headings)");
        return;
    }

    if (state == 2) {
        /* State 2: CONTENTS (all headings visible, but all bodies folded) */
        GtkTextIter scan;
        gtk_text_buffer_get_start_iter(tab->buffer, &scan);
        while (!gtk_text_iter_is_end(&scan)) {
            GtkTextIter ls = scan;
            GtkTextIter le = ls;
            gtk_text_iter_forward_to_line_end(&le);
            gchar* text = gtk_text_buffer_get_text(tab->buffer, &ls, &le, FALSE);
            gint lev = orgmode_heading_level(text);
            g_free(text);

            if (lev > 0) {
                GtkTextIter sub_start = ls;
                if (gtk_text_iter_forward_line(&sub_start)) {
                    GtkTextIter sub_scan = sub_start;
                    GtkTextIter sub_end = sub_start;
                    while (!gtk_text_iter_is_end(&sub_scan)) {
                        GtkTextIter hls = sub_scan;
                        GtkTextIter hle = hls;
                        gtk_text_iter_forward_to_line_end(&hle);
                        gchar* ht = gtk_text_buffer_get_text(tab->buffer, &hls, &hle, FALSE);
                        gint hlev = orgmode_heading_level(ht);
                        g_free(ht);
                        if (hlev > 0) {
                            sub_end = hls;
                            break;
                        }
                        if (!gtk_text_iter_forward_line(&sub_scan)) {
                            gtk_text_buffer_get_end_iter(tab->buffer, &sub_end);
                            break;
                        }
                        sub_end = sub_scan;
                    }
                    if (!gtk_text_iter_equal(&sub_start, &sub_end)) {
                        gtk_text_buffer_apply_tag(tab->buffer, fold_tag, &sub_start, &sub_end);
                        gtk_text_buffer_apply_tag(tab->buffer, fold_hdr_tag, &ls, &le);
                    }
                }
            }
            if (!gtk_text_iter_forward_line(&scan)) break;
        }
        emacs_echo(tab, "CONTENTS (all headings visible)");
        return;
    }
}

/* ------------------------------------------------------------------------- */
/* Editor Orgmode Key Shortcuts                                              */
/* ------------------------------------------------------------------------- */

gboolean orgmode_editor_handle_key(AppData* app, EditorTab* tab, guint keyval, GdkModifierType state) {
    (void)app;
    if (tab == NULL || tab->buffer == NULL) return FALSE;
    gboolean is_org = (tab->mode == EDITOR_MODE_ORGMODE) ||
                      (tab->name != NULL && is_org_file(tab->name));
    if (!is_org) return FALSE;

    /* 1. Shift+Tab: Global Visibility Cycle in editor */
    if ((keyval == GDK_KEY_ISO_Left_Tab) || (keyval == GDK_KEY_Tab && (state & GDK_SHIFT_MASK))) {
        orgmode_editor_cycle_global_fold(tab);
        return TRUE;
    }

    /* 2. Tab: Fold / Unfold Heading if cursor is on a heading */
    if (keyval == GDK_KEY_Tab && !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK))) {
        if (orgmode_editor_toggle_heading_fold(tab)) {
            return TRUE;
        }
        /* Not on a heading: let Tab perform standard indentation / cell jump */
        return FALSE;
    }

    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cur, gtk_text_buffer_get_insert(tab->buffer));
    gint cur_line = gtk_text_iter_get_line(&cur);

    /* 3. Shift+Right / Shift+Left: Cycle TODO State */
    if ((keyval == GDK_KEY_Right || keyval == GDK_KEY_Left) && (state & GDK_SHIFT_MASK)) {
        if (orgmode_cycle_todo_at_line(tab->path, cur_line, tab->buffer)) {
            gtk_text_buffer_set_modified(tab->buffer, TRUE);
            editor_tab_label_update(tab);
            return TRUE;
        }
    }

    /* 4. Alt+Return: Org Insert Heading at current level (M-RET) */
    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) && (state & GDK_ALT_MASK)) {
        GtkTextIter line_start = cur;
        gtk_text_iter_set_line_offset(&line_start, 0);
        GtkTextIter line_end = line_start;
        gtk_text_iter_forward_to_line_end(&line_end);
        gchar* line_text = gtk_text_buffer_get_text(tab->buffer, &line_start, &line_end, FALSE);
        gint hlevel = orgmode_heading_level(line_text);
        g_free(line_text);

        if (hlevel == 0) hlevel = 1;

        GString* new_h = g_string_new("\n");
        for (gint k = 0; k < hlevel; k++) {
            g_string_append_c(new_h, '*');
        }
        g_string_append(new_h, " ");

        gtk_text_buffer_insert(tab->buffer, &cur, new_h->str, -1);
        g_string_free(new_h, TRUE);
        gtk_text_buffer_set_modified(tab->buffer, TRUE);
        editor_tab_label_update(tab);
        return TRUE;
    }

    /* 5. Ctrl+Return or Space on checkbox: Toggle Checkbox */
    if ((keyval == GDK_KEY_Return && (state & GDK_CONTROL_MASK)) ||
        (keyval == GDK_KEY_space && (state & GDK_CONTROL_MASK))) {
        if (orgmode_toggle_checkbox_at_line(tab->path, cur_line, tab->buffer)) {
            gtk_text_buffer_set_modified(tab->buffer, TRUE);
            editor_tab_label_update(tab);
            return TRUE;
        }
    }

    return FALSE;
}
