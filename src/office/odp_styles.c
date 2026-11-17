/* OpenDocument drawing trees and styles (see odp_styles.h) */
#include "office/odp_styles.h"

#include <gio/gio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "formats/zip_xml.h"

#define MAX_DEPTH 256
#define MAX_PARENTS 16

/* ---- Tree ---- */

static OdpNode* node_new(const gchar* name) {
    OdpNode* n = g_new0(OdpNode, 1);
    n->name = g_strdup(name);
    n->children = g_ptr_array_new_with_free_func((GDestroyNotify)odp_node_free);
    return n;
}

void odp_node_free(OdpNode* node) {
    if (node == NULL) {
        return;
    }
    g_free(node->name);
    g_strfreev(node->attr_names);
    g_strfreev(node->attr_values);
    g_free(node->text);
    g_ptr_array_free(node->children, TRUE);
    g_free(node);
}

typedef struct {
    GPtrArray* stack;   /* OdpNode*, the root first */
} TreeParse;

static void tree_start(GMarkupParseContext* ctx, const gchar* name, const gchar** names, const gchar** values,
                       gpointer data, GError** error) {
    (void)ctx;
    TreeParse* t = data;
    if (t->stack->len >= MAX_DEPTH) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, "document nested too deeply");
        return;
    }
    OdpNode* n = node_new(name);
    n->attr_names = g_strdupv((gchar**)names);
    n->attr_values = g_strdupv((gchar**)values);
    if (t->stack->len > 0) {
        g_ptr_array_add(((OdpNode*)g_ptr_array_index(t->stack, t->stack->len - 1))->children, n);
    }
    g_ptr_array_add(t->stack, n);
}

static void tree_end(GMarkupParseContext* ctx, const gchar* name, gpointer data, GError** error) {
    (void)ctx;
    (void)name;
    (void)error;
    TreeParse* t = data;
    /* The root stays at the bottom of the stack */
    if (t->stack->len > 1) {
        g_ptr_array_set_size(t->stack, t->stack->len - 1);
    }
}

static void tree_text(GMarkupParseContext* ctx, const gchar* text, gsize len, gpointer data, GError** error) {
    (void)ctx;
    (void)error;
    TreeParse* t = data;
    if (t->stack->len == 0) {
        return;
    }
    OdpNode* parent = g_ptr_array_index(t->stack, t->stack->len - 1);
    /* Whitespace between elements only matters inside text */
    gboolean blank = TRUE;
    for (gsize i = 0; i < len && blank; i++) {
        blank = g_ascii_isspace(text[i]);
    }
    if (blank && !g_str_has_prefix(parent->name, "text:")) {
        return;
    }
    OdpNode* last = parent->children->len > 0 ? g_ptr_array_index(parent->children, parent->children->len - 1) : NULL;
    if (last != NULL && last->name == NULL) {
        gchar* joined = g_strdup_printf("%s%.*s", last->text, (int)len, text);
        g_free(last->text);
        last->text = joined;
        return;
    }
    OdpNode* n = node_new(NULL);
    n->text = g_strndup(text, len);
    g_ptr_array_add(parent->children, n);
}

static const GMarkupParser tree_parser = { tree_start, tree_end, tree_text, NULL, NULL };

static OdpNode* tree_result(TreeParse* t, gboolean ok) {
    OdpNode* root = t->stack->len > 0 ? g_ptr_array_index(t->stack, 0) : NULL;
    g_ptr_array_free(t->stack, TRUE);
    if (!ok) {
        odp_node_free(root);
        return NULL;
    }
    return root;
}

OdpNode* odp_node_parse(ZipReader* zip, const gchar* part, gsize max_size, gboolean optional, GError** error) {
    if (optional && !zip_reader_has(zip, part)) {
        return NULL;
    }
    TreeParse t = { g_ptr_array_new() };
    gboolean ok = zip_xml_parse(zip, part, max_size, optional, &tree_parser, &t, error);
    if (ok && t.stack->len == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s is empty", part);
        ok = FALSE;
    }
    return tree_result(&t, ok);
}

