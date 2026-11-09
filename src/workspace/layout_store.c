/* Saved workspace layouts (see layout_store.h) */
#include "workspace/layout_store.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include "workspace/tab_tree.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

SavedLayout* saved_layout_new(const gchar* name) {
    SavedLayout* l = g_new0(SavedLayout, 1);
    l->name = g_strdup(name != NULL ? name : "");
    l->files_vis = TRUE;
    l->preview_vis = TRUE;
    l->main_frac = 0.25;
    l->right_frac = 0.70;
    l->editor_frac = 0.50;
    l->terminals = g_strdup("");
    l->editor_splits = g_strdup("");
    return l;
}

SavedLayout* saved_layout_copy(const SavedLayout* layout) {
    SavedLayout* l = g_new(SavedLayout, 1);
    *l = *layout;
    l->name = g_strdup(layout->name);
    l->terminals = g_strdup(layout->terminals);
    l->editor_splits = g_strdup(layout->editor_splits);
    return l;
}

void saved_layout_free(gpointer layout) {
    SavedLayout* l = layout;
    if (l == NULL) {
        return;
    }
    g_free(l->name);
    g_free(l->terminals);
    g_free(l->editor_splits);
    g_free(l);
}

gchar* saved_layout_summary(const SavedLayout* layout) {
    GPtrArray* parts = g_ptr_array_new_with_free_func(g_free);
    if (layout->files_vis) {
        g_ptr_array_add(parts, g_strdup(_("Explorer")));
    }
    if (layout->preview_vis) {
        g_ptr_array_add(parts, g_strdup(_("Preview")));
    }
    if (layout->editor_dedicated) {
        TabTreeNode* tree = tab_tree_parse(layout->editor_splits);
        guint panes = tab_tree_notebook_count(tree);
        guint terms = tab_tree_terminal_count(tree);
        tab_tree_free(tree);
        gchar* pane_text = panes > 1 ? g_strdup_printf(ngettext("%u pane", "%u panes", panes), panes) : NULL;
        gchar* term_text = terms > 0 ? g_strdup_printf(ngettext("%u terminal", "%u terminals", terms), terms) : NULL;
        gchar* text;
        if (pane_text != NULL && term_text != NULL) {
            /* TRANSLATORS: e.g. "Editor Panel (2 panes, 1 terminal)" */
            text = g_strdup_printf(_("Editor Panel (%1$s, %2$s)"), pane_text, term_text);
        } else if (pane_text != NULL || term_text != NULL) {
            text = g_strdup_printf(_("Editor Panel (%s)"), pane_text != NULL ? pane_text : term_text);
        } else {
            text = g_strdup(_("Editor Panel"));
        }
        g_free(pane_text);
        g_free(term_text);
        g_ptr_array_add(parts, text);
    }
    if (layout->term_vis) {
        TermLayoutNode* tree = term_layout_parse(layout->terminals);
        guint n = MAX(1, term_layout_count(tree));
        term_layout_free(tree);
        gchar* text;
        if (n > 1) {
            text = g_strdup_printf(layout->stacked
                                       ? ngettext("Terminal (%u terminal, below)", "Terminal (%u terminals, below)", n)
                                       : ngettext("Terminal (%u terminal, beside)", "Terminal (%u terminals, beside)", n),
                                   n);
        } else {
            text = g_strdup(layout->stacked ? _("Terminal (below)") : _("Terminal (beside)"));
        }
        g_ptr_array_add(parts, text);
    }
    if (parts->len == 0) {
        g_ptr_array_add(parts, g_strdup(_("No panels")));
    }
    if (layout->tagbars && (layout->editor_tagbar || layout->preview_tagbar)) {
        g_ptr_array_add(parts, g_strdup(!layout->preview_tagbar ? _("Tagbar (editor)")
                                        : !layout->editor_tagbar ? _("Tagbar (preview)")
                                                                 : _("Tagbar")));
    }
    g_ptr_array_add(parts, NULL);
    gchar* text = g_strjoinv(" · ", (gchar**)parts->pdata);
    g_ptr_array_free(parts, TRUE);
    return text;
}

gchar* layout_store_default_path(void) {
    gchar* dir = app_config_dir();
    gchar* path = g_build_filename(dir, "layouts.ini", NULL);
    g_free(dir);
    return path;
}

static gdouble read_frac(GKeyFile* kf, const gchar* group, const gchar* key, gdouble fallback) {
    GError* err = NULL;
    gdouble v = g_key_file_get_double(kf, group, key, &err);
    if (err != NULL) {
        g_error_free(err);
        return fallback;
    }
    return CLAMP(v, 0.05, 0.95);
}

