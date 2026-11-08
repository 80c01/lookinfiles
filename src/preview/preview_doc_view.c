/* Display of PreviewDocs in the preview text view (see preview_doc_view.h). */
#include "preview/preview_doc_view.h"
#include "theme/fonts.h"
#include "theme/text_tags.h"

#include <string.h>

#include "markup/html.h"
#include "markup/html_tags.h"
#include "media/image_utils.h"
#include "markup/markdown.h"
#include "theme/palette.h"
#include "theme/text_style.h"

/* Tag name of a style */
static const gchar* const STYLE_TAGS[PREVIEW_STYLE_COUNT] = {
    [PREVIEW_STYLE_TITLE] = "pd_title",
    [PREVIEW_STYLE_DIM] = "pd_dim",
    [PREVIEW_STYLE_SECTION] = "pd_section",
    [PREVIEW_STYLE_BOLD] = "pd_bold",
    [PREVIEW_STYLE_ITALIC] = "pd_italic",
    [PREVIEW_STYLE_UNDERLINE] = "pd_underline",
    [PREVIEW_STYLE_STRIKE] = "pd_strike",
    [PREVIEW_STYLE_HEADER] = "pd_header",
    [PREVIEW_STYLE_ROW_ALT] = "pd_row_alt",
    [PREVIEW_STYLE_GRID] = "pd_grid",
    [PREVIEW_STYLE_KEY] = "pd_key",
    [PREVIEW_STYLE_STRING] = "pd_string",
    [PREVIEW_STYLE_NUMBER] = "pd_number",
    [PREVIEW_STYLE_KEYWORD] = "pd_keyword",
    [PREVIEW_STYLE_COMMENT] = "pd_comment",
    [PREVIEW_STYLE_ADDED] = "pd_added",
    [PREVIEW_STYLE_REMOVED] = "pd_removed",
    [PREVIEW_STYLE_HUNK] = "pd_hunk",
    [PREVIEW_STYLE_FILE] = "pd_file",
    [PREVIEW_STYLE_ERROR] = "pd_error",
    [PREVIEW_STYLE_WARNING] = "pd_warning",
    [PREVIEW_STYLE_INFO] = "pd_info",
    [PREVIEW_STYLE_SUCCESS] = "pd_success",
    [PREVIEW_STYLE_LINK] = "pd_link",
    [PREVIEW_STYLE_HEADING1] = "pd_h1",
    [PREVIEW_STYLE_HEADING2] = "pd_h2",
    [PREVIEW_STYLE_HEADING3] = "pd_h3",
    [PREVIEW_STYLE_CODE] = "pd_code",
    [PREVIEW_STYLE_MONO] = "pd_mono",
    [PREVIEW_STYLE_INDENT] = "pd_indent",
    [PREVIEW_STYLE_INDENT2] = "pd_indent2",
    [PREVIEW_STYLE_INDENT3] = "pd_indent3",
    [PREVIEW_STYLE_SUPERSCRIPT] = "pd_sup",
    [PREVIEW_STYLE_SUBSCRIPT] = "pd_sub",
    [PREVIEW_STYLE_TABLE] = "pd_table",
    [PREVIEW_STYLE_OUTLINE1] = "pd_outline_h1",
    [PREVIEW_STYLE_OUTLINE2] = "pd_outline_h2",
    [PREVIEW_STYLE_OUTLINE3] = "pd_outline_h3",
    [PREVIEW_STYLE_OUTLINE4] = "pd_outline_h4",
    [PREVIEW_STYLE_OUTLINE5] = "pd_outline_h5",
    [PREVIEW_STYLE_OUTLINE6] = "pd_outline_h6",
};

