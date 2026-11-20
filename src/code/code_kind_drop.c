/* Kind drop-down of the symbol pickers (see code_kind_drop.h) */
#include "code/code_kind_drop.h"
#include "code/code_outline_tree.h"
#include "core/i18n.h"
#include <string.h>

/* Kinds offered, in the order of the tagbar groups; row 0 is every kind */
static const CodeSymbolKind KIND_CHOICES[] = {
    CODE_SYM_FUNCTION, CODE_SYM_METHOD, CODE_SYM_MACRO,  CODE_SYM_TYPE,   CODE_SYM_CLASS,
    CODE_SYM_VARIABLE, CODE_SYM_CONSTANT, CODE_SYM_FIELD, CODE_SYM_MODULE, CODE_SYM_TARGET,
};
#define N_KIND_CHOICES G_N_ELEMENTS(KIND_CHOICES)
#define N_ROWS (N_KIND_CHOICES + 1)

struct CodeKindDrop {
    GtkWidget* drop;
    GtkStringList* rows;
    guint counts[CODE_SYM_N_KINDS]; /* matches of each kind, last shown */
    gboolean counted;               /* counts known */
    guint* remembered;
    CodeKindDropChanged changed;
    gpointer data;
};

/* Label of a row: the kind, and how many symbols of it match once known */
static gchar* row_label(CodeKindDrop* kd, guint row) {
    const gchar* name = row == 0 ? _("All kinds") : code_outline_group_label(KIND_CHOICES[row - 1]);
    if (!kd->counted) return g_strdup(name);
    guint n = 0;
    if (row == 0) {
        for (guint k = 0; k < CODE_SYM_N_KINDS; k++) n += kd->counts[k];
    } else {
        n = kd->counts[KIND_CHOICES[row - 1]];
    }
    /* TRANSLATORS: a kind of symbol and how many match, "Functions (12)" */
    return g_strdup_printf(C_("symbol kind count", "%1$s (%2$u)"), name, n);
}

static void on_selected(GObject* drop, GParamSpec* pspec, gpointer data) {
    (void)drop;
    (void)pspec;
    CodeKindDrop* kd = data;
    if (kd->remembered != NULL) *kd->remembered = gtk_drop_down_get_selected(GTK_DROP_DOWN(kd->drop));
    if (kd->changed != NULL) kd->changed(kd->data);
}

CodeKindDrop* code_kind_drop_new(guint* remembered, CodeKindDropChanged changed, gpointer data) {
    CodeKindDrop* kd = g_new0(CodeKindDrop, 1);
    kd->remembered = remembered;
    kd->changed = changed;
    kd->data = data;
    kd->rows = gtk_string_list_new(NULL);
    for (guint i = 0; i < N_ROWS; i++) {
        gchar* label = row_label(kd, i);
        gtk_string_list_append(kd->rows, label);
        g_free(label);
    }
    /* The drop-down takes the list */
    kd->drop = gtk_drop_down_new(G_LIST_MODEL(kd->rows), NULL);
    guint row = remembered != NULL && *remembered < N_ROWS ? *remembered : 0;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(kd->drop), row);
    gtk_widget_set_tooltip_text(kd->drop, _("Kind of symbol (Alt+K / Alt+Shift+K)"));
    gtk_widget_set_focus_on_click(kd->drop, FALSE);
    g_signal_connect(kd->drop, "notify::selected", G_CALLBACK(on_selected), kd);
    g_object_set_data_full(G_OBJECT(kd->drop), "code-kind-drop", kd, g_free);
    return kd;
}

GtkWidget* code_kind_drop_widget(CodeKindDrop* kd) {
    return kd->drop;
}

GtkWidget* code_kind_drop_hint_new(void) {
    GtkWidget* hint = gtk_label_new(_("Alt+K / Alt+Shift+K: next / previous kind"));
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(hint), 1.0f);
    return hint;
}

guint code_kind_drop_kinds(CodeKindDrop* kd) {
    guint row = gtk_drop_down_get_selected(GTK_DROP_DOWN(kd->drop));
    if (row == 0 || row >= N_ROWS) return CODE_SYM_KINDS_ALL;
    return CODE_SYM_KIND_BIT(KIND_CHOICES[row - 1]);
}

void code_kind_drop_set_counts(CodeKindDrop* kd, const guint* counts) {
    memcpy(kd->counts, counts, sizeof(kd->counts));
    kd->counted = TRUE;
    const gchar* labels[N_ROWS + 1];
    gchar* owned[N_ROWS];
    for (guint i = 0; i < N_ROWS; i++) labels[i] = owned[i] = row_label(kd, i);
    labels[N_ROWS] = NULL;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(kd->drop));
    g_signal_handlers_block_by_func(kd->drop, on_selected, kd);
    gtk_string_list_splice(kd->rows, 0, N_ROWS, labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(kd->drop), sel);
    g_signal_handlers_unblock_by_func(kd->drop, on_selected, kd);
    for (guint i = 0; i < N_ROWS; i++) g_free(owned[i]);
}

gboolean code_kind_drop_key(CodeKindDrop* kd, guint keyval, GdkModifierType state) {
    if ((state & (GDK_ALT_MASK | GDK_CONTROL_MASK)) != GDK_ALT_MASK ||
        (keyval != GDK_KEY_k && keyval != GDK_KEY_K)) {
        return FALSE;
    }
    /* Next / previous row, past the kinds no symbol matches (every kind,
     * row 0, is always a stop) */
    guint step = (state & GDK_SHIFT_MASK) != 0 ? N_ROWS - 1 : 1;
    guint row = gtk_drop_down_get_selected(GTK_DROP_DOWN(kd->drop));
    if (row >= N_ROWS) row = 0;
    for (guint i = 0; i < N_ROWS; i++) {
        row = (row + step) % N_ROWS;
        if (row == 0 || !kd->counted || kd->counts[KIND_CHOICES[row - 1]] > 0) break;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(kd->drop), row);
    return TRUE;
}
