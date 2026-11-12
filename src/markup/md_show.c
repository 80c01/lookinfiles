/* Rich text parsed without widgets, shown in a text buffer (see md_show.h). */
#include "markup/md_show.h"
#include "markup/markdown_link.h"
#include "markup/markdown_table.h"
#include "markup/math_paintable.h"
#include "media/image_utils.h"
#include "theme/fonts.h"
#include "theme/icons.h"

#ifdef HAVE_GSV
#include <gtksourceview/gtksource.h>
#endif

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

/* A formula at at (moved after it), lowered so that its baseline meets the text's */
static void insert_math(GtkTextBuffer* buffer, GtkTextView* view, GtkTextIter* at, const MdObject* o) {
    PangoContext* pc = view != NULL ? gtk_widget_get_pango_context(GTK_WIDGET(view)) : NULL;
    gint descent = 0;
    GdkPaintable* math = math_paintable_new(pc, o->text, o->name != NULL, fonts_size(FONT_UI), &descent);
    gint offset = gtk_text_iter_get_offset(at);
    gtk_text_buffer_insert_paintable(buffer, at, math);
    g_object_unref(math);
    if (descent <= 0) return;
    gchar name[32];
    g_snprintf(name, sizeof name, "md_math_rise_%d", descent);
    GtkTextTag* tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name);
    if (tag == NULL) tag = gtk_text_buffer_create_tag(buffer, name, "rise", -descent * PANGO_SCALE, NULL);
    GtkTextIter start;
    gtk_text_buffer_get_iter_at_offset(buffer, &start, offset);
    gtk_text_buffer_apply_tag(buffer, tag, &start, at);
}

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
    case MD_OBJECT_MATH:
        insert_math(buffer, view, &at, o);
        break;
    case MD_OBJECT_CODE:
        break;
    }
}

void md_out_show(GtkTextBuffer* buffer, GtkTextView* view, const MdOut* md) {
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

