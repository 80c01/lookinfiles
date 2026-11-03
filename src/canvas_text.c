#include "canvas_text.h"
#include "fonts.h"
#include "palette.h"
#include "text_tags.h"

#include <ctype.h>
#include <string.h>

const gchar* const CANVAS_TEXT_STYLE_TAGS[CANVAS_TEXT_STYLE_COUNT] = {
    "b", "i", "u", "s", "code", "h",
    "h2", "mark",
    "serif", "mono",
    "sm", "lg",
    "c-muted", "c-accent", "c-danger", "c-success", "c-warning"
};

GtkTextTag* canvas_text_style_tag(GtkTextBuffer* buffer, guint bit_index) {
    if (bit_index >= CANVAS_TEXT_STYLE_COUNT) return NULL;
    const gchar* name = CANVAS_TEXT_STYLE_TAGS[bit_index];
    switch (bit_index) {
    case 0: /* b */
        return text_tag_ensure(buffer, name, "weight", PANGO_WEIGHT_BOLD, NULL);
    case 1: /* i */
        return text_tag_ensure(buffer, name, "style", PANGO_STYLE_ITALIC, NULL);
    case 2: /* u */
        return text_tag_ensure(buffer, name, "underline", PANGO_UNDERLINE_SINGLE, NULL);
    case 3: /* s */
        return text_tag_ensure(buffer, name, "strikethrough", TRUE, NULL);
    case 4: /* code */
        return text_tag_ensure(buffer, name, "family", "@font_code", "background", "@bg_code_inline",
                               "scale", FONT_SCALE_CODE, NULL);
    case 5: /* h */
        return text_tag_ensure(buffer, name, "weight", PANGO_WEIGHT_BOLD, "scale", FONT_SCALE_H1, NULL);
    case 6: /* h2 */
        return text_tag_ensure(buffer, name, "weight", PANGO_WEIGHT_BOLD, "scale", FONT_SCALE_H2, NULL);
    case 7: /* mark */
        return text_tag_ensure(buffer, name, "background", "@bg_warning", "foreground", "@fg", NULL);
    case 8: /* serif */
        return text_tag_ensure(buffer, name, "family", "@font_serif", NULL);
    case 9: /* mono */
        return text_tag_ensure(buffer, name, "family", "@font_code", NULL);
    case 10: /* sm */
        return text_tag_ensure(buffer, name, "scale", FONT_SCALE_SMALL, NULL);
    case 11: /* lg */
        return text_tag_ensure(buffer, name, "scale", FONT_SCALE_H3, NULL);
    case 12: /* c-muted */
        return text_tag_ensure(buffer, name, "foreground", "@fg_subtle", NULL);
    case 13: /* c-accent */
        return text_tag_ensure(buffer, name, "foreground", "@accent", NULL);
    case 14: /* c-danger */
        return text_tag_ensure(buffer, name, "foreground", "@error", NULL);
    case 15: /* c-success */
        return text_tag_ensure(buffer, name, "foreground", "@success", NULL);
    case 16: /* c-warning */
        return text_tag_ensure(buffer, name, "foreground", "@warning", NULL);
    default:
        return NULL;
    }
}

void canvas_text_ensure_tags(GtkTextBuffer* buffer) {
    for (guint k = 0; k < CANVAS_TEXT_STYLE_COUNT; k++) {
        canvas_text_style_tag(buffer, k);
    }
    for (gint level = 1; level <= 6; level++) {
        gchar* name = g_strdup_printf("indent_%d", level);
        text_tag_ensure(buffer, name, "left-margin", level * 24, NULL);
        g_free(name);
    }
}

guint canvas_text_styles_at(GtkTextBuffer* buffer, const GtkTextIter* iter) {
    guint styles = 0;
    for (guint k = 0; k < CANVAS_TEXT_STYLE_COUNT; k++) {
        GtkTextTag* tag = canvas_text_style_tag(buffer, k);
        if (tag != NULL && gtk_text_iter_has_tag(iter, tag)) styles |= 1u << k;
    }
    return styles;
}