OdpNode* odp_node_parse_text(const gchar* xml, gssize len, GError** error) {
    TreeParse t = { g_ptr_array_new() };
    GMarkupParseContext* ctx = g_markup_parse_context_new(&tree_parser, 0, &t, NULL);
    gboolean ok = g_markup_parse_context_parse(ctx, xml, len, error) && g_markup_parse_context_end_parse(ctx, error);
    g_markup_parse_context_free(ctx);
    if (ok && t.stack->len == 0) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY, "empty document");
        ok = FALSE;
    }
    return tree_result(&t, ok);
}

const gchar* odp_node_attr(const OdpNode* node, const gchar* name) {
    if (node == NULL || node->attr_names == NULL) {
        return NULL;
    }
    for (gint i = 0; node->attr_names[i] != NULL; i++) {
        if (strcmp(node->attr_names[i], name) == 0) {
            return node->attr_values[i];
        }
    }
    return NULL;
}

gboolean odp_node_is(const OdpNode* node, const gchar* name) {
    return node != NULL && node->name != NULL && strcmp(node->name, name) == 0;
}

OdpNode* odp_node_child(const OdpNode* node, const gchar* name) {
    if (node == NULL) {
        return NULL;
    }
    for (guint i = 0; i < node->children->len; i++) {
        OdpNode* c = g_ptr_array_index(node->children, i);
        if (odp_node_is(c, name)) {
            return c;
        }
    }
    return NULL;
}

OdpNode* odp_node_find(const OdpNode* node, const gchar* name) {
    if (node == NULL) {
        return NULL;
    }
    for (guint i = 0; i < node->children->len; i++) {
        OdpNode* c = g_ptr_array_index(node->children, i);
        if (odp_node_is(c, name)) {
            return c;
        }
        OdpNode* found = odp_node_find(c, name);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

static void append_text(GString* out, const OdpNode* node) {
    if (node->name == NULL) {
        g_string_append(out, node->text);
        return;
    }
    gboolean block = odp_node_is(node, "text:p") || odp_node_is(node, "text:h");
    if (block && out->len > 0 && out->str[out->len - 1] != '\n') {
        g_string_append_c(out, '\n');
    }
    if (odp_node_is(node, "text:s")) {
        g_string_append_c(out, ' ');
    } else if (odp_node_is(node, "text:tab")) {
        g_string_append_c(out, '\t');
    } else if (odp_node_is(node, "text:line-break")) {
        g_string_append_c(out, '\n');
    }
    for (guint i = 0; i < node->children->len; i++) {
        append_text(out, g_ptr_array_index(node->children, i));
    }
}

gchar* odp_node_text(const OdpNode* node) {
    GString* out = g_string_new(NULL);
    if (node != NULL) {
        append_text(out, node);
    }
    return g_strstrip(g_string_free(out, FALSE));
}

/* ---- Values ---- */

gdouble odp_length(const gchar* value, gdouble fallback) {
    if (value == NULL) {
        return fallback;
    }
    gchar* end = NULL;
    gdouble v = g_ascii_strtod(value, &end);
    if (end == value || !isfinite(v)) {
        return fallback;
    }
    while (*end == ' ') end++;
    if (g_str_has_prefix(end, "cm")) return v * 72.0 / 2.54;
    if (g_str_has_prefix(end, "mm")) return v * 72.0 / 25.4;
    if (g_str_has_prefix(end, "in")) return v * 72.0;
    if (g_str_has_prefix(end, "pt")) return v;
    if (g_str_has_prefix(end, "pc")) return v * 12.0;
    if (g_str_has_prefix(end, "px")) return v * 0.75;
    if (*end == '\0') return v;   /* no unit: points */
    return fallback;
}

guint32 odp_color(const gchar* value, guint32 fallback) {
    if (value == NULL || value[0] != '#' || strlen(value) != 7) {
        return fallback;
    }
    gchar* end = NULL;
    guint64 v = g_ascii_strtoull(value + 1, &end, 16);
    return end != NULL && *end == '\0' ? (guint32)v : fallback;
}

gdouble odp_percent(const gchar* value) {
    if (value == NULL) {
        return -1;
    }
    gchar* end = NULL;
    gdouble v = g_ascii_strtod(value, &end);
    return end != value && end != NULL && *end == '%' ? v / 100.0 : -1;
}

gchar* odp_first_family(const gchar* families) {
    if (families == NULL) {
        return NULL;
    }
    const gchar* comma = strchr(families, ',');
    gchar* first = comma != NULL ? g_strndup(families, (gsize)(comma - families)) : g_strdup(families);
    g_strstrip(first);
    gsize len = strlen(first);
    if (len >= 2 && (first[0] == '\'' || first[0] == '"') && first[len - 1] == first[0]) {
        memmove(first, first + 1, len - 2);
        first[len - 2] = '\0';
    }
    if (first[0] == '\0') {
        g_free(first);
        return NULL;
    }
    return first;
}

/* ---- Styles ---- */

struct OdpStyle {
    gchar* parent;
    gchar* family;
    GHashTable* props;      /* qualified attribute -> value */
    gchar* list;            /* name of a list style inside the style */
    gboolean is_default;
};

static void style_free(gpointer data) {
    OdpStyle* s = data;
    g_free(s->parent);
    g_free(s->family);
    g_free(s->list);
    g_hash_table_destroy(s->props);
    g_free(s);
}

static void list_free(gpointer data) {
    OdpList* l = data;
    for (gint i = 0; i < ODP_LIST_LEVELS; i++) {
        g_free(l->levels[i].bullet);
        g_free(l->levels[i].num_format);
        g_free(l->levels[i].num_prefix);
        g_free(l->levels[i].num_suffix);
        g_free(l->levels[i].font);
    }
    g_free(l);
}

OdpStyles* odp_styles_new(void) {
    OdpStyles* s = g_new0(OdpStyles, 1);
    s->styles = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, style_free);
    s->defaults = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, style_free);
    s->lists = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, list_free);
    s->fonts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    s->gradients = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    s->images = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    s->layouts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    s->masters = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->templates = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_destroy);
    return s;
}

