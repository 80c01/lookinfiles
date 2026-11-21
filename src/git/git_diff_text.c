#include "git/git_diff_text.h"
#include "core/i18n.h"
#include "theme/text_tags.h"
#include <string.h>

gchar* git_diff_text_cut(gchar* text, guint max_lines) {
    if (text == NULL) return NULL;
    const gchar* p = text;
    for (guint n = 0; n < max_lines; n++) {
        p = strchr(p, '\n');
        if (p == NULL) {
            return text;
        }
        p++;
    }
    guint rest = 0;
    for (const gchar* q = p; *q != '\0'; q++) {
        rest += (*q == '\n');
    }
    if (rest == 0 && *p == '\0') {
        return text;
    }
    rest = MAX(rest, 1u); /* a last line without a line break */
    gchar* more = g_strdup_printf(ngettext("… %u more line not shown", "… %u more lines not shown", rest), rest);
    gchar* cut = g_strdup_printf("%.*s\n%s\n", (int)(p - text), text, more);
    g_free(more);
    g_free(text);
    return cut;
}

/* The tags of the diff lines, once per buffer */
static void ensure_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tags = gtk_text_buffer_get_tag_table(buffer);
    if (gtk_text_tag_table_lookup(tags, "diff_add") != NULL) return;
    /* Same look as the diff preview (preview_doc_view.c) */
    text_tag_create(buffer, "diff_add", "foreground", "@diff_added_fg",
                    "paragraph-background", "@diff_added_bg", NULL);
    text_tag_create(buffer, "diff_del", "foreground", "@diff_removed_fg",
                    "paragraph-background", "@diff_removed_bg", NULL);
    text_tag_create(buffer, "diff_hunk", "foreground", "@diff_hunk_fg",
                    "paragraph-background", "@diff_hunk_bg", NULL);
    text_tag_create(buffer, "diff_header", "foreground", "@syn_function", "weight", PANGO_WEIGHT_BOLD, NULL);
    text_tag_create(buffer, "diff_dim", "foreground", "@fg_subtle", NULL);
}

void git_diff_text_show(GtkTextBuffer* buffer, const gchar* raw) {
    gtk_text_buffer_set_text(buffer, "", -1);
    if (raw == NULL || raw[0] == '\0') {
        return;
    }
    ensure_tags(buffer);

    gchar** lines = g_strsplit(raw, "\n", -1);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(buffer, &iter);

    gboolean in_diff = FALSE; /* +/- lines before the first diff are the message and stat */
    for (gint i = 0; lines[i] != NULL; i++) {
        const gchar* line = lines[i];
        const gchar* tag_name = NULL;
        if (g_str_has_prefix(line, "diff --git")) {
            in_diff = TRUE;
        }

        if (g_str_has_prefix(line, "commit ") ||
            g_str_has_prefix(line, "Author: ") ||
            g_str_has_prefix(line, "Date: ")) {
            tag_name = "diff_header";
        } else if (g_str_has_prefix(line, "diff --git") ||
                   g_str_has_prefix(line, "index ") ||
                   g_str_has_prefix(line, "--- ") ||
                   g_str_has_prefix(line, "+++ ")) {
            tag_name = "diff_dim";
        } else if (g_str_has_prefix(line, "@@")) {
            tag_name = "diff_hunk";
        } else if (in_diff && line[0] == '+') {
            tag_name = "diff_add";
        } else if (in_diff && line[0] == '-') {
            tag_name = "diff_del";
        }

        gchar* with_nl = g_strconcat(line, "\n", NULL);
        if (tag_name != NULL) {
            gtk_text_buffer_insert_with_tags_by_name(buffer, &iter, with_nl, -1, tag_name, NULL);
        } else {
            gtk_text_buffer_insert(buffer, &iter, with_nl, -1);
        }
        g_free(with_nl);
    }
    g_strfreev(lines);
}