static gboolean read_bool(GKeyFile* kf, const gchar* group, const gchar* key, gboolean fallback) {
    GError* err = NULL;
    gboolean v = g_key_file_get_boolean(kf, group, key, &err);
    if (err != NULL) {
        g_error_free(err);
        return fallback;
    }
    return v;
}

#define TAGBAR_MAX_WIDTH 4000

static const gchar* DEFAULT_GROUP = "Default";

SavedLayout* saved_layout_read(GKeyFile* kf, const gchar* group, const gchar* name) {
    SavedLayout* l = saved_layout_new(name);
    l->files_vis = read_bool(kf, group, "Explorer", TRUE);
    l->preview_vis = read_bool(kf, group, "Preview", TRUE);
    l->term_vis = read_bool(kf, group, "Terminal", FALSE);
    l->editor_dedicated = read_bool(kf, group, "EditorPanel", FALSE);
    l->stacked = read_bool(kf, group, "TerminalBelow", FALSE);
    l->main_frac = read_frac(kf, group, "ExplorerWidth", l->main_frac);
    l->right_frac = read_frac(kf, group, "PreviewShare", l->right_frac);
    l->editor_frac = read_frac(kf, group, "EditorShare", l->editor_frac);
    gchar* terms = g_key_file_get_string(kf, group, "TerminalPanes", NULL);
    TermLayoutNode* tree = term_layout_parse(terms);
    g_free(l->terminals);
    l->terminals = (tree != NULL) ? term_layout_to_string(tree) : g_strdup("");
    term_layout_free(tree);
    g_free(terms);
    gchar* splits = g_key_file_get_string(kf, group, "EditorSplits", NULL);
    TabTreeNode* editor = tab_tree_parse(splits);
    g_free(l->editor_splits);
    l->editor_splits = (editor != NULL) ? tab_tree_to_string(editor) : g_strdup("");
    tab_tree_free(editor);
    g_free(splits);
    l->tagbars = g_key_file_has_key(kf, group, "EditorTagbar", NULL) ||
                 g_key_file_has_key(kf, group, "PreviewTagbar", NULL);
    l->editor_tagbar = read_bool(kf, group, "EditorTagbar", FALSE);
    l->preview_tagbar = read_bool(kf, group, "PreviewTagbar", FALSE);
    gint width = g_key_file_get_integer(kf, group, "TagbarWidth", NULL); /* 0 if missing */
    l->tagbar_width = CLAMP(width, 0, TAGBAR_MAX_WIDTH);
    return l;
}

static GKeyFile* open_store(const gchar* path) {
    GKeyFile* kf = g_key_file_new();
    if (path == NULL || !g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(kf);
        return NULL;
    }
    return kf;
}

GPtrArray* layout_store_load(const gchar* path) {
    GPtrArray* layouts = g_ptr_array_new_with_free_func(saved_layout_free);
    GKeyFile* kf = open_store(path);
    if (kf == NULL) {
        return layouts;
    }
    gchar** groups = g_key_file_get_groups(kf, NULL);
    for (gchar** g = groups; *g != NULL; g++) {
        if (g_strcmp0(*g, DEFAULT_GROUP) == 0) {
            continue;
        }
        gchar* name = g_key_file_get_string(kf, *g, "Name", NULL);
        if (name != NULL && name[0] != '\0' && layout_store_find(layouts, name) == NULL) {
            g_ptr_array_add(layouts, saved_layout_read(kf, *g, name));
        }
        g_free(name);
    }
    g_strfreev(groups);
    g_key_file_free(kf);
    return layouts;
}

SavedLayout* layout_store_builtin_default(void) {
    return saved_layout_new("Default");
}

typedef struct {
    const gchar* name;
    gboolean files_vis, preview_vis, term_vis, editor_dedicated, stacked;
    gdouble main_frac, right_frac, editor_frac;
    const gchar* terminals;
} Preset;

static const Preset PRESETS[] = {
    { N_("Code"), TRUE, TRUE, TRUE, TRUE, TRUE, 0.18, 0.70, 0.40, "T" },
    { N_("Terminal"), TRUE, TRUE, TRUE, FALSE, FALSE, 0.22, 0.40, 0.50, "H:0.500(T,T)" },
    { N_("Focus"), FALSE, TRUE, FALSE, FALSE, FALSE, 0.25, 0.70, 0.50, "" },
};