void odp_styles_free(OdpStyles* styles) {
    if (styles == NULL) {
        return;
    }
    g_hash_table_destroy(styles->styles);
    g_hash_table_destroy(styles->defaults);
    g_hash_table_destroy(styles->lists);
    g_hash_table_destroy(styles->fonts);
    g_hash_table_destroy(styles->gradients);
    g_hash_table_destroy(styles->images);
    g_hash_table_destroy(styles->layouts);
    g_hash_table_destroy(styles->masters);
    g_hash_table_destroy(styles->templates);
    g_free(styles->first_master);
    g_free(styles);
}

static void add_list(OdpStyles* styles, const OdpNode* node) {
    const gchar* name = odp_node_attr(node, "style:name");
    if (name == NULL) {
        return;
    }
    OdpList* list = g_new0(OdpList, 1);
    for (guint i = 0; i < node->children->len; i++) {
        const OdpNode* lv = g_ptr_array_index(node->children, i);
        gboolean bullet = odp_node_is(lv, "text:list-level-style-bullet");
        gboolean number = odp_node_is(lv, "text:list-level-style-number");
        gboolean image = odp_node_is(lv, "text:list-level-style-image");
        if (!bullet && !number && !image) {
            continue;
        }
        const gchar* level = odp_node_attr(lv, "text:level");
        gint n = level != NULL ? atoi(level) : 1;
        if (n < 1 || n > ODP_LIST_LEVELS) {
            continue;
        }
        OdpListLevel* l = &list->levels[n - 1];
        l->set = TRUE;
        l->color = 0xFFFFFFFFu;
        if (bullet) {
            const gchar* c = odp_node_attr(lv, "text:bullet-char");
            l->bullet = c != NULL && c[0] != '\0' ? g_strdup(c) : NULL;
        } else if (image) {
            l->bullet = g_strdup("•");
        } else {
            l->numbered = TRUE;
            l->num_format = g_strdup(odp_node_attr(lv, "style:num-format"));
            const gchar* start = odp_node_attr(lv, "text:start-value");
            l->start = start != NULL ? atoi(start) : 1;
        }
        l->num_prefix = g_strdup(odp_node_attr(lv, "style:num-prefix"));
        l->num_suffix = g_strdup(odp_node_attr(lv, "style:num-suffix"));
        const OdpNode* props = odp_node_child(lv, "style:list-level-properties");
        l->space_before = odp_length(odp_node_attr(props, "text:space-before"), 0);
        l->min_label_width = odp_length(odp_node_attr(props, "text:min-label-width"), 0);
        if (g_strcmp0(odp_node_attr(props, "text:list-level-position-and-space-mode"), "label-alignment") == 0) {
            const OdpNode* align = odp_node_child(props, "style:list-level-label-alignment");
            l->label_alignment = TRUE;
            l->margin_left = odp_length(odp_node_attr(align, "fo:margin-left"), 0);
            l->text_indent = odp_length(odp_node_attr(align, "fo:text-indent"), 0);
        }
        const OdpNode* text = odp_node_child(lv, "style:text-properties");
        if (text != NULL) {
            const gchar* face = odp_node_attr(text, "style:font-name");
            const gchar* family = face != NULL ? odp_styles_font(styles, face) : NULL;
            l->font = family != NULL ? g_strdup(family) : odp_first_family(odp_node_attr(text, "fo:font-family"));
            gdouble pct = odp_percent(odp_node_attr(text, "fo:font-size"));
            l->size_percent = pct > 0 ? pct : 0;
            if (g_strcmp0(odp_node_attr(text, "style:use-window-font-color"), "true") != 0) {
                l->color = odp_color(odp_node_attr(text, "fo:color"), 0xFFFFFFFFu);
            }
        }
    }
    g_hash_table_replace(styles->lists, g_strdup(name), list);
}

