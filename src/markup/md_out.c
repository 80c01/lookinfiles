/* Output of the Markdown parser (see md_out.h). */
#include "markup/md_out.h"

#include <string.h>

static void object_clear(gpointer data) {
    MdObject* o = data;
    g_free(o->name);
    g_free(o->text);
    g_clear_object(&o->image);
    if (o->rows != NULL) {
        g_ptr_array_unref(o->rows);
    }
    if (o->aligns != NULL) {
        g_ptr_array_unref(o->aligns);
    }
}

static void link_clear(gpointer data) {
    g_free(((MdLinkSpan*)data)->url);
}

MdOut* md_out_new(void) {
    MdOut* out = g_new0(MdOut, 1);
    out->text = g_string_new(NULL);
    out->spans = g_array_new(FALSE, FALSE, sizeof(MdSpan));
    out->links = g_array_new(FALSE, FALSE, sizeof(MdLinkSpan));
    g_array_set_clear_func(out->links, link_clear);
    out->objects = g_array_new(FALSE, TRUE, sizeof(MdObject));
    g_array_set_clear_func(out->objects, object_clear);
    return out;
}

void md_out_free(MdOut* out) {
    if (out == NULL) {
        return;
    }
    g_string_free(out->text, TRUE);
    g_array_unref(out->spans);
    g_array_unref(out->links);
    g_array_unref(out->objects);
    g_free(out);
}

gint md_out_offset(const MdOut* out) {
    return out->n_chars;
}

void md_out_append(MdOut* out, const gchar* text, gssize len) {
    if (text == NULL) {
        return;
    }
    gsize n = (len < 0) ? strlen(text) : (gsize)len;
    g_string_append_len(out->text, text, (gssize)n);
    out->n_chars += (gint)g_utf8_strlen(text, (gssize)n);
}

void md_out_append_tagged(MdOut* out, const gchar* text, const gchar* tag) {
    gint start = out->n_chars;
    md_out_append(out, text, -1);
    md_out_tag_from(out, start, tag);
}

void md_out_tag_from(MdOut* out, gint start, const gchar* tag) {
    if (tag == NULL || start >= out->n_chars) {
        return;
    }
    MdSpan span = {start, out->n_chars, g_intern_string(tag)};
    g_array_append_val(out->spans, span);
}

void md_out_link_from(MdOut* out, gint start, const gchar* url) {
    if (url == NULL || start >= out->n_chars) {
        return;
    }
    MdLinkSpan link = {start, out->n_chars, g_strdup(url)};
    g_array_append_val(out->links, link);
}

static MdObject* add_object(MdOut* out, MdObjectKind kind, gboolean in_text) {
    MdObject o = {0};
    o.kind = kind;
    o.offset = out->n_chars;
    g_array_append_val(out->objects, o);
    if (in_text) {
        md_out_append(out, MD_OUT_OBJECT_CHAR, -1);
    }
    return &g_array_index(out->objects, MdObject, out->objects->len - 1);
}

void md_out_add_image(MdOut* out, GObject* image) {
    add_object(out, MD_OBJECT_IMAGE, TRUE)->image = g_object_ref(image);
}

void md_out_add_glyph(MdOut* out, const gchar* glyph, const gchar* tag) {
    gint start = out->n_chars;
    add_object(out, MD_OBJECT_GLYPH, TRUE)->name = g_strdup(glyph);
    md_out_append(out, " ", 1);
    md_out_tag_from(out, start, tag);
}

void md_out_add_table(MdOut* out, GPtrArray* rows, GPtrArray* aligns, guint cols) {
    MdObject* o = add_object(out, MD_OBJECT_TABLE, TRUE);
    o->rows = rows;
    o->aligns = aligns;
    o->cols = cols;
    md_out_append(out, "\n", 1);
}

void md_out_add_code(MdOut* out, gint start, const gchar* code, const gchar* info) {
    MdObject o = {0};
    o.kind = MD_OBJECT_CODE;
    o.offset = start;
    o.name = g_strdup(info);
    o.text = g_strdup(code);
    g_array_append_val(out->objects, o);
}
