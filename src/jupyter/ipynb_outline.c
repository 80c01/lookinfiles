/* The outline of a Jupyter notebook (see ipynb_outline.h) */
#include "jupyter/ipynb_outline.h"

#include <string.h>

GPtrArray* ipynb_outline_headings(const IpynbDoc* doc) {
    GPtrArray* out = code_outline_entries_new();
    guint n = doc != NULL ? ipynb_doc_n_cells(doc) : 0;
    for (guint i = 0; i < n; i++) {
        if (ipynb_doc_cell_type(doc, i) != IPYNB_CELL_MARKDOWN) continue;
        gchar* source = ipynb_doc_cell_source(doc, i);
        GPtrArray* headings = code_outline_markdown(source, source != NULL ? strlen(source) : 0);
        g_free(source);
        for (guint j = 0; j < headings->len; j++) {
            CodeOutlineEntry* e = g_ptr_array_index(headings, j);
            e->column = e->line;
            e->line = (gint)i;
        }
        g_ptr_array_extend_and_steal(out, headings);
    }
    return out;
}

gboolean ipynb_outline_same(const GPtrArray* a, const GPtrArray* b) {
    guint na = a != NULL ? a->len : 0;
    guint nb = b != NULL ? b->len : 0;
    if (na != nb) return FALSE;
    for (guint i = 0; i < na; i++) {
        const CodeOutlineEntry* x = g_ptr_array_index(a, i);
        const CodeOutlineEntry* y = g_ptr_array_index(b, i);
        if (x->level != y->level || x->line != y->line || x->column != y->column || g_strcmp0(x->name, y->name) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

guint ipynb_outline_cell_level(const IpynbDoc* doc, guint index) {
    if (doc == NULL || ipynb_doc_cell_type(doc, index) != IPYNB_CELL_MARKDOWN) return 0;
    gchar* source = ipynb_doc_cell_source(doc, index);
    GPtrArray* headings = code_outline_markdown(source, source != NULL ? strlen(source) : 0);
    g_free(source);
    guint level = 0;
    for (guint j = 0; j < headings->len; j++) {
        const CodeOutlineEntry* e = g_ptr_array_index(headings, j);
        if (e->level > 0 && (level == 0 || (guint)e->level < level)) level = (guint)e->level;
    }
    g_ptr_array_unref(headings);
    return level;
}

guint ipynb_outline_section_end(const guint* levels, guint n, guint index) {
    if (index >= n) return n;
    guint level = levels[index];
    guint end = index + 1;
    if (level == 0) return end;
    while (end < n && (levels[end] == 0 || levels[end] > level)) end++;
    return end;
}

void ipynb_outline_hidden(const guint* levels, const gboolean* collapsed, guint n, gboolean* hidden) {
    guint i = 0;
    while (i < n) {
        hidden[i] = FALSE;
        if (levels[i] == 0 || !collapsed[i]) {
            i++;
            continue;
        }
        guint end = ipynb_outline_section_end(levels, n, i);
        for (guint j = i + 1; j < end; j++) hidden[j] = TRUE;
        i = end;
    }
}