static void add_props(OdpStyles* styles, OdpStyle* style, const OdpNode* node) {
    for (guint i = 0; i < node->children->len; i++) {
        const OdpNode* p = g_ptr_array_index(node->children, i);
        if (p->name == NULL || !g_str_has_suffix(p->name, "-properties")) {
            continue;
        }
        for (gint k = 0; p->attr_names != NULL && p->attr_names[k] != NULL; k++) {
            g_hash_table_replace(style->props, g_strdup(p->attr_names[k]), g_strdup(p->attr_values[k]));
        }
        const OdpNode* list = odp_node_child(p, "text:list-style");
        if (list != NULL && odp_node_attr(list, "style:name") != NULL) {
            add_list(styles, list);
            g_free(style->list);
            style->list = g_strdup(odp_node_attr(list, "style:name"));
        }
    }
}

static void add_style(OdpStyles* styles, const OdpNode* node, gboolean is_default) {
    const gchar* family = odp_node_attr(node, "style:family");
    const gchar* name = odp_node_attr(node, "style:name");
    if (family == NULL || (!is_default && name == NULL)) {
        return;
    }
    OdpStyle* s = g_new0(OdpStyle, 1);
    s->family = g_strdup(family);
    s->parent = g_strdup(odp_node_attr(node, "style:parent-style-name"));
    s->props = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    s->is_default = is_default;
    add_props(styles, s, node);
    if (is_default) {
        g_hash_table_replace(styles->defaults, g_strdup(family), s);
    } else {
        g_hash_table_replace(styles->styles, g_strconcat(family, "/", name, NULL), s);
    }
}

