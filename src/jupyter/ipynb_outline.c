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
