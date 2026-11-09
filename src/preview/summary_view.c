/* Draws the summary of a row without file content (see summary_view.h). */
#include "preview/summary_view.h"
#include "theme/fonts.h"
#include "theme/icons.h"
#include "theme/text_tags.h"

#define COLUMN_GAP "    " /* space between two aligned columns */
#define INDENT "  "       /* items and keys under their heading */

static void ensure_tags(GtkTextBuffer* buffer) {
    GtkTextTagTable* tt = gtk_text_buffer_get_tag_table(buffer);
    if (gtk_text_tag_table_lookup(tt, "sum_title") != NULL) {
        return;
    }
    /* Same look as the directory preview header and sections */
    text_tag_create(buffer, "sum_title",
                    "weight", 600, "scale", FONT_SCALE_TITLE, "foreground", "@fg_strong",
                    "pixels-below-lines", 4, NULL);
    text_tag_create(buffer, "sum_dim", "foreground", "@fg_subtle", NULL);
    text_tag_create(buffer, "sum_section",
                    "weight", 600, "foreground", "@fg_strong",
                    "paragraph-background", "@bg_block",
                    "pixels-above-lines", 6, "pixels-below-lines", 6, NULL);
    text_tag_create(buffer, "sum_key", "weight", 600, "foreground", "@fg_strong", NULL);
    text_tag_create(buffer, "sum_row", "pixels-above-lines", 2, "pixels-below-lines", 2, NULL);
    /* Their tab stops are set for each summary */
    gtk_text_buffer_create_tag(buffer, "sum_tabs_fields", NULL);
    gtk_text_buffer_create_tag(buffer, "sum_tabs_items", NULL);
    gtk_text_buffer_create_tag(buffer, "sum_tabs_keys", NULL);
}

/* Width in pixels of text in the view's font (bold if asked) */
static gint text_width(GtkWidget* view, const gchar* text, gboolean bold) {
    PangoLayout* layout = gtk_widget_create_pango_layout(view, text);
    if (bold) {
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD));
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
    }
    gint width = 0;
    pango_layout_get_pixel_size(layout, &width, NULL);
    g_object_unref(layout);
    return width;
}

/* Width of an icon inserted before a text (the icon and its space) */
static gint icon_width(GtkWidget* view) {
    GdkPaintable* icon = glyph_paintable("folder");
    gint width = gdk_paintable_get_intrinsic_width(icon) + text_width(view, " ", FALSE);
    g_object_unref(icon);
    return width;
}

/* One tab stop at x pixels on the paragraphs of tag */
static void set_tab(GtkTextBuffer* buffer, const gchar* tag, gint x) {
    GtkTextTag* t = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), tag);
    PangoTabArray* tabs = pango_tab_array_new_with_positions(1, TRUE, PANGO_TAB_LEFT, x);
    g_object_set(t, "tabs", tabs, NULL);
    pango_tab_array_free(tabs);
}

/* Tab stops of the label / value, name / detail and key / action columns,
 * from the widest entry of each kind */
static void set_columns(GtkWidget* view, GtkTextBuffer* buffer, GPtrArray* lines) {
    gint gap = text_width(view, COLUMN_GAP, FALSE);
    gint indent = text_width(view, INDENT, FALSE);
    gint icon = icon_width(view);
    gint fields = 0, items = 0, keys = 0;
    for (guint i = 0; i < lines->len; i++) {
        const PreviewSummaryLine* line = g_ptr_array_index(lines, i);
        switch (line->kind) {
        case PREVIEW_SUMMARY_FIELD:
            fields = MAX(fields, text_width(view, line->text, FALSE));
            break;
        case PREVIEW_SUMMARY_ITEM: {
            /* Very long names are not used to place the column */
            gchar* name = g_utf8_substring(line->text, 0, PREVIEW_SUMMARY_NAME_MAX);
            items = MAX(items, text_width(view, name, FALSE));
            g_free(name);
            break;
        }
        case PREVIEW_SUMMARY_KEY:
            keys = MAX(keys, text_width(view, line->text, TRUE));
            break;
        default:
            break;
        }
    }
    set_tab(buffer, "sum_tabs_fields", fields + gap);
    set_tab(buffer, "sum_tabs_items", indent + icon + items + gap);
    set_tab(buffer, "sum_tabs_keys", indent + keys + gap);
}