static void add_gradient(OdpStyles* styles, const OdpNode* node) {
    const gchar* name = odp_node_attr(node, "draw:name");
    if (name == NULL) {
        return;
    }
    OdpGradient* g = g_new0(OdpGradient, 1);
    g->start = odp_color(odp_node_attr(node, "draw:start-color"), 0x000000);
    g->end = odp_color(odp_node_attr(node, "draw:end-color"), 0xFFFFFF);
    const gchar* angle = odp_node_attr(node, "draw:angle");
    if (angle != NULL) {
        gchar* end = NULL;
        gdouble a = g_ascii_strtod(angle, &end);
        /* No unit: tenths of a degree (ODF 1.2) */
        if (end != NULL && g_str_has_prefix(end, "deg")) g->angle = a;
        else if (end != NULL && g_str_has_prefix(end, "rad")) g->angle = a * 180.0 / G_PI;
        else if (end != NULL && g_str_has_prefix(end, "grad")) g->angle = a * 0.9;
        else g->angle = a / 10.0;
    }
    const gchar* style = odp_node_attr(node, "draw:style");
    g->axial = g_strcmp0(style, "axial") == 0;
    g->radial = style != NULL && !g->axial && strcmp(style, "linear") != 0;
    g_hash_table_replace(styles->gradients, g_strdup(name), g);
}

static void add_template(OdpStyles* styles, const OdpNode* node) {
    const gchar* name = odp_node_attr(node, "table:name");
    if (name == NULL) {
        name = odp_node_attr(node, "text:style-name");
    }
    if (name == NULL) {
        return;
    }
    GHashTable* parts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (guint i = 0; i < node->children->len; i++) {
        const OdpNode* p = g_ptr_array_index(node->children, i);
        const gchar* style = odp_node_attr(p, "table:style-name");
        if (style == NULL) {
            style = odp_node_attr(p, "text:style-name");
        }
        if (p->name != NULL && style != NULL) {
            g_hash_table_replace(parts, g_strdup(zip_xml_local(p->name)), g_strdup(style));
        }
    }
    g_hash_table_replace(styles->templates, g_strdup(name), parts);
}

static void add_entry(OdpStyles* styles, const OdpNode* n) {
    if (odp_node_is(n, "style:style")) {
        add_style(styles, n, FALSE);
    } else if (odp_node_is(n, "style:default-style")) {
        add_style(styles, n, TRUE);
    } else if (odp_node_is(n, "text:list-style")) {
        add_list(styles, n);
    } else if (odp_node_is(n, "style:font-face")) {
        const gchar* name = odp_node_attr(n, "style:name");
        gchar* family = odp_first_family(odp_node_attr(n, "svg:font-family"));
        if (name != NULL && family != NULL) {
            g_hash_table_replace(styles->fonts, g_strdup(name), family);
        } else {
            g_free(family);
        }
    } else if (odp_node_is(n, "draw:gradient")) {
        add_gradient(styles, n);
    } else if (odp_node_is(n, "draw:fill-image")) {
        const gchar* name = odp_node_attr(n, "draw:name");
        const gchar* href = odp_node_attr(n, "xlink:href");
        if (name != NULL && href != NULL) {
            g_hash_table_replace(styles->images, g_strdup(name), g_strdup(href));
        }
    } else if (odp_node_is(n, "style:page-layout")) {
        const gchar* name = odp_node_attr(n, "style:name");
        const OdpNode* p = odp_node_child(n, "style:page-layout-properties");
        gdouble w = odp_length(odp_node_attr(p, "fo:page-width"), 0);
        gdouble h = odp_length(odp_node_attr(p, "fo:page-height"), 0);
        if (name != NULL && w > 0 && h > 0) {
            /* Width, height, then the margins: top, bottom, left, right */
            gdouble* size = g_new(gdouble, 6);
            size[0] = w;
            size[1] = h;
            size[2] = odp_length(odp_node_attr(p, "fo:margin-top"), 56.7);
            size[3] = odp_length(odp_node_attr(p, "fo:margin-bottom"), 56.7);
            size[4] = odp_length(odp_node_attr(p, "fo:margin-left"), 56.7);
            size[5] = odp_length(odp_node_attr(p, "fo:margin-right"), 56.7);
            g_hash_table_replace(styles->layouts, g_strdup(name), size);
        }
    } else if (odp_node_is(n, "style:master-page")) {
        const gchar* name = odp_node_attr(n, "style:name");
        if (name != NULL) {
            g_hash_table_replace(styles->masters, g_strdup(name), (gpointer)n);
            if (styles->first_master == NULL) {
                styles->first_master = g_strdup(name);
            }
        }
    } else if (odp_node_is(n, "table:table-template")) {
        add_template(styles, n);
    }
}