/* Palette colours on the preview background */
static void ensure_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    if (gtk_text_tag_table_lookup(table, "pd_title") != NULL) {
        return;
    }
    gtk_text_buffer_create_tag(buffer, "pd_title", "weight", 700, "scale", FONT_SCALE_TITLE, NULL);
    text_tag_create(buffer, "pd_dim", "foreground", "@fg_subtle", NULL);
    gtk_text_buffer_create_tag(buffer, "pd_section", "weight", 700, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_bold", "weight", 700, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_italic", "style", PANGO_STYLE_ITALIC, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_underline", "underline", PANGO_UNDERLINE_SINGLE, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_strike", "strikethrough", TRUE, NULL);
    text_tag_create(buffer, "pd_header", "weight", 700, "foreground", "@fg_strong", NULL);
    text_tag_create(buffer, "pd_row_alt", "paragraph-background", "@bg_block", NULL);
    text_tag_create(buffer, "pd_grid", "foreground", "@rule", NULL);
    text_tag_create(buffer, "pd_key", "foreground", "@syn_variable", NULL);
    text_tag_create(buffer, "pd_string", "foreground", "@syn_string", NULL);
    text_tag_create(buffer, "pd_number", "foreground", "@syn_number", NULL);
    text_tag_create(buffer, "pd_keyword", "foreground", "@syn_keyword", NULL);
    text_tag_create(buffer, "pd_comment", "foreground", "@syn_comment", NULL);
    text_tag_create(buffer, "pd_added", "foreground", "@diff_added_fg",
                               "paragraph-background", "@diff_added_bg", NULL);
    text_tag_create(buffer, "pd_removed", "foreground", "@diff_removed_fg",
                               "paragraph-background", "@diff_removed_bg", NULL);
    text_tag_create(buffer, "pd_hunk", "foreground", "@diff_hunk_fg",
                               "paragraph-background", "@diff_hunk_bg", NULL);
    text_tag_create(buffer, "pd_file", "foreground", "@syn_function", "weight", 700, NULL);
    text_tag_create(buffer, "pd_error", "foreground", "@error", NULL);
    text_tag_create(buffer, "pd_warning", "foreground", "@warning", NULL);
    text_tag_create(buffer, "pd_info", "foreground", "@link", NULL);
    text_tag_create(buffer, "pd_success", "foreground", "@success", NULL);
    text_tag_create(buffer, "pd_link", "foreground", "@link",
                               "underline", PANGO_UNDERLINE_SINGLE, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_h1", "weight", 700, "scale", FONT_SCALE_H1,
                               "pixels-above-lines", 8, "pixels-below-lines", 4, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_h2", "weight", 700, "scale", FONT_SCALE_H2,
                               "pixels-above-lines", 6, "pixels-below-lines", 3, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_h3", "weight", 700, "scale", FONT_SCALE_H3,
                               "pixels-above-lines", 4, "pixels-below-lines", 2, NULL);
    /* Outline levels: no look, read by the tagbar (preview_tagbar.h) */
    for (gint i = PREVIEW_STYLE_OUTLINE1; i <= PREVIEW_STYLE_OUTLINE6; i++) {
        gtk_text_buffer_create_tag(buffer, STYLE_TAGS[i], NULL);
    }
    text_tag_create(buffer, "pd_code", "family", "@font_code",
                               "paragraph-background", "@bg_block", NULL);
    text_tag_create(buffer, "pd_mono", "family", "@font_code", NULL);
    /* Indents in addition to the view's own left margin (text padding) */
    gtk_text_buffer_create_tag(buffer, "pd_indent", "left-margin", PREVIEW_TEXT_PADDING_X + 56, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_indent2", "left-margin", PREVIEW_TEXT_PADDING_X + 112, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_indent3", "left-margin", PREVIEW_TEXT_PADDING_X + 168, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_sup", "rise", 4 * PANGO_SCALE, "scale", 0.75, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_sub", "rise", -2 * PANGO_SCALE, "scale", 0.75, NULL);
    text_tag_create(buffer, "pd_table", "family", "@font_code", NULL);
    g_object_set(gtk_text_tag_table_lookup(table, "pd_table"), "wrap-mode", GTK_WRAP_NONE, NULL);
}

static guint32 palette_value(PaletteRole role) {
    guint32 rgb = 0;
    preview_color_parse(palette_color(role), &rgb);
    return rgb;
}

/* Tag of a span with an argument (fonts, document colours, paragraph layout),
 * NULL when the argument is malformed. Colours depend on the theme: the
 * view's background is part of their tag name. */