void canvas_text_apply_styles(GtkTextBuffer* buffer, gint start, gint end, guint styles) {
    GtkTextIter s, e;
    for (guint k = 0; k < CANVAS_TEXT_STYLE_COUNT; k++) {
        if ((styles & (1u << k)) == 0) continue;
        gtk_text_buffer_get_iter_at_offset(buffer, &s, start);
        gtk_text_buffer_get_iter_at_offset(buffer, &e, end);
        GtkTextTag* tag = canvas_text_style_tag(buffer, k);
        if (tag != NULL) gtk_text_buffer_apply_tag(buffer, tag, &s, &e);
    }
}

void canvas_text_toggle_style(GtkTextBuffer* buffer, guint* pending, guint style) {
    guint k = (guint)g_bit_nth_lsf(style, -1);
    GtkTextIter s, e;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &s, &e)) {
        if (pending != NULL) *pending ^= style;
        return;
    }
    GtkTextTag* tag = canvas_text_style_tag(buffer, k);
    if (tag == NULL) return;
    gboolean all = TRUE;
    for (GtkTextIter i = s; gtk_text_iter_compare(&i, &e) < 0; gtk_text_iter_forward_char(&i)) {
        if (!gtk_text_iter_has_tag(&i, tag)) {
            all = FALSE;
            break;
        }
    }
    if (all) {
        gtk_text_buffer_remove_tag(buffer, tag, &s, &e);
    } else {
        gtk_text_buffer_apply_tag(buffer, tag, &s, &e);
    }
}

void canvas_text_apply_preset(GtkTextBuffer* buffer, CanvasPreset preset) {
    GtkTextIter s, e;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &s, &e)) {
        GtkTextMark* ins = gtk_text_buffer_get_insert(buffer);
        gtk_text_buffer_get_iter_at_mark(buffer, &s, ins);
        gtk_text_iter_set_line_offset(&s, 0);
        e = s;
        if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
    }
    GtkTextTag* h1 = canvas_text_style_tag(buffer, 5);
    GtkTextTag* h2 = canvas_text_style_tag(buffer, 6);
    GtkTextTag* mark = canvas_text_style_tag(buffer, 7);
    GtkTextTag* code = canvas_text_style_tag(buffer, 4);
    GtkTextTag* muted = canvas_text_style_tag(buffer, 12);
    GtkTextTag* italic = canvas_text_style_tag(buffer, 1);

    if (h1) gtk_text_buffer_remove_tag(buffer, h1, &s, &e);
    if (h2) gtk_text_buffer_remove_tag(buffer, h2, &s, &e);
    if (mark) gtk_text_buffer_remove_tag(buffer, mark, &s, &e);
    if (code) gtk_text_buffer_remove_tag(buffer, code, &s, &e);
    if (muted) gtk_text_buffer_remove_tag(buffer, muted, &s, &e);
    if (italic) gtk_text_buffer_remove_tag(buffer, italic, &s, &e);

    switch (preset) {
    case CANVAS_PRESET_H1:
        if (h1) gtk_text_buffer_apply_tag(buffer, h1, &s, &e);
        break;
    case CANVAS_PRESET_H2:
        if (h2) gtk_text_buffer_apply_tag(buffer, h2, &s, &e);
        break;
    case CANVAS_PRESET_BODY:
        break;
    case CANVAS_PRESET_QUOTE:
        if (muted) gtk_text_buffer_apply_tag(buffer, muted, &s, &e);
        if (italic) gtk_text_buffer_apply_tag(buffer, italic, &s, &e);
        canvas_text_indent(buffer, 1);
        break;
    }
}

static gint line_indent_level(GtkTextBuffer* buffer, const GtkTextIter* line_iter) {
    for (gint lvl = 6; lvl >= 1; lvl--) {
        gchar* name = g_strdup_printf("indent_%d", lvl);
        GtkTextTag* tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name);
        g_free(name);
        if (tag != NULL && gtk_text_iter_has_tag(line_iter, tag)) return lvl;
    }
    return 0;
}

