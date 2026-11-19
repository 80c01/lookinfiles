#ifndef LAYOUT_HISTORY_STACK_H
#define LAYOUT_HISTORY_STACK_H

/* Bounded back / forward history stack for workspace layouts and layout
 * comparison helper (used by layout_history.c). Pure logic, no widgets. */
#include "workspace/layout_store.h"

/* Minimum split-fraction change that counts as a new layout step */
#define LAYOUT_HISTORY_FRAC_EPS 0.005

typedef struct LayoutHistoryStack LayoutHistoryStack;

/* limit: maximum entries kept (at least 2; oldest entries are dropped when full).
 * free_fn: frees an entry (may be NULL). */
LayoutHistoryStack* layout_history_stack_new(guint limit, GDestroyNotify free_fn);
void layout_history_stack_free(LayoutHistoryStack* stack);
void layout_history_stack_clear(LayoutHistoryStack* stack);

/* Append entry after the current position, dropping any forward entries.
 * Takes ownership of entry. */
void layout_history_stack_push(LayoutHistoryStack* stack, gpointer entry);

gboolean layout_history_stack_can_back(const LayoutHistoryStack* stack);
gboolean layout_history_stack_can_forward(const LayoutHistoryStack* stack);

/* Current entry, or NULL if empty */
gpointer layout_history_stack_current(const LayoutHistoryStack* stack);
/* Step back / forward and return the entry now current, or NULL if at the end */
gpointer layout_history_stack_back(LayoutHistoryStack* stack);
gpointer layout_history_stack_forward(LayoutHistoryStack* stack);

guint layout_history_stack_length(const LayoutHistoryStack* stack);
guint layout_history_stack_position(const LayoutHistoryStack* stack);

/* TRUE when a and b represent the same visible panel arrangement (ignoring
 * split fractions of hidden dividers, tab counts within the same pane, and
 * sub-epsilon allocation jitter) */
gboolean layout_history_same_panels(const SavedLayout* a, const SavedLayout* b);

#endif /* LAYOUT_HISTORY_STACK_H */
