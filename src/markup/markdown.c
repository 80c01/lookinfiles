#include "markup/markdown.h"
#include "markup/markdown_link.h"
#include "markup/markdown_table.h"
#include "markup/markdown_parse.h"
#include "theme/fonts.h"
#include "theme/text_tags.h"
#include "media/image_utils.h"
#include "theme/icons.h"

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

#ifdef HAVE_GSV
/* GtkSourceView language of a code fence info string ("c", "py", "bash"...) */
static GtkSourceLanguage* code_language(const gchar* info) {
    if (info == NULL || info[0] == '\0') {
        return NULL;
    }
    static const struct { const gchar* alias; const gchar* id; } ALIASES[] = {
        {"javascript", "js"}, {"node", "js"}, {"ts", "typescript"}, {"py", "python3"},
        {"python", "python3"}, {"bash", "sh"}, {"shell", "sh"}, {"zsh", "sh"},
        {"console", "sh"}, {"yml", "yaml"}, {"md", "markdown"}, {"c++", "cpp"},
        {"cs", "c-sharp"}, {"csharp", "c-sharp"}, {"rs", "rust"}, {"golang", "go"},
        {"rb", "ruby"}, {"kt", "kotlin"}, {"htm", "html"}, {"patch", "diff"},
    };
    gchar* word = g_strndup(info, strcspn(info, " \t{"));
    gchar* id = g_ascii_strdown(word, -1);
    g_free(word);
    for (gsize i = 0; i < G_N_ELEMENTS(ALIASES); i++) {
        if (g_strcmp0(id, ALIASES[i].alias) == 0) {
            g_free(id);
            id = g_strdup(ALIASES[i].id);
            break;
        }
    }
    GtkSourceLanguageManager* lm = gtk_source_language_manager_get_default();
    GtkSourceLanguage* lang = gtk_source_language_manager_get_language(lm, id);
    if (lang == NULL) {
        gchar* file = g_strconcat("code.", id, NULL);
        lang = gtk_source_language_manager_guess_language(lm, file, NULL);
        g_free(file);
    }
    g_free(id);
    return lang;
}

/* Colour a code block inserted at `start` (offset in buffer) like the editor:
 * highlight a scratch GtkSourceBuffer with the Dark+ scheme and copy its
 * colours as tags ("md_hl_RRGGBB[bi]") */
static void highlight_code(GtkTextBuffer* buffer, gint start, const gchar* code, const gchar* info) {
    GtkSourceLanguage* lang = code_language(info);
    GtkSourceStyleScheme* scheme = gtk_source_style_scheme_manager_get_scheme(
        gtk_source_style_scheme_manager_get_default(), "vscode-dark");
    if (lang == NULL || scheme == NULL) {
        return;
    }
    GtkSourceBuffer* sb = gtk_source_buffer_new_with_language(lang);
    gtk_source_buffer_set_style_scheme(sb, scheme);
    gtk_source_buffer_set_highlight_syntax(sb, TRUE);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sb), code, -1);
    GtkTextIter it, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(sb), &it, &end);
    gtk_source_buffer_ensure_highlight(sb, &it, &end);

    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    while (!gtk_text_iter_equal(&it, &end)) {
        GtkTextIter next = it;
        if (!gtk_text_iter_forward_to_tag_toggle(&next, NULL)) {
            next = end;
        }
        GdkRGBA fg;
        gboolean has_fg = FALSE, bold = FALSE, italic = FALSE;
        GSList* tags = gtk_text_iter_get_tags(&it); /* ascending priority */
        for (GSList* l = tags; l != NULL; l = l->next) {
            gboolean fg_set = FALSE, weight_set = FALSE, style_set = FALSE;
            GdkRGBA* rgba = NULL;
            gint weight = 0;
            PangoStyle style = PANGO_STYLE_NORMAL;
            g_object_get(l->data, "foreground-set", &fg_set, "foreground-rgba", &rgba,
                         "weight-set", &weight_set, "weight", &weight,
                         "style-set", &style_set, "style", &style, NULL);
            if (fg_set && rgba != NULL) {
                fg = *rgba;
                has_fg = TRUE;
            }
            if (weight_set) bold = (weight >= PANGO_WEIGHT_SEMIBOLD);
            if (style_set) italic = (style == PANGO_STYLE_ITALIC);
            if (rgba != NULL) gdk_rgba_free(rgba);
        }
        g_slist_free(tags);
        if (has_fg) {
            gchar name[32];
            g_snprintf(name, sizeof name, "md_hl_%02x%02x%02x%s%s", (guint)(fg.red * 255 + 0.5),
                       (guint)(fg.green * 255 + 0.5), (guint)(fg.blue * 255 + 0.5),
                       bold ? "b" : "", italic ? "i" : "");
            if (gtk_text_tag_table_lookup(table, name) == NULL) {
                gtk_text_buffer_create_tag(buffer, name, "foreground-rgba", &fg,
                                           "weight", bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                                           "style", italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL,
                                           NULL);
            }
            GtkTextIter a, b;
            gtk_text_buffer_get_iter_at_offset(buffer, &a, start + gtk_text_iter_get_offset(&it));
            gtk_text_buffer_get_iter_at_offset(buffer, &b, start + gtk_text_iter_get_offset(&next));
            gtk_text_buffer_apply_tag_by_name(buffer, name, &a, &b);
        }
        it = next;
    }
    g_object_unref(sb);
}
#endif