static GtkTextTag* arg_tag(GtkTextBuffer* buffer, PreviewStyle style, const gchar* arg) {
    static const gchar* const PREFIX[PREVIEW_STYLE_COUNT] = {
        [PREVIEW_STYLE_FONT] = "pd_font", [PREVIEW_STYLE_FAMILY] = "pd_family",
        [PREVIEW_STYLE_SCALE] = "pd_scale", [PREVIEW_STYLE_COLOR] = "pd_color",
        [PREVIEW_STYLE_HIGHLIGHT] = "pd_highlight", [PREVIEW_STYLE_PARAGRAPH] = "pd_para",
    };
    if (PREFIX[style] == NULL || arg == NULL) {
        return NULL;
    }
    const gchar* bg = palette_color(PAL_BG_VIEW);
    gchar* name = g_strdup_printf("%s:%s:%s", PREFIX[style], arg, bg);
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag* tag = gtk_text_tag_table_lookup(table, name);
    if (tag != NULL) {
        g_free(name);
        return tag;
    }
    guint32 rgb = 0;
    PreviewParagraph para;
    switch (style) {
    case PREVIEW_STYLE_FONT:
        tag = gtk_text_buffer_create_tag(buffer, name, "font", arg, NULL);
        break;
    case PREVIEW_STYLE_FAMILY:
        tag = gtk_text_buffer_create_tag(buffer, name, NULL);
        g_object_set(tag, "family", arg, NULL);
        break;
    case PREVIEW_STYLE_SCALE: {
        gdouble scale = g_ascii_strtod(arg, NULL);
        if (scale > 0.2 && scale < 10.0) {
            tag = gtk_text_buffer_create_tag(buffer, name, "scale", scale, NULL);
        }
        break;
    }
    case PREVIEW_STYLE_COLOR:
        if (preview_color_parse(arg, &rgb)) {
            guint32 shown = preview_color_readable(rgb, palette_value(PAL_BG_VIEW), palette_value(PAL_FG_TEXT));
            gchar* color = preview_color_format(shown);
            tag = gtk_text_buffer_create_tag(buffer, name, "foreground", color, NULL);
            g_free(color);
        }
        break;
    case PREVIEW_STYLE_HIGHLIGHT:
        if (preview_color_parse(arg, &rgb)) {
            /* The theme's text or background colour, whichever stands out */
            guint32 fg = palette_value(PAL_FG_TEXT), bgv = palette_value(PAL_BG_VIEW);
            gchar* back = preview_color_format(rgb);
            gchar* fore = preview_color_format(preview_color_contrast(rgb, fg) >= preview_color_contrast(rgb, bgv)
                                                   ? fg : bgv);
            tag = gtk_text_buffer_create_tag(buffer, name, "background", back, "foreground", fore, NULL);
            g_free(back);
            g_free(fore);
        }
        break;
    case PREVIEW_STYLE_PARAGRAPH:
        if (preview_paragraph_parse(arg, &para)) {
            static const GtkJustification JUSTIFY[] = { GTK_JUSTIFY_LEFT, GTK_JUSTIFY_CENTER, GTK_JUSTIFY_RIGHT,
                                                        GTK_JUSTIFY_FILL };
            tag = gtk_text_buffer_create_tag(buffer, name, "justification", JUSTIFY[para.align],
                                             "left-margin", PREVIEW_TEXT_PADDING_X + MAX(para.left, 0),
                                             "right-margin",
                                             PREVIEW_TEXT_PADDING_X + TEXT_SCROLLBAR_CLEARANCE + MAX(para.right, 0),
                                             "indent", para.indent, "pixels-above-lines", MAX(para.above, 0),
                                             "pixels-below-lines", MAX(para.below, 0), NULL);
        }
        break;
    default:
        break;
    }
    g_free(name);
    return tag;
}