void canvas_text_indent(GtkTextBuffer* buffer, gint step) {
    GtkTextIter s, e;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &s, &e)) {
        GtkTextMark* ins = gtk_text_buffer_get_insert(buffer);
        gtk_text_buffer_get_iter_at_mark(buffer, &s, ins);
        e = s;
    }
    gint start_line = gtk_text_iter_get_line(&s);
    gint end_line = gtk_text_iter_get_line(&e);
    for (gint line = start_line; line <= end_line; line++) {
        GtkTextIter line_start, line_end;
        gtk_text_buffer_get_iter_at_line(buffer, &line_start, line);
        line_end = line_start;
        if (!gtk_text_iter_ends_line(&line_end)) gtk_text_iter_forward_to_line_end(&line_end);

        gint cur_lvl = line_indent_level(buffer, &line_start);
        for (gint lvl = 1; lvl <= 6; lvl++) {
            gchar* name = g_strdup_printf("indent_%d", lvl);
            GtkTextTag* tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name);
            g_free(name);
            if (tag != NULL) gtk_text_buffer_remove_tag(buffer, tag, &line_start, &line_end);
        }
        gint new_lvl = CLAMP(cur_lvl + step, 0, 6);
        if (new_lvl > 0) {
            gchar* name = g_strdup_printf("indent_%d", new_lvl);
            GtkTextTag* tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name);
            g_free(name);
            if (tag != NULL) gtk_text_buffer_apply_tag(buffer, tag, &line_start, &line_end);
        }
    }
}

gboolean canvas_text_toggle_checkbox(GtkTextBuffer* buffer) {
    GtkTextMark* ins = gtk_text_buffer_get_insert(buffer);
    GtkTextIter iter, line_start, line_end;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, ins);
    line_start = iter;
    gtk_text_iter_set_line_offset(&line_start, 0);
    line_end = line_start;
    if (!gtk_text_iter_ends_line(&line_end)) gtk_text_iter_forward_to_line_end(&line_end);

    gchar* line = gtk_text_iter_get_text(&line_start, &line_end);
    gchar* cb = strstr(line, "[ ]");
    gchar* cb_done = strstr(line, "[X]");
    if (cb_done == NULL) cb_done = strstr(line, "[x]");

    if (cb != NULL) {
        gint offset = (gint)(cb - line);
        GtkTextIter c1 = line_start, c2 = line_start;
        gtk_text_iter_forward_chars(&c1, offset + 1);
        c2 = c1;
        gtk_text_iter_forward_char(&c2);
        gtk_text_buffer_delete(buffer, &c1, &c2);
        gtk_text_buffer_insert(buffer, &c1, "X", 1);
        g_free(line);
        return TRUE;
    } else if (cb_done != NULL) {
        gint offset = (gint)(cb_done - line);
        GtkTextIter c1 = line_start, c2 = line_start;
        gtk_text_iter_forward_chars(&c1, offset + 1);
        c2 = c1;
        gtk_text_iter_forward_char(&c2);
        gtk_text_buffer_delete(buffer, &c1, &c2);
        gtk_text_buffer_insert(buffer, &c1, " ", 1);
        g_free(line);
        return TRUE;
    } else {
        /* No checkbox on line: insert "- [ ] " at start */
        gtk_text_buffer_insert(buffer, &line_start, "- [ ] ", 6);
        g_free(line);
        return TRUE;
    }
}