void odp_styles_add(OdpStyles* styles, const OdpNode* root) {
    if (root == NULL) {
        return;
    }
    static const gchar* const SECTIONS[] = { "office:font-face-decls", "office:styles", "office:automatic-styles",
                                             "office:master-styles" };
    for (gsize s = 0; s < G_N_ELEMENTS(SECTIONS); s++) {
        const OdpNode* section = odp_node_child(root, SECTIONS[s]);
        for (guint i = 0; section != NULL && i < section->children->len; i++) {
            add_entry(styles, g_ptr_array_index(section->children, i));
        }
    }
}

const gchar* odp_styles_font(const OdpStyles* styles, const gchar* face) {
    return face != NULL ? g_hash_table_lookup(styles->fonts, face) : NULL;
}

const OdpList* odp_styles_list(const OdpStyles* styles, const gchar* name) {
    return name != NULL ? g_hash_table_lookup(styles->lists, name) : NULL;
}

static const OdpStyle* find_style(const OdpStyles* styles, const gchar* family, const gchar* name) {
    if (name == NULL) {
        return NULL;
    }
    gchar* key = g_strconcat(family, "/", name, NULL);
    const OdpStyle* s = g_hash_table_lookup(styles->styles, key);
    g_free(key);
    return s;
}

const OdpList* odp_styles_style_list(const OdpStyles* styles, const gchar* family, const gchar* name) {
    for (gint depth = 0; name != NULL && depth < MAX_PARENTS; depth++) {
        const OdpStyle* s = find_style(styles, family, name);
        if (s == NULL) {
            break;
        }
        if (s->list != NULL) {
            const OdpList* l = odp_styles_list(styles, s->list);
            if (l != NULL) {
                return l;
            }
        }
        name = s->parent;
    }
    return NULL;
}

gboolean odp_styles_page_size(const OdpStyles* styles, const gchar* master, gdouble* width, gdouble* height) {
    const OdpNode* m = master != NULL ? g_hash_table_lookup(styles->masters, master) : NULL;
    const gchar* layout = odp_node_attr(m, "style:page-layout-name");
    const gdouble* size = layout != NULL ? g_hash_table_lookup(styles->layouts, layout) : NULL;
    if (size == NULL) {
        return FALSE;
    }
    *width = size[0];
    *height = size[1];
    return TRUE;
}

gboolean odp_styles_page_margins(const OdpStyles* styles, const gchar* master, gdouble margins[4]) {
    const OdpNode* m = master != NULL ? g_hash_table_lookup(styles->masters, master) : NULL;
    const gchar* layout = odp_node_attr(m, "style:page-layout-name");
    const gdouble* size = layout != NULL ? g_hash_table_lookup(styles->layouts, layout) : NULL;
    if (size == NULL) {
        return FALSE;
    }
    memcpy(margins, size + 2, 4 * sizeof(gdouble));
    return TRUE;
}