/* Make the fonts of font files usable by name (once per file) */
static void load_font_files(GtkWidget* view, const GPtrArray* files) {
    if (files == NULL) {
        return;
    }
#if PANGO_VERSION_CHECK(1, 56, 0)
    static GHashTable* loaded = NULL;
    if (loaded == NULL) {
        loaded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
    PangoFontMap* map = pango_context_get_font_map(gtk_widget_get_pango_context(view));
    for (guint i = 0; i < files->len; i++) {
        const gchar* path = g_ptr_array_index(files, i);
        if (!g_hash_table_contains(loaded, path) && pango_font_map_add_font_file(map, path, NULL)) {
            g_hash_table_add(loaded, g_strdup(path));
        }
    }
#else
    (void)view;
    (void)files;
#endif
}

/* Decode an image scaled down to max_width (NULL if it cannot be decoded) */
static GdkTexture* decode_image(GBytes* data, gint max_width) {
    gsize len = 0;
    const guchar* bytes = g_bytes_get_data(data, &len);
    GdkPixbuf* pix = pixbuf_from_bytes(bytes, len, max_width);
    if (pix == NULL) {
        return NULL;
    }
    GdkTexture* texture = texture_new_for_pixbuf(pix);
    g_object_unref(pix);
    return texture;
}

/* Insert the document; images are never wider than max_image_width (0: no limit) */
static void insert_doc(GtkTextBuffer* buffer, const PreviewDoc* doc, gint max_image_width) {
    ensure_tags(buffer);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gint base = gtk_text_iter_get_offset(&end);
    gtk_text_buffer_insert(buffer, &end, doc->text->str, (gint)doc->text->len);

    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* span = &g_array_index(doc->spans, PreviewSpan, i);
        GtkTextIter a, b;
        gtk_text_buffer_get_iter_at_offset(buffer, &a, base + span->start);
        gtk_text_buffer_get_iter_at_offset(buffer, &b, base + span->end);
        if (span->arg != NULL) {
            GtkTextTag* tag = arg_tag(buffer, span->style, span->arg);
            if (tag != NULL) {
                gtk_text_buffer_apply_tag(buffer, tag, &a, &b);
            }
        } else if (STYLE_TAGS[span->style] != NULL) {
            gtk_text_buffer_apply_tag_by_name(buffer, STYLE_TAGS[span->style], &a, &b);
        }
    }

    /* Images replace their one-character placeholder: offsets do not move */
    for (guint i = 0; i < doc->images->len; i++) {
        const PreviewImage* image = &g_array_index(doc->images, PreviewImage, i);
        gint width = image->max_width;
        if (max_image_width > 0 && (width <= 0 || width > max_image_width)) {
            width = max_image_width;
        }
        GdkTexture* texture = decode_image(image->data, width);
        if (texture == NULL) {
            continue;
        }
        GtkTextIter a, b;
        gtk_text_buffer_get_iter_at_offset(buffer, &a, base + image->offset);
        b = a;
        gtk_text_iter_forward_char(&b);
        gtk_text_buffer_delete(buffer, &a, &b);
        gtk_text_buffer_insert_paintable(buffer, &a, GDK_PAINTABLE(texture));
        g_object_unref(texture);
    }
}

void preview_doc_insert(GtkTextBuffer* buffer, const PreviewDoc* doc) {
    insert_doc(buffer, doc, 0);
}

void preview_doc_show(GtkTextView* view, const PreviewDoc* doc, const gchar* path,
                      const gchar* name, gint64 size) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);
    g_object_set_data(G_OBJECT(buffer), "minimap_paged",
                      (doc != NULL && doc->paged) ? GINT_TO_POINTER(1) : NULL);
    GtkWidget* widget = GTK_WIDGET(view);
    set_view_text_padding(widget);
    if (doc->format == PREVIEW_DOC_HTML) {
        set_view_monospace(widget, FALSE);
        gtk_text_view_set_wrap_mode(view, GTK_WRAP_WORD_CHAR);
        ensure_html_tags(buffer);
        render_html_to_buffer(buffer, view, doc->text->str, path, FALSE, name, size);
        return;
    }
    if (doc->format == PREVIEW_DOC_MARKDOWN) {
        set_view_monospace(widget, FALSE);
        gtk_text_view_set_wrap_mode(view, GTK_WRAP_WORD_CHAR);
        ensure_markdown_tags(buffer);
        render_markdown_to_buffer(buffer, view, doc->text->str, path, FALSE, name, size);
        return;
    }
    set_view_monospace(widget, doc->monospace);
    gtk_text_view_set_wrap_mode(view, doc->wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
    load_font_files(widget, doc->font_files);
    gtk_text_buffer_set_text(buffer, "", 0);
    /* Images fit in the view as it is now (slides, document pictures) */
    gint avail = gtk_widget_get_width(widget) - 2 * PREVIEW_TEXT_PADDING_X - TEXT_SCROLLBAR_CLEARANCE;
    insert_doc(buffer, doc, avail >= 160 ? avail : 0);
    if (doc->scroll_to_end) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_place_cursor(buffer, &end);
        GtkTextMark* mark = gtk_text_buffer_get_insert(buffer);
        gtk_text_view_scroll_to_mark(view, mark, 0.0, TRUE, 0.0, 1.0);
    }
}
