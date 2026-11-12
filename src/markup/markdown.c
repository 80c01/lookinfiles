#include "markup/markdown.h"
#include "markup/markdown_parse.h"
#include "markup/md_show.h"
#include "theme/fonts.h"
#include "theme/text_tags.h"

void ensure_markdown_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tt = gtk_text_buffer_get_tag_table(buffer);

    /* Colours and fonts as roles ("@accent"): they follow theme changes */
    #define CREATE_TAG(name, ...) text_tag_ensure(buffer, name, __VA_ARGS__, NULL)

    /* VS Code Markdown preview look (Dark+): headings in the text colour with
     * a rule under h1/h2, quiet lists, padded code panels, quotes with a bar.
     * Tags created later win: base spacing first, highlighting tags last. */
    CREATE_TAG("md_body",
               "pixels-inside-wrap", 4);

    CREATE_TAG("md_gap", /* blank line between blocks */
               "size-points", 8.0);

    CREATE_TAG("md_block_gap", /* extra space around code panels */
               "size-points", 7.0);

    CREATE_TAG("md_h1",
               "scale", FONT_SCALE_H1,
               "weight", 600,
               "foreground", "@fg_strong",
               "pixels-above-lines", 16,
               "pixels-below-lines", 6);

    CREATE_TAG("md_h2",
               "scale", FONT_SCALE_H2,
               "weight", 600,
               "foreground", "@fg_strong",
               "pixels-above-lines", 16,
               "pixels-below-lines", 6);

    CREATE_TAG("md_h3",
               "scale", FONT_SCALE_H3,
               "weight", 600,
               "foreground", "@fg_strong",
               "pixels-above-lines", 14,
               "pixels-below-lines", 4);

    CREATE_TAG("md_h4",
               "scale", FONT_SCALE_H4,
               "weight", 600,
               "foreground", "@fg_strong",
               "pixels-above-lines", 12,
               "pixels-below-lines", 4);

    CREATE_TAG("md_h5",
               "scale", FONT_SCALE_H5,
               "weight", 600,
               "foreground", "@fg_strong",
               "pixels-above-lines", 10,
               "pixels-below-lines", 4);

    CREATE_TAG("md_h6",
               "scale", FONT_SCALE_H6,
               "weight", 600,
               "foreground", "@fg_muted",
               "pixels-above-lines", 10,
               "pixels-below-lines", 4);

    /* Thin full-width line (under h1/h2, horizontal rule): a line holding a
     * space in a tiny font, with a paragraph background (GtkTextView paints no
     * background on a line without characters) */
    CREATE_TAG("md_rule",
               "size-points", 1.0,
               "paragraph-background", "@rule");

    CREATE_TAG("md_hr",
               "size-points", 1.6,
               "paragraph-background", "@rule");

    CREATE_TAG("md_bold",
               "weight", PANGO_WEIGHT_BOLD);

    CREATE_TAG("md_italic",
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("md_bold_italic",
               "weight", PANGO_WEIGHT_BOLD,
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("md_strike",
               "strikethrough", TRUE,
               "foreground", "@fg_subtle");

    CREATE_TAG("md_inline_code",
               "family", "@font_code",
               "scale", FONT_SCALE_CODE,
               "background", "@bg_code_inline",
               "foreground", "@syn_escape");

    /* Code block: padded panel */
    CREATE_TAG("md_code_block",
               "family", "@font_code",
               "scale", FONT_SCALE_CODE,
               "foreground", "@fg_text",
               "paragraph-background", "@bg_block",
               "left-margin", 10,
               "indent", 16, /* every code line is a paragraph: 16 px inner padding */
               "right-margin", 16 + TEXT_SCROLLBAR_CLEARANCE,
               "wrap-mode", GTK_WRAP_NONE,
               "pixels-inside-wrap", 0,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    /* Padding lines above and below the code (VS Code: 16 px). Code and
     * padding lines share the same margins: a paragraph background starts at
     * its line's left margin, so different margins would notch the panel. */
    CREATE_TAG("md_code_label",
               "foreground", "@fg_subtle",
               "style", PANGO_STYLE_ITALIC,
               "scale", FONT_SCALE_SMALL,
               "left-margin", 10,
               NULL);
    CREATE_TAG("md_code_pad",
               "size-points", 10.0,
               "left-margin", 10,
               "right-margin", 16 + TEXT_SCROLLBAR_CLEARANCE,
               "paragraph-background", "@bg_block");

    CREATE_TAG("md_quote",
               "foreground", "@fg_muted",
               "paragraph-background", "@bg_block",
               "left-margin", 10,
               "pixels-above-lines", 0,
               "pixels-below-lines", 0);

    CREATE_TAG("md_quote_bar",
               "foreground", "@accent");

    CREATE_TAG("md_list_bullet",
               "foreground", "@fg");

    /* Disc / circle / square list markers, sized like VS Code's (the plain
     * bullet glyph is much smaller) */
    CREATE_TAG("md_list_disc",
               "foreground", "@fg",
               "scale", 0.62,
               "rise", 1 * PANGO_SCALE);

    CREATE_TAG("md_list_task_done",
               "foreground", "@fg");

    CREATE_TAG("md_list_task_todo",
               "foreground", "@fg");

    /* In-progress task ([/] or [-]): half-filled box, VS Code's warning amber */
    CREATE_TAG("md_list_task_running",
               "foreground", "@warning");

    CREATE_TAG("md_link",
               "foreground", "@link");

    CREATE_TAG("md_table",
               "family", "@font_code",
               "scale", FONT_SCALE_CODE,
               "foreground", "@fg",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 10,
               "pixels-inside-wrap", 0,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("md_table_header",
               "family", "@font_code",
               "scale", FONT_SCALE_CODE,
               "weight", PANGO_WEIGHT_BOLD,
               "foreground", "@fg_strong",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 10,
               "pixels-inside-wrap", 0,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    CREATE_TAG("md_table_border",
               "family", "@font_code",
               "scale", FONT_SCALE_CODE,
               "foreground", "@rule",
               "wrap-mode", GTK_WRAP_NONE,
               "left-margin", 10,
               "pixels-inside-wrap", 0,
               "pixels-above-lines", 1,
               "pixels-below-lines", 1);

    /* GitHub alerts: coloured bar and title, body like a quote */
    CREATE_TAG("md_alert_note", "foreground", "@link", "weight", 600);
    CREATE_TAG("md_alert_tip", "foreground", "@success", "weight", 600);
    CREATE_TAG("md_alert_important", "foreground", "@syn_control", "weight", 600);
    CREATE_TAG("md_alert_warning", "foreground", "@warning", "weight", 600);
    CREATE_TAG("md_alert_caution", "foreground", "@error", "weight", 600);

    CREATE_TAG("md_image",
               "foreground", "@syn_variable",
               "style", PANGO_STYLE_ITALIC);

    CREATE_TAG("md_preview_banner",
               "family", "@font_code",
               "scale", FONT_SCALE_SMALL,
               "foreground", "@syn_function",
               "background", "@bg_code_inline",
               "pixels-above-lines", 4,
               "pixels-below-lines", 8);

    #undef CREATE_TAG

    /* List items: the marker sits at the start of the line and a tab stop
     * puts the text at a fixed position, so the first line and the wrapped
     * lines start at the same x whatever the marker width (VS Code: 2em per
     * level). md_li_*: left-margin = marker x, negative indent = hanging lines. */
    for (gint d = 0; d < 8; d++) {
        gchar tname[32];
        gint text_x = 34 + d * 24;
        static const struct { const gchar* kind; gint marker_w; } KINDS[] = {
            {"ul", 16}, {"ol", 24}, {"task", 20},
        };
        for (gsize k = 0; k < G_N_ELEMENTS(KINDS); k++) {
            g_snprintf(tname, sizeof(tname), "md_li_%s_d%d", KINDS[k].kind, d);
            if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
                gint w = KINDS[k].marker_w;
                PangoTabArray* tabs = pango_tab_array_new_with_positions(1, TRUE, PANGO_TAB_LEFT, w);
                gtk_text_buffer_create_tag(buffer, tname,
                    "left-margin", text_x - w,
                    "indent", -w,
                    "tabs", tabs,
                    NULL);
                pango_tab_array_free(tabs);
            }
        }
        /* Continuation paragraph of a list item: aligned with the item text */
        for (gsize k = 0; k < 2; k++) {
            g_snprintf(tname, sizeof(tname), "md_li_p_%s_d%d", k == 0 ? "ul" : "ol", d);
            if (gtk_text_tag_table_lookup(tt, tname) == NULL) {
                gtk_text_buffer_create_tag(buffer, tname,
                    "left-margin", text_x,
                    "indent", 0,
                    "pixels-above-lines", 6,
                    "pixels-below-lines", 4,
                    NULL);
            }
        }
    }
}

void render_markdown_to_buffer(GtkTextBuffer* buffer,
                               GtkTextView* view,
                               const gchar* markdown_text,
                               const gchar* doc_path,
                               gboolean is_preview,
                               const gchar* file_name,
                               gint64 total_size) {
    MdOut* md = markdown_parse(markdown_text, doc_path, is_preview, file_name, total_size, view != NULL);
    md_out_show(buffer, view, md);
    md_out_free(md);
}
