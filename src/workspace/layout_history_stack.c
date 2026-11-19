/* Bounded back / forward history stack for workspace layouts (see layout_history_stack.h) */
#include "workspace/layout_history_stack.h"
#include "workspace/tab_tree.h"
#include <math.h>

struct LayoutHistoryStack {
    GPtrArray* entries;
    guint limit;
    guint pos; /* index of the current entry when entries->len > 0 */
};

LayoutHistoryStack* layout_history_stack_new(guint limit, GDestroyNotify free_fn) {
    LayoutHistoryStack* s = g_new0(LayoutHistoryStack, 1);
    s->entries = g_ptr_array_new_with_free_func(free_fn);
    s->limit = MAX(limit, 2u);
    s->pos = 0;
    return s;
}

void layout_history_stack_free(LayoutHistoryStack* stack) {
    if (stack == NULL) {
        return;
    }
    g_ptr_array_unref(stack->entries);
    g_free(stack);
}

void layout_history_stack_clear(LayoutHistoryStack* stack) {
    if (stack == NULL) {
        return;
    }
    g_ptr_array_set_size(stack->entries, 0);
    stack->pos = 0;
}

void layout_history_stack_push(LayoutHistoryStack* stack, gpointer entry) {
    if (stack == NULL || entry == NULL) {
        return;
    }
    if (stack->entries->len > 0 && stack->pos + 1 < stack->entries->len) {
        g_ptr_array_set_size(stack->entries, stack->pos + 1);
    }
    g_ptr_array_add(stack->entries, entry);
    if (stack->entries->len > stack->limit) {
        g_ptr_array_remove_index(stack->entries, 0);
    }
    stack->pos = stack->entries->len - 1;
}

gboolean layout_history_stack_can_back(const LayoutHistoryStack* stack) {
    return stack != NULL && stack->entries->len > 0 && stack->pos > 0;
}

gboolean layout_history_stack_can_forward(const LayoutHistoryStack* stack) {
    return stack != NULL && stack->entries->len > 0 && stack->pos + 1 < stack->entries->len;
}

gpointer layout_history_stack_current(const LayoutHistoryStack* stack) {
    if (stack == NULL || stack->entries->len == 0) {
        return NULL;
    }
    return g_ptr_array_index(stack->entries, stack->pos);
}

gpointer layout_history_stack_back(LayoutHistoryStack* stack) {
    if (!layout_history_stack_can_back(stack)) {
        return NULL;
    }
    stack->pos--;
    return g_ptr_array_index(stack->entries, stack->pos);
}

gpointer layout_history_stack_forward(LayoutHistoryStack* stack) {
    if (!layout_history_stack_can_forward(stack)) {
        return NULL;
    }
    stack->pos++;
    return g_ptr_array_index(stack->entries, stack->pos);
}

guint layout_history_stack_length(const LayoutHistoryStack* stack) {
    return (stack != NULL) ? stack->entries->len : 0;
}

guint layout_history_stack_position(const LayoutHistoryStack* stack) {
    return (stack != NULL && stack->entries->len > 0) ? stack->pos : 0;
}

static gboolean same_frac(gdouble a, gdouble b) {
    return fabs(a - b) < LAYOUT_HISTORY_FRAC_EPS;
}

/* Same terminal pane splits and proportions (tab count inside a leaf ignored) */
static gboolean same_term_tree(const TermLayoutNode* a, const TermLayoutNode* b) {
    if (a == b) {
        return TRUE;
    }
    if (a == NULL || b == NULL) {
        return FALSE;
    }
    if (a->is_terminal != b->is_terminal) {
        return FALSE;
    }
    if (a->is_terminal) {
        return TRUE;
    }
    return a->vertical == b->vertical && same_frac(a->frac, b->frac) &&
           same_term_tree(a->first, b->first) && same_term_tree(a->second, b->second);
}

static gboolean same_term_text(const gchar* a, const gchar* b) {
    gboolean a_empty = (a == NULL || a[0] == '\0');
    gboolean b_empty = (b == NULL || b[0] == '\0');
    if (a_empty || b_empty) {
        return a_empty == b_empty;
    }
    TermLayoutNode* ta = term_layout_parse(a);
    TermLayoutNode* tb = term_layout_parse(b);
    gboolean same = (ta != NULL && tb != NULL) ? same_term_tree(ta, tb)
                                               : g_strcmp0(a, b) == 0;
    term_layout_free(ta);
    term_layout_free(tb);
    return same;
}

/* Same editor pane splits and proportions (contents inside a leaf compared by snapshot) */
static gboolean same_tab_tree(const TabTreeNode* a, const TabTreeNode* b) {
    if (a == b) {
        return TRUE;
    }
    if (a == NULL || b == NULL) {
        return FALSE;
    }
    if (a->is_notebook != b->is_notebook) {
        return FALSE;
    }
    if (a->is_notebook) {
        return TRUE;
    }
    return a->vertical == b->vertical && same_frac(a->frac, b->frac) &&
           same_tab_tree(a->first, b->first) && same_tab_tree(a->second, b->second);
}

static gboolean same_editor_text(const gchar* a, const gchar* b) {
    gboolean a_empty = (a == NULL || a[0] == '\0');
    gboolean b_empty = (b == NULL || b[0] == '\0');
    if (a_empty || b_empty) {
        return a_empty == b_empty;
    }
    TabTreeNode* ta = tab_tree_parse(a);
    TabTreeNode* tb = tab_tree_parse(b);
    gboolean same = (ta != NULL && tb != NULL) ? same_tab_tree(ta, tb)
                                               : g_strcmp0(a, b) == 0;
    tab_tree_free(ta);
    tab_tree_free(tb);
    return same;
}

gboolean layout_history_same_panels(const SavedLayout* a, const SavedLayout* b) {
    if (a == b) {
        return TRUE;
    }
    if (a == NULL || b == NULL) {
        return FALSE;
    }
    if (a->files_vis != b->files_vis ||
        a->preview_vis != b->preview_vis ||
        a->term_vis != b->term_vis ||
        a->editor_dedicated != b->editor_dedicated ||
        a->stacked != b->stacked) {
        return FALSE;
    }
    gboolean center_vis = a->preview_vis || a->editor_dedicated;
    if (a->files_vis && (center_vis || a->term_vis) && !same_frac(a->main_frac, b->main_frac)) {
        return FALSE;
    }
    if (center_vis && a->term_vis && !same_frac(a->right_frac, b->right_frac)) {
        return FALSE;
    }
    if (a->preview_vis && a->editor_dedicated) {
        if (a->editor_left != b->editor_left || !same_frac(a->editor_frac, b->editor_frac)) {
            return FALSE;
        }
    }
    if (a->term_vis && !same_term_text(a->terminals, b->terminals)) {
        return FALSE;
    }
    if (a->editor_dedicated && !same_editor_text(a->editor_splits, b->editor_splits)) {
        return FALSE;
    }
    if (a->tagbars != b->tagbars) {
        return FALSE;
    }
    if (a->tagbars) {
        if (a->preview_tagbar != b->preview_tagbar || a->tagbar_width != b->tagbar_width) {
            return FALSE;
        }
        if ((!a->editor_tagbar_as_is || !b->editor_tagbar_as_is) &&
            a->editor_tagbar != b->editor_tagbar) {
            return FALSE;
        }
    }
    return TRUE;
}