GPtrArray* layout_store_presets(GPtrArray* saved) {
    GPtrArray* presets = g_ptr_array_new_with_free_func(saved_layout_free);
    for (guint i = 0; i < G_N_ELEMENTS(PRESETS); i++) {
        const Preset* p = &PRESETS[i];
        const gchar* name = _(p->name);
        if (saved != NULL && layout_store_find(saved, name) != NULL) {
            continue;
        }
        SavedLayout* l = saved_layout_new(name);
        l->files_vis = p->files_vis;
        l->preview_vis = p->preview_vis;
        l->term_vis = p->term_vis;
        l->editor_dedicated = p->editor_dedicated;
        l->stacked = p->stacked;
        l->main_frac = p->main_frac;
        l->right_frac = p->right_frac;
        l->editor_frac = p->editor_frac;
        g_free(l->terminals);
        l->terminals = g_strdup(p->terminals);
        g_ptr_array_add(presets, l);
    }
    return presets;
}

/* n panes in a row (H) or a column (V), sharing the room evenly */
static gchar* grid_line(guint n, gchar dir) {
    if (n <= 1) {
        return g_strdup("[0]");
    }
    gchar* rest = grid_line(n - 1, dir);
    gchar frac[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(frac, sizeof frac, "%.3f", 1.0 / n); /* a dot whatever the locale */
    gchar* text = g_strdup_printf("%c:%s([0],%s)", dir, frac, rest);
    g_free(rest);
    return text;
}

gchar* layout_store_grid_splits(guint files, gint width, gint height) {
    if (width <= 0 || height <= 0) { /* size unknown: a wide panel */
        width = 2 * LAYOUT_GRID_MIN_W;
        height = 2 * LAYOUT_GRID_MIN_H;
    }
    guint cols = (guint)MAX(1, width / LAYOUT_GRID_MIN_W);
    guint rows = (guint)MAX(1, height / LAYOUT_GRID_MIN_H);
    /* two files always get two panes, even below the minimum size */
    guint n = MIN(MIN(MAX(files, 1u), 4u), MAX(cols * rows, 2u));
    gboolean wide = width >= height;
    gboolean square = cols >= 2 && rows >= 2; /* room for two by two */
    switch (n) {
    case 1:
        return g_strdup("[0]");
    case 2:
        /* side by side when wide, else one above the other, even below
         * the minimum size */
        return grid_line(2, wide ? 'H' : 'V');
    case 3:
        if (square) {
            return g_strdup(wide ? "H:0.500([0],V:0.500([0],[0]))"  /* one left, two stacked right */
                                 : "V:0.500([0],H:0.500([0],[0]))"); /* one on top, two below */
        }
        return grid_line(3, cols >= 3 ? 'H' : 'V');
    default:
        if (square) {
            return g_strdup("H:0.500(V:0.500([0],[0]),V:0.500([0],[0]))");
        }
        return grid_line(4, cols >= 4 ? 'H' : 'V');
    }
}

void layout_store_editor_area(const SavedLayout* l, gint width, gint height, gint* editor_w, gint* editor_h) {
    gdouble w = width;
    gdouble h = height;
    if (l->files_vis) {
        w *= 1.0 - l->main_frac;
    }
    if (l->term_vis) {
        if (l->stacked) {
            h *= l->right_frac;
        } else {
            w *= l->right_frac;
        }
    }
    if (l->preview_vis) {
        w *= 1.0 - l->editor_frac;
    }
    *editor_w = (gint)(w + 0.5);
    *editor_h = (gint)(h + 0.5);
}

SavedLayout* layout_store_auto(const SavedLayout* current, guint files, const AutoRules* rules, gint width,
                               gint height) {
    SavedLayout* l = saved_layout_copy(current);
    g_free(l->name);
    l->name = g_strdup(_("Auto"));
    if (!current->editor_dedicated) {
        l->editor_frac = 0.40; /* the editor panel opens beside the preview: give it the room */
    }
    l->editor_dedicated = TRUE;
    if (rules != NULL && rules->show_terminal >= 0) {
        l->term_vis = (rules->show_terminal > 0);
    }
    if (rules != NULL && rules->show_preview >= 0) {
        l->preview_vis = (rules->show_preview > 0);
    }
    if (rules != NULL && rules->terminal_below >= 0) {
        l->stacked = (rules->terminal_below > 0);
    }
    gint ew = 0;
    gint eh = 0;
    if (width > 0 && height > 0) {
        layout_store_editor_area(l, width, height, &ew, &eh);
    }
    /* two editors go side by side in a panel that wide and wider than tall */
    gint side_by_side = MAX(2 * LAYOUT_GRID_MIN_W, eh);
    if (files >= 2 && l->preview_vis && ew > 0 && ew < side_by_side) {
        /* the preview narrows (down to LAYOUT_PREVIEW_MIN_FRAC) when that
         * leaves room for two editors side by side */
        l->preview_vis = FALSE;
        gint shared = 0;
        layout_store_editor_area(l, width, height, &shared, &eh);
        l->preview_vis = TRUE;
        gdouble frac = floor((1.0 - (gdouble)side_by_side / shared) * 100.0) / 100.0;
        if (frac >= LAYOUT_PREVIEW_MIN_FRAC && frac < l->editor_frac) {
            l->editor_frac = frac;
            layout_store_editor_area(l, width, height, &ew, &eh);
        }
    }
    g_free(l->editor_splits);
    l->editor_splits = layout_store_grid_splits(files, ew, eh);
    return l;
}

gboolean layout_store_is_customized(const SavedLayout* l) {
    SavedLayout* d = layout_store_builtin_default();
    gboolean differs = l->files_vis != d->files_vis || l->preview_vis != d->preview_vis ||
                       l->term_vis != d->term_vis || l->editor_dedicated != d->editor_dedicated ||
                       l->stacked != d->stacked || ABS(l->main_frac - d->main_frac) > 0.0005 ||
                       ABS(l->right_frac - d->right_frac) > 0.0005 ||
                       ABS(l->editor_frac - d->editor_frac) > 0.0005 ||
                       g_strcmp0(l->terminals, d->terminals) != 0 ||
                       g_strcmp0(l->editor_splits, d->editor_splits) != 0 ||
                       (l->tagbars && (l->editor_tagbar || l->preview_tagbar));
    saved_layout_free(d);
    return differs;
}

SavedLayout* layout_store_load_default(const gchar* path) {
    GKeyFile* kf = open_store(path);
    SavedLayout* l = (kf != NULL && g_key_file_has_group(kf, DEFAULT_GROUP))
                         ? saved_layout_read(kf, DEFAULT_GROUP, "Default")
                         : layout_store_builtin_default();
    if (kf != NULL) {
        g_key_file_free(kf);
    }
    return l;
}

/* Fractions with 3 decimals ("0.700", not "0.69999999999999996") */
static void set_frac(GKeyFile* kf, const gchar* group, const gchar* key, gdouble value) {
    gchar text[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(text, sizeof text, "%.3f", value);
    g_key_file_set_value(kf, group, key, text);
}

void saved_layout_write(GKeyFile* kf, const gchar* group, const SavedLayout* l, gboolean named) {
    if (named) {
        g_key_file_set_string(kf, group, "Name", l->name);
    }
    g_key_file_set_boolean(kf, group, "Explorer", l->files_vis);
    g_key_file_set_boolean(kf, group, "Preview", l->preview_vis);
    g_key_file_set_boolean(kf, group, "Terminal", l->term_vis);
    g_key_file_set_boolean(kf, group, "EditorPanel", l->editor_dedicated);
    g_key_file_set_boolean(kf, group, "TerminalBelow", l->stacked);
    set_frac(kf, group, "ExplorerWidth", l->main_frac);
    set_frac(kf, group, "PreviewShare", l->right_frac);
    set_frac(kf, group, "EditorShare", l->editor_frac);
    g_key_file_set_string(kf, group, "TerminalPanes", l->terminals);
    if (l->editor_splits != NULL && l->editor_splits[0] != '\0') {
        g_key_file_set_string(kf, group, "EditorSplits", l->editor_splits);
    }
    if (l->tagbars) {
        g_key_file_set_boolean(kf, group, "EditorTagbar", l->editor_tagbar);
        g_key_file_set_boolean(kf, group, "PreviewTagbar", l->preview_tagbar);
        if (l->tagbar_width > 0) {
            g_key_file_set_integer(kf, group, "TagbarWidth", l->tagbar_width);
        }
    }
}

gboolean layout_store_save(const gchar* path, GPtrArray* layouts, const SavedLayout* default_layout,
                           GError** error) {
    GKeyFile* kf = g_key_file_new();
    if (default_layout != NULL && layout_store_is_customized(default_layout)) {
        saved_layout_write(kf, DEFAULT_GROUP, default_layout, FALSE);
    }
    for (guint i = 0; i < layouts->len; i++) {
        gchar* group = g_strdup_printf("Layout %u", i + 1);
        saved_layout_write(kf, group, g_ptr_array_index(layouts, i), TRUE);
        g_free(group);
    }
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    gboolean ok = g_key_file_save_to_file(kf, path, error);
    g_key_file_free(kf);
    return ok;
}

SavedLayout* layout_store_find(GPtrArray* layouts, const gchar* name) {
    for (guint i = 0; i < layouts->len; i++) {
        SavedLayout* l = g_ptr_array_index(layouts, i);
        if (g_strcmp0(l->name, name) == 0) {
            return l;
        }
    }
    return NULL;
}

void layout_store_put(GPtrArray* layouts, SavedLayout* layout) {
    for (guint i = 0; i < layouts->len; i++) {
        SavedLayout* l = g_ptr_array_index(layouts, i);
        if (g_strcmp0(l->name, layout->name) == 0) {
            saved_layout_free(l);
            g_ptr_array_index(layouts, i) = layout;
            return;
        }
    }
    g_ptr_array_add(layouts, layout);
}

gboolean layout_store_remove(GPtrArray* layouts, const gchar* name) {
    SavedLayout* l = layout_store_find(layouts, name);
    return l != NULL && g_ptr_array_remove(layouts, l);
}

/* ------------------------------------------------------------------------- */
/* Terminal pane tree                                                        */
/* ------------------------------------------------------------------------- */

static TermLayoutNode* parse_node(const gchar** p, gint depth) {
    if (depth > 16) {
        return NULL;
    }
    if (**p == 'T') {
        (*p)++;
        guint tabs = 1;
        if (g_ascii_isdigit(**p)) {
            gchar* end = NULL;
            guint64 v = g_ascii_strtoull(*p, &end, 10);
            if (v < 1 || v > TERM_LAYOUT_MAX_TABS) {
                return NULL;
            }
            tabs = (guint)v;
            *p = end;
        }
        TermLayoutNode* n = g_new0(TermLayoutNode, 1);
        n->is_terminal = TRUE;
        n->tabs = tabs;
        return n;
    }
    if ((**p != 'H' && **p != 'V') || (*p)[1] != ':') {
        return NULL;
    }
    gboolean vertical = (**p == 'V');
    *p += 2;
    gchar* end = NULL;
    gdouble frac = g_ascii_strtod(*p, &end);
    if (end == *p || *end != '(') {
        return NULL;
    }
    *p = end + 1;
    TermLayoutNode* first = parse_node(p, depth + 1);
    if (first == NULL || **p != ',') {
        term_layout_free(first);
        return NULL;
    }
    (*p)++;
    TermLayoutNode* second = parse_node(p, depth + 1);
    if (second == NULL || **p != ')') {
        term_layout_free(first);
        term_layout_free(second);
        return NULL;
    }
    (*p)++;
    TermLayoutNode* n = g_new0(TermLayoutNode, 1);
    n->vertical = vertical;
    n->frac = CLAMP(frac, 0.05, 0.95);
    n->first = first;
    n->second = second;
    return n;
}

TermLayoutNode* term_layout_parse(const gchar* text) {
    if (text == NULL || text[0] == '\0') {
        return NULL;
    }
    const gchar* p = text;
    TermLayoutNode* n = parse_node(&p, 0);
    if (n != NULL && *p != '\0') {
        term_layout_free(n);
        return NULL;
    }
    return n;
}

static void append_node(GString* s, const TermLayoutNode* n) {
    if (n->is_terminal) {
        g_string_append_c(s, 'T');
        if (n->tabs > 1) {
            g_string_append_printf(s, "%u", n->tabs);
        }
        return;
    }
    gchar frac[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(frac, sizeof frac, "%.3f", n->frac);
    g_string_append_printf(s, "%c:%s(", n->vertical ? 'V' : 'H', frac);
    append_node(s, n->first);
    g_string_append_c(s, ',');
    append_node(s, n->second);
    g_string_append_c(s, ')');
}

gchar* term_layout_to_string(const TermLayoutNode* node) {
    GString* s = g_string_new(NULL);
    if (node != NULL) {
        append_node(s, node);
    }
    return g_string_free(s, FALSE);
}

guint term_layout_count(const TermLayoutNode* node) {
    if (node == NULL) {
        return 0;
    }
    return node->is_terminal ? MAX(node->tabs, 1) : term_layout_count(node->first) + term_layout_count(node->second);
}

void term_layout_free(TermLayoutNode* node) {
    if (node != NULL) {
        term_layout_free(node->first);
        term_layout_free(node->second);
        g_free(node);
    }
}
