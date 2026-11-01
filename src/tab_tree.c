/* The editor tabs of a workspace as text (see tab_tree.h) */
#include "tab_tree.h"

TabTreeNode* tab_tree_notebook_new(void) {
    TabTreeNode* n = g_new0(TabTreeNode, 1);
    n->is_notebook = TRUE;
    n->tabs = g_array_new(FALSE, FALSE, sizeof(guint));
    n->current = -1;
    n->focused = -1;
    return n;
}

guint tab_tree_notebook_add(TabTreeNode* notebook, guint file) {
    g_array_append_val(notebook->tabs, file);
    return notebook->tabs->len - 1;
}

TabTreeNode* tab_tree_split_new(gboolean vertical, gdouble frac, TabTreeNode* first, TabTreeNode* second) {
    if (first == NULL || second == NULL) {
        return first != NULL ? first : second;
    }
    TabTreeNode* n = g_new0(TabTreeNode, 1);
    n->vertical = vertical;
    n->frac = CLAMP(frac, 0.05, 0.95);
    n->first = first;
    n->second = second;
    return n;
}

void tab_tree_free(TabTreeNode* node) {
    if (node == NULL) {
        return;
    }
    if (node->tabs != NULL) {
        g_array_free(node->tabs, TRUE);
    }
    tab_tree_free(node->first);
    tab_tree_free(node->second);
    g_free(node);
}

guint tab_tree_notebook_count(const TabTreeNode* node) {
    if (node == NULL) {
        return 0;
    }
    return node->is_notebook ? 1 : tab_tree_notebook_count(node->first) + tab_tree_notebook_count(node->second);
}

guint tab_tree_terminal_count(const TabTreeNode* node) {
    if (node == NULL) {
        return 0;
    }
    if (!node->is_notebook) {
        return tab_tree_terminal_count(node->first) + tab_tree_terminal_count(node->second);
    }
    guint n = 0;
    for (guint i = 0; i < node->tabs->len; i++) {
        n += (g_array_index(node->tabs, guint, i) == TAB_TREE_TERMINAL) ? 1 : 0;
    }
    return n;
}

static TabTreeNode* parse_notebook(const gchar** p) {
    (*p)++; /* '[' */
    TabTreeNode* n = tab_tree_notebook_new();
    while (**p != ']') {
        if (n->tabs->len > 0) {
            if (**p != ',') {
                tab_tree_free(n);
                return NULL;
            }
            (*p)++;
        }
        if ((!g_ascii_isdigit(**p) && **p != 't') || n->tabs->len >= TAB_TREE_MAX_TABS) {
            tab_tree_free(n);
            return NULL;
        }
        guint tab = TAB_TREE_TERMINAL;
        if (**p == 't') {
            (*p)++;
        } else {
            gchar* end = NULL;
            guint64 v = g_ascii_strtoull(*p, &end, 10);
            if (v > G_MAXINT) {
                tab_tree_free(n);
                return NULL;
            }
            *p = end;
            tab = (guint)v;
        }
        gint pos = (gint)tab_tree_notebook_add(n, tab);
        for (; **p == '*' || **p == '!'; (*p)++) {
            if (**p == '*') {
                n->current = pos;
            } else {
                n->focused = pos;
            }
        }
    }
    (*p)++; /* ']' */
    if (n->tabs->len == 0) {
        tab_tree_free(n);
        return NULL;
    }
    return n;
}

static TabTreeNode* parse_node(const gchar** p, gint depth) {
    if (depth > 16) {
        return NULL;
    }
    if (**p == '[') {
        return parse_notebook(p);
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
    TabTreeNode* first = parse_node(p, depth + 1);
    if (first == NULL || **p != ',') {
        tab_tree_free(first);
        return NULL;
    }
    (*p)++;
    TabTreeNode* second = parse_node(p, depth + 1);
    if (second == NULL || **p != ')') {
        tab_tree_free(first);
        tab_tree_free(second);
        return NULL;
    }
    (*p)++;
    return tab_tree_split_new(vertical, frac, first, second);
}

TabTreeNode* tab_tree_parse(const gchar* text) {
    if (text == NULL || text[0] == '\0') {
        return NULL;
    }
    const gchar* p = text;
    TabTreeNode* n = parse_node(&p, 0);
    if (n != NULL && *p != '\0') {
        tab_tree_free(n);
        return NULL;
    }
    return n;
}

static void append_node(GString* s, const TabTreeNode* n) {
    if (n->is_notebook) {
        g_string_append_c(s, '[');
        for (guint i = 0; i < n->tabs->len; i++) {
            guint tab = g_array_index(n->tabs, guint, i);
            if (i > 0) {
                g_string_append_c(s, ',');
            }
            if (tab == TAB_TREE_TERMINAL) {
                g_string_append_c(s, 't');
            } else {
                g_string_append_printf(s, "%u", tab);
            }
            g_string_append_printf(s, "%s%s", (gint)i == n->current ? "*" : "", (gint)i == n->focused ? "!" : "");
        }
        g_string_append_c(s, ']');
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

gchar* tab_tree_to_string(const TabTreeNode* node) {
    GString* s = g_string_new(NULL);
    if (node != NULL) {
        append_node(s, node);
    }
    return g_string_free(s, FALSE);
}
