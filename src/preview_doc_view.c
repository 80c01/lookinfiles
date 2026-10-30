/* Display of PreviewDocs in the preview text view (see preview_doc_view.h). */
#include "preview_doc_view.h"
#include "fonts.h"
#include "text_tags.h"

#include <string.h>

#include "html.h"
#include "image_utils.h"
#include "markdown.h"
#include "text_style.h"

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
    text_tag_create(buffer, "pd_code", "family", "@font_code",
                               "paragraph-background", "@bg_block", NULL);
    text_tag_create(buffer, "pd_mono", "family", "@font_code", NULL);
    /* Indents in addition to the view's own left margin (text padding) */
    gtk_text_buffer_create_tag(buffer, "pd_indent", "left-margin", PREVIEW_TEXT_PADDING_X + 56, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_indent2", "left-margin", PREVIEW_TEXT_PADDING_X + 112, NULL);
    gtk_text_buffer_create_tag(buffer, "pd_indent3", "left-margin", PREVIEW_TEXT_PADDING_X + 168, NULL);
}

/* Tag of a font span: a Pango font description */
static GtkTextTag* font_tag(GtkTextBuffer* buffer, const gchar* description) {
    gchar* name = g_strdup_printf("pd_font:%s", description);
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag* tag = gtk_text_tag_table_lookup(table, name);
    if (tag == NULL) {
        tag = gtk_text_buffer_create_tag(buffer, name, "font", description, NULL);
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

void preview_doc_insert(GtkTextBuffer* buffer, const PreviewDoc* doc) {
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
        if (span->style == PREVIEW_STYLE_FONT) {
            if (span->arg != NULL) {
                gtk_text_buffer_apply_tag(buffer, font_tag(buffer, span->arg), &a, &b);
            }
        } else if (STYLE_TAGS[span->style] != NULL) {
            gtk_text_buffer_apply_tag_by_name(buffer, STYLE_TAGS[span->style], &a, &b);
        }
    }

    /* Images replace their one-character placeholder: offsets do not move */
    for (guint i = 0; i < doc->images->len; i++) {
        const PreviewImage* image = &g_array_index(doc->images, PreviewImage, i);
        GdkTexture* texture = decode_image(image->data, image->max_width);
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

void preview_doc_show(GtkTextView* view, const PreviewDoc* doc, const gchar* path,
                      const gchar* name, gint64 size) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);
    GtkWidget* widget = GTK_WIDGET(view);
    set_view_text_padding(widget);
    if (doc->format == PREVIEW_DOC_HTML) {
        set_view_monospace(widget, FALSE);
        gtk_text_view_set_wrap_mode(view, GTK_WRAP_WORD_CHAR);
        ensure_html_tags(buffer);
        render_html_to_buffer(buffer, doc->text->str, path, FALSE, name, size);
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
    preview_doc_insert(buffer, doc);
    if (doc->scroll_to_end) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_place_cursor(buffer, &end);
        GtkTextMark* mark = gtk_text_buffer_get_insert(buffer);
        gtk_text_view_scroll_to_mark(view, mark, 0.0, TRUE, 0.0, 1.0);
    }
}