/* Show the object o (its U+FFFC already in the text, except code) */
static void show_object(GtkTextBuffer* buffer, GtkTextView* view, const MdObject* o) {
    if (o->kind == MD_OBJECT_CODE) {
#ifdef HAVE_GSV
        highlight_code(buffer, o->offset, o->text, o->name);
#endif
        return;
    }
    GtkTextIter at, after;
    gtk_text_buffer_get_iter_at_offset(buffer, &at, o->offset);
    after = at;
    gtk_text_iter_forward_char(&after);
    gtk_text_buffer_delete(buffer, &at, &after);
    switch (o->kind) {
    case MD_OBJECT_IMAGE: {
        GdkTexture* tex = texture_new_for_pixbuf(GDK_PIXBUF(o->image));
        GdkPaintable* fit = fit_image_new(tex, view);
        gtk_text_buffer_insert_paintable(buffer, &at, fit);
        g_object_unref(fit);
        g_object_unref(tex);
        break;
    }
    case MD_OBJECT_GLYPH: {
        GdkPaintable* glyph = glyph_paintable(o->name);
        gtk_text_buffer_insert_paintable(buffer, &at, glyph);
        g_object_unref(glyph);
        break;
    }
    case MD_OBJECT_TABLE:
        if (view != NULL) {
            markdown_table_insert_grid(buffer, view, &at, o->rows, o->aligns, o->cols);
        }
        break;
    case MD_OBJECT_CODE:
        break;
    }
}

void markdown_show(GtkTextBuffer* buffer, GtkTextView* view, const MdOut* md) {
    markdown_link_clear(buffer);
    gtk_text_buffer_set_text(buffer, md->text->str, (gint)md->text->len);
    for (guint i = 0; i < md->objects->len; i++) {
        show_object(buffer, view, &g_array_index(md->objects, MdObject, i));
    }
    /* Tags by name, looked up once per name */
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    const gchar* last_name = NULL;
    GtkTextTag* last_tag = NULL;
    for (guint i = 0; i < md->spans->len; i++) {
        const MdSpan* span = &g_array_index(md->spans, MdSpan, i);
        if (span->tag != last_name) {
            last_name = span->tag; /* interned */
            last_tag = gtk_text_tag_table_lookup(table, span->tag);
        }
        if (last_tag != NULL) {
            GtkTextIter a, b;
            gtk_text_buffer_get_iter_at_offset(buffer, &a, span->start);
            gtk_text_buffer_get_iter_at_offset(buffer, &b, span->end);
            gtk_text_buffer_apply_tag(buffer, last_tag, &a, &b);
        }
    }
    for (guint i = 0; i < md->links->len; i++) {
        const MdLinkSpan* link = &g_array_index(md->links, MdLinkSpan, i);
        GtkTextIter a, b;
        gtk_text_buffer_get_iter_at_offset(buffer, &a, link->start);
        gtk_text_buffer_get_iter_at_offset(buffer, &b, link->end);
        markdown_link_add(buffer, &a, &b, link->url);
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
    markdown_show(buffer, view, md);
    md_out_free(md);
}
