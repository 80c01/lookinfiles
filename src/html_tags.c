/* Text tags of the HTML preview (see html_tags.h) */
#include "html_tags.h"
#include "theme/fonts.h"
#include "theme/text_tags.h"

void ensure_html_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tt = gtk_text_buffer_get_tag_table(buffer);

    /* Colours and fonts as roles ("@accent"): they follow theme changes */
    #define CREATE_TAG(name, ...) text_tag_ensure(buffer, name, __VA_ARGS__, NULL)

    CREATE_TAG("html_title",
               "scale", FONT_SCALE_H2,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_strong",
               "pixels-above-lines", 8,
               "pixels-below-lines", 4);

    CREATE_TAG("html_h1",
               "scale", FONT_SCALE_H1,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_strong",
               "pixels-above-lines", 14,
               "pixels-below-lines", 6);

    CREATE_TAG("html_h2",
               "scale", FONT_SCALE_H2,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_strong",
               "pixels-above-lines", 12,
               "pixels-below-lines", 4);

    CREATE_TAG("html_h3",
               "scale", FONT_SCALE_H3,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_strong",
               "pixels-above-lines", 10,
               "pixels-below-lines", 3);

    CREATE_TAG("html_h4",
               "scale", FONT_SCALE_H4,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_strong",
               "pixels-above-lines", 8,
               "pixels-below-lines", 2);

    CREATE_TAG("html_h5",
               "scale", FONT_SCALE_H5,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_strong",
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("html_h6",
               "scale", FONT_SCALE_H6,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_muted",
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("html_bold",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_italic",
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("html_underline",
               "underline", PANGO_UNDERLINE_SINGLE);

    CREATE_TAG("html_strike",
               "strikethrough", TRUE,
               "foreground", "@fg_subtle");

    CREATE_TAG("html_code",
               "family", "@font_code",
               "scale", FONT_SCALE_CODE,
               "background", "@bg_code_inline",
               "foreground", "@syn_escape");

    CREATE_TAG("html_kbd",
               "family", "@font_code",
               "scale", FONT_SCALE_CODE,
               "background", "@bg_code_inline",
               "foreground", "@syn_function",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_pre",
               "family", "@font_code",
               "scale", FONT_SCALE_CODE,
               "background", "@bg_block",
               "foreground", "@fg_text",
               "left-margin", 24,
               "right-margin", 24,
               "wrap-mode", GTK_WRAP_NONE,
               "pixels-above-lines", 3,
               "pixels-below-lines", 3);

    CREATE_TAG("html_code_header",
               "family", "@font_code",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@fg_subtle",
               "background", "@bg_view",
               "left-margin", 24,
               "pixels-above-lines", 8,
               "pixels-below-lines", 2);

    CREATE_TAG("html_quote",
               "style", PANGO_STYLE_ITALIC,
               "foreground", "@fg_muted",
               "left-margin", 24,
               "pixels-above-lines", 2,
               "pixels-below-lines", 2);

    CREATE_TAG("html_quote_bar",
               "foreground", "@accent",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_link",
               "foreground", "@link",
               "underline", PANGO_UNDERLINE_SINGLE);

    CREATE_TAG("html_mark",
               "background", "@highlight",
               "foreground", "@highlight_fg");

    CREATE_TAG("html_small",
               "scale", FONT_SCALE_SMALL);

    CREATE_TAG("html_sub",
               "scale", 0.8);

    CREATE_TAG("html_sup",
               "scale", 0.8);

    CREATE_TAG("html_bullet",
               "foreground", "@fg",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_num",
               "foreground", "@fg",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_task_done",
               "foreground", "@syn_comment",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_task_todo",
               "foreground", "@syn_function",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("html_hr",
               "foreground", "@rule",
               "pixels-above-lines", 8,
               "pixels-below-lines", 8);

    CREATE_TAG("html_table",
               "family", "@font_code",
               "foreground", "@fg",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 16,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("html_table_header",
               "family", "@font_code",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_strong",
               "background", "@bg_code_inline",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 16,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("html_table_border",
               "family", "@font_code",
               "foreground", "@rule",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 16,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("html_table_caption",
               "family", "@font_code",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_subtle",
               "left-margin", 16,
               "pixels-above-lines", 4,
               "pixels-below-lines", 2);

    CREATE_TAG("html_tag",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@fg_subtle");

    CREATE_TAG("html_preview_header",
               "family", "@font_code",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@fg_subtle",
               "background", "@bg_view",
               "left-margin", 16,
               "pixels-above-lines", 6,
               "pixels-below-lines", 4);

    CREATE_TAG("html_meta_sub",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@fg_subtle",
               "style", PANGO_STYLE_ITALIC,
               "left-margin", 16,
               "pixels-above-lines", 2,
               "pixels-below-lines", 6);

    CREATE_TAG("html_dt",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_strong",
               "pixels-above-lines", 6,
               "pixels-below-lines", 2);

    CREATE_TAG("html_dd",
               "left-margin", 28,
               "foreground", "@fg",
               "pixels-below-lines", 3);

    CREATE_TAG("html_summary",
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@accent_fg",
               "pixels-above-lines", 6,
               "pixels-below-lines", 3);

    CREATE_TAG("html_figcaption",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@fg_subtle",
               "style", PANGO_STYLE_ITALIC,
               "left-margin", 24,
               "pixels-above-lines", 2,
               "pixels-below-lines", 4);

    #undef CREATE_TAG

    for (gint d = 0; d < 8; d++) {
        gchar tname[32];
        gint base_margin = 18 + d * 20;

        /* Unordered list item: bullet at base_margin, text & wrapped lines at base_margin + 14 */
        g_snprintf(tname, sizeof(tname), "html_li_ul_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin,
                "indent", -14,
                NULL);
        }

        /* Ordered list item: number at base_margin, text & wrapped lines at base_margin + 19 */
        g_snprintf(tname, sizeof(tname), "html_li_ol_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin,
                "indent", -19,
                NULL);
        }

        /* Task list item: checkbox at base_margin, text & wrapped lines at base_margin + 17 */
        g_snprintf(tname, sizeof(tname), "html_li_task_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin,
                "indent", -17,
                NULL);
        }

        /* Continuation paragraph in unordered list: aligns with text at base_margin + 14 */
        g_snprintf(tname, sizeof(tname), "html_li_p_ul_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin + 14,
                "indent", 0,
                "pixels-above-lines", 6,
                "pixels-below-lines", 4,
                NULL);
        }

        /* Continuation paragraph in ordered / task list: aligns with text at base_margin + 19 */
        g_snprintf(tname, sizeof(tname), "html_li_p_ol_d%d", d);
        if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
            gtk_text_buffer_create_tag(buffer, tname,
                "left-margin", base_margin + 19,
                "indent", 0,
                "pixels-above-lines", 6,
                "pixels-below-lines", 4,
                NULL);
        }
    }
}