static void insert(GtkTextBuffer* buffer, GtkTextIter* it, const gchar* text,
                   const gchar* tag1, const gchar* tag2, const gchar* tag3) {
    gtk_text_buffer_insert_with_tags_by_name(buffer, it, text, -1, tag1, tag2, tag3, NULL);
}

static void insert_icon(GtkTextBuffer* buffer, GtkTextIter* it, const PreviewSummaryLine* line,
                        const gchar* tag) {
    if (line->glyph != NULL) {
        text_insert_glyph(buffer, it, line->glyph, tag);
    } else if (line->file_icon != NULL) {
        text_insert_file_icon(buffer, it, line->file_icon, FALSE, tag);
    }
}

void summary_view_render(GtkWidget* view, GPtrArray* lines) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, "", 0);
    if (lines == NULL) {
        return;
    }
    ensure_tags(buffer);
    set_columns(view, buffer, lines);

    GtkTextIter it;
    gtk_text_buffer_get_start_iter(buffer, &it);
    PreviewSummaryKind previous = PREVIEW_SUMMARY_TITLE;
    for (guint i = 0; i < lines->len; i++) {
        const PreviewSummaryLine* line = g_ptr_array_index(lines, i);
        /* A blank line closes the header (title and fields) */
        if (i > 0 && previous == PREVIEW_SUMMARY_FIELD && line->kind != PREVIEW_SUMMARY_FIELD) {
            insert(buffer, &it, "\n", NULL, NULL, NULL);
        }
        switch (line->kind) {
        case PREVIEW_SUMMARY_TITLE:
            insert_icon(buffer, &it, line, "sum_title");
            insert(buffer, &it, line->text, "sum_title", NULL, NULL);
            insert(buffer, &it, "\n", "sum_title", NULL, NULL);
            break;
        case PREVIEW_SUMMARY_FIELD:
            insert(buffer, &it, line->text, "sum_dim", "sum_tabs_fields", NULL);
            insert(buffer, &it, "\t", "sum_tabs_fields", NULL, NULL);
            insert(buffer, &it, line->detail != NULL ? line->detail : "", "sum_tabs_fields", NULL, NULL);
            insert(buffer, &it, "\n", "sum_tabs_fields", NULL, NULL);
            break;
        case PREVIEW_SUMMARY_HEADING:
            if (previous != PREVIEW_SUMMARY_FIELD && previous != PREVIEW_SUMMARY_TITLE) {
                insert(buffer, &it, "\n", NULL, NULL, NULL);
            }
            insert(buffer, &it, line->text, "sum_section", NULL, NULL);
            insert(buffer, &it, "\n", "sum_section", NULL, NULL);
            break;
        case PREVIEW_SUMMARY_ITEM:
            insert(buffer, &it, INDENT, "sum_tabs_items", "sum_row", NULL);
            insert_icon(buffer, &it, line, "sum_tabs_items");
            insert(buffer, &it, line->text, "sum_tabs_items", "sum_row", NULL);
            if (line->detail != NULL) {
                insert(buffer, &it, "\t", "sum_tabs_items", "sum_row", NULL);
                insert(buffer, &it, line->detail, "sum_tabs_items", "sum_row", "sum_dim");
            }
            insert(buffer, &it, "\n", "sum_tabs_items", "sum_row", NULL);
            break;
        case PREVIEW_SUMMARY_KEY:
            insert(buffer, &it, INDENT, "sum_tabs_keys", "sum_row", NULL);
            insert(buffer, &it, line->text, "sum_tabs_keys", "sum_row", "sum_key");
            insert(buffer, &it, "\t", "sum_tabs_keys", "sum_row", NULL);
            insert(buffer, &it, line->detail != NULL ? line->detail : "", "sum_tabs_keys", "sum_row", NULL);
            insert(buffer, &it, "\n", "sum_tabs_keys", "sum_row", NULL);
            break;
        case PREVIEW_SUMMARY_NOTE:
            insert(buffer, &it, INDENT, "sum_dim", NULL, NULL);
            insert(buffer, &it, line->text, "sum_dim", NULL, NULL);
            insert(buffer, &it, "\n", "sum_dim", NULL, NULL);
            break;
        case PREVIEW_SUMMARY_TEXT:
            insert(buffer, &it, line->text, NULL, NULL, NULL);
            break;
        }
        previous = line->kind;
    }
}