gboolean canvas_text_click_checkbox(GtkTextView* view, gdouble x, gdouble y) {
    gint bx, by;
    gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_WIDGET, (gint)x, (gint)y, &bx, &by);
    GtkTextIter iter, line_start, line_end;
    gtk_text_view_get_iter_at_location(view, &iter, bx, by);
    line_start = iter;
    gtk_text_iter_set_line_offset(&line_start, 0);
    line_end = line_start;
    if (!gtk_text_iter_ends_line(&line_end)) gtk_text_iter_forward_to_line_end(&line_end);

    gchar* line = gtk_text_iter_get_text(&line_start, &line_end);
    gchar* pos_box = strstr(line, "[ ]");
    gchar* pos_done = strstr(line, "[X]");
    if (pos_done == NULL) pos_done = strstr(line, "[x]");
    gchar* found = (pos_box != NULL) ? pos_box : pos_done;

    if (found != NULL) {
        gint char_idx = gtk_text_iter_get_line_offset(&iter);
        gint box_start = (gint)(found - line);
        /* If clicked near the brackets: [ ] is 3 chars */
        if (char_idx >= box_start && char_idx <= box_start + 3) {
            GtkTextBuffer* buf = gtk_text_view_get_buffer(view);
            GtkTextIter c1 = line_start, c2 = line_start;
            gtk_text_iter_forward_chars(&c1, box_start + 1);
            c2 = c1;
            gtk_text_iter_forward_char(&c2);
            gtk_text_buffer_delete(buf, &c1, &c2);
            gtk_text_buffer_insert(buf, &c1, (pos_box != NULL) ? "X" : " ", 1);
            g_free(line);
            return TRUE;
        }
    }
    g_free(line);
    return FALSE;
}

void canvas_text_insert_list(GtkTextBuffer* buffer, const gchar* prefix) {
    if (prefix == NULL) return;
    GtkTextMark* ins = gtk_text_buffer_get_insert(buffer);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, ins);
    gtk_text_iter_set_line_offset(&iter, 0);
    gtk_text_buffer_insert(buffer, &iter, prefix, -1);
}

gboolean canvas_text_handle_enter(GtkTextView* view) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);
    GtkTextMark* ins = gtk_text_buffer_get_insert(buffer);
    GtkTextIter iter, line_start;
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, ins);
    line_start = iter;
    gtk_text_iter_set_line_offset(&line_start, 0);

    gchar* line = gtk_text_iter_get_text(&line_start, &iter);
    const gchar* p = line;
    while (*p == ' ' || *p == '\t') p++;

    /* Checkbox list: "- [ ] " or "- [X] " */
    if ((strncmp(p, "- [ ] ", 6) == 0) || (strncmp(p, "- [X] ", 6) == 0) || (strncmp(p, "- [x] ", 6) == 0)) {
        if (p[6] == '\0') {
            /* Empty item: remove checkbox on Enter */
            gtk_text_buffer_delete(buffer, &line_start, &iter);
            g_free(line);
            return TRUE;
        }
        gtk_text_buffer_insert_at_cursor(buffer, "\n- [ ] ", 7);
        g_free(line);
        return TRUE;
    }

    /* Bullet list: "- " or "* " */
    if ((p[0] == '-' || p[0] == '*') && p[1] == ' ') {
        if (p[2] == '\0') {
            /* Empty bullet: clear bullet */
            gtk_text_buffer_delete(buffer, &line_start, &iter);
            g_free(line);
            return TRUE;
        }
        gchar next_bullet[8];
        g_snprintf(next_bullet, sizeof(next_bullet), "\n%c ", p[0]);
        gtk_text_buffer_insert_at_cursor(buffer, next_bullet, -1);
        g_free(line);
        return TRUE;
    }

    /* Numbered list: "1. ", "2. " */
    if (isdigit((unsigned char)p[0])) {
        gchar* dot = strstr(p, ". ");
        if (dot != NULL && dot < p + 6) {
            if (dot[2] == '\0') {
                /* Empty numbered line: clear it */
                gtk_text_buffer_delete(buffer, &line_start, &iter);
                g_free(line);
                return TRUE;
            }
            gint num = (gint)g_ascii_strtoll(p, NULL, 10);
            gchar next_num[32];
            g_snprintf(next_num, sizeof(next_num), "\n%d. ", num + 1);
            gtk_text_buffer_insert_at_cursor(buffer, next_num, -1);
            g_free(line);
            return TRUE;
        }
    }

    g_free(line);
    return FALSE;
}
