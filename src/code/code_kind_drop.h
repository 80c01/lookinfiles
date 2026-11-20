#ifndef CODE_KIND_DROP_H
#define CODE_KIND_DROP_H

/* Drop-down of the symbol pickers (docs/code-nav.md): every kind, or one
 * kind (functions, macros, types...), each row with how many symbols of it
 * match; Alt+K / Alt+Shift+K step through the kinds that have some. */
#include <gtk/gtk.h>
#include "code/code_symbol.h"

typedef struct CodeKindDrop CodeKindDrop;

/* Called when the kind chosen changes (by the user or code_kind_drop_key()) */
typedef void (*CodeKindDropChanged)(gpointer data);

/* A new drop-down on the row *remembered (kept there for the next picker,
 * may be NULL); it lives as long as its widget */
CodeKindDrop* code_kind_drop_new(guint* remembered, CodeKindDropChanged changed, gpointer data);
GtkWidget* code_kind_drop_widget(CodeKindDrop* kd);
/* A dim label naming the keys, for the foot of the dialog */
GtkWidget* code_kind_drop_hint_new(void);

/* Mask of the kinds chosen (CODE_SYM_KINDS_ALL for every kind) */
guint code_kind_drop_kinds(CodeKindDrop* kd);
/* Shows counts (CODE_SYM_N_KINDS entries) in the rows, the row chosen kept */
void code_kind_drop_set_counts(CodeKindDrop* kd, const guint* counts);
/* Alt+K / Alt+Shift+K: TRUE when the key was taken */
gboolean code_kind_drop_key(CodeKindDrop* kd, guint keyval, GdkModifierType state);

#endif /* CODE_KIND_DROP_H */