static gchar* to_roman(gint n, gboolean upper) {
    static const gint VALUES[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const gchar* const DIGITS[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    GString* s = g_string_new(NULL);
    n = CLAMP(n, 1, 3999);
    for (gsize i = 0; i < G_N_ELEMENTS(VALUES); i++) {
        while (n >= VALUES[i]) {
            g_string_append(s, DIGITS[i]);
            n -= VALUES[i];
        }
    }
    if (upper) {
        gchar* u = g_ascii_strup(s->str, -1);
        g_string_free(s, TRUE);
        return u;
    }
    return g_string_free(s, FALSE);
}

gchar* odp_list_label(const OdpListLevel* l, gint n) {
    const gchar* f = l->num_format != NULL ? l->num_format : "1";
    gchar* num;
    if (f[0] == 'a' || f[0] == 'A') {
        GString* s = g_string_new(NULL);
        for (gint k = n; k > 0; k = (k - 1) / 26) {
            g_string_prepend_c(s, (gchar)((f[0] == 'a' ? 'a' : 'A') + (k - 1) % 26));
        }
        num = g_string_free(s, FALSE);
    } else if (f[0] == 'i' || f[0] == 'I') {
        num = to_roman(n, f[0] == 'I');
    } else if (f[0] == '\0') {
        num = g_strdup("");
    } else {
        num = g_strdup_printf("%d", n);
    }
    gchar* label = g_strconcat(l->num_prefix != NULL ? l->num_prefix : "", num,
                               l->num_suffix != NULL ? l->num_suffix : "", NULL);
    g_free(num);
    return label;
}

const gchar* odp_styles_template_part(const OdpStyles* styles, const gchar* name, const gchar* part) {
    GHashTable* parts = name != NULL ? g_hash_table_lookup(styles->templates, name) : NULL;
    return parts != NULL ? g_hash_table_lookup(parts, part) : NULL;
}

/* ---- Cascades ---- */

static void cascade_push(OdpCascade* c, const OdpStyle* s) {
    if (c->n < ODP_CASCADE_MAX) {
        c->chain[c->n++] = s;
    }
}

void odp_cascade_add(OdpCascade* c, const OdpStyles* styles, const gchar* family, const gchar* name) {
    for (gint depth = 0; name != NULL && depth < MAX_PARENTS; depth++) {
        const OdpStyle* s = find_style(styles, family, name);
        if (s == NULL) {
            break;
        }
        cascade_push(c, s);
        name = s->parent;
    }
}

void odp_cascade_add_default(OdpCascade* c, const OdpStyles* styles, const gchar* family) {
    const OdpStyle* s = g_hash_table_lookup(styles->defaults, family);
    if (s != NULL) {
        cascade_push(c, s);
    }
}

void odp_cascade_append(OdpCascade* c, const OdpCascade* other) {
    for (guint i = 0; i < other->n; i++) {
        cascade_push(c, other->chain[i]);
    }
}

const gchar* odp_cascade_get(const OdpCascade* c, const gchar* key) {
    for (guint i = 0; i < c->n; i++) {
        const gchar* v = g_hash_table_lookup(c->chain[i]->props, key);
        if (v != NULL) {
            return v;
        }
    }
    return NULL;
}

const gchar* odp_cascade_get_either(const OdpCascade* c, const gchar* key, const gchar* other, gboolean* second) {
    for (guint i = 0; i < c->n; i++) {
        const gchar* v = g_hash_table_lookup(c->chain[i]->props, key);
        if (v != NULL) {
            *second = FALSE;
            return v;
        }
        v = g_hash_table_lookup(c->chain[i]->props, other);
        if (v != NULL) {
            *second = TRUE;
            return v;
        }
    }
    *second = FALSE;
    return NULL;
}

const gchar* odp_cascade_get_explicit(const OdpCascade* c, const gchar* key) {
    for (guint i = 0; i < c->n; i++) {
        if (c->chain[i]->is_default) {
            continue;
        }
        const gchar* v = g_hash_table_lookup(c->chain[i]->props, key);
        if (v != NULL) {
            return v;
        }
    }
    return NULL;
}

gdouble odp_cascade_font_size(const OdpCascade* c, gdouble fallback) {
    gdouble factor = 1.0;
    for (guint i = 0; i < c->n; i++) {
        const gchar* v = g_hash_table_lookup(c->chain[i]->props, "fo:font-size");
        if (v == NULL) {
            continue;
        }
        gdouble pct = odp_percent(v);
        if (pct > 0) {
            factor *= pct;
            continue;
        }
        gdouble size = odp_length(v, -1);
        if (size > 0) {
            return size * factor;
        }
    }
    return fallback * factor;
}
