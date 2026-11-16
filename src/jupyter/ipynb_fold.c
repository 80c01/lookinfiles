/* Folding in the Jupyter notebook tab (see ipynb_tab_private.h): the
 * sections under the Markdown headings, and the outputs of the code cells.
 * What is folded is kept in the cells' metadata as JupyterLab keeps it
 * (ipynb_doc_cell_flag()), so it is saved with the notebook. */
#include "core/i18n.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_outline.h"
#include "jupyter/ipynb_tab_private.h"
#include "theme/theme.h"

#define FOLD_INDEX_KEY "lookin-ipynb-fold-index" /* on an arrow or a note: its cell */
#define FOLD_ARROW_KEY "lookin-ipynb-fold-arrow" /* on a Markdown cell's view: its section arrow */
#define FOLD_NOTE_KEY "lookin-ipynb-fold-note"   /* on a Markdown cell's view: "n cells hidden" */
#define OUT_HEAD_KEY "lookin-ipynb-out-head"     /* on a code cell's view: the head of its outputs */
#define OUT_ARROW_KEY "lookin-ipynb-out-arrow"   /* on a code cell's view: the arrow of that head */
#define OUT_PROMPT_KEY "lookin-ipynb-out-prompt" /* on a code cell's view: Out [n] in that head */
#define OUT_NOTE_KEY "lookin-ipynb-out-note"     /* on a code cell's view: "n outputs hidden" in it */
#define OUT_AREA_KEY "lookin-ipynb-out-area"     /* on a code cell's view: the head and the outputs */
#define OUT_BOX_KEY "lookin-ipynb-out-box"       /* on a code cell's view: the outputs */
#define OUT_COUNT_KEY "lookin-ipynb-out-count"   /* on a code cell's view: how many outputs it shows */

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    /* The arrows: the icons and colour of the explorer's tree expanders */
    theme_add_css(".ipynb-tab button.ipynb-fold { padding: 0; min-width: 16px; min-height: 16px; border: none;"
                  " background: none; box-shadow: none; color: @fg; }"
                  ".ipynb-tab button.ipynb-fold:hover { color: @fg_bright; }"
                  ".ipynb-tab button.ipynb-fold-note { padding: 2px 10px 4px 10px; border: none; background: none;"
                  " box-shadow: none; color: @fg_muted; font-style: italic; }"
                  ".ipynb-tab button.ipynb-fold-note:hover { color: @fg_text; }"
                  /* The head of the outputs: a strip like the cell's head, a line under it */
                  ".ipynb-tab button.ipynb-out-head { padding: 4px 4px 3px 8px; min-height: 23px; border: none;"
                  " border-radius: 0; border-bottom: 1px solid @border; background: none; box-shadow: none; }"
                  ".ipynb-tab button.ipynb-out-head.ipynb-folded { border-bottom: none; }"
                  ".ipynb-tab .ipynb-out-box { padding: 6px 10px; }"
                  ".ipynb-tab .ipynb-fold-arrow { color: @fg; }"
                  ".ipynb-tab button.ipynb-out-head:hover .ipynb-fold-arrow,"
                  " .ipynb-tab button.ipynb-out-head:hover .ipynb-out-prompt { color: @fg_bright; }"
                  ".ipynb-tab .ipynb-out-note { color: @fg_muted; font-style: italic; }");
}

static GtkWidget* view_at(IpynbTab* t, guint index) {
    return index < t->views->len ? g_ptr_array_index(t->views, index) : NULL;
}

static guint fold_index(GtkWidget* widget) {
    return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), FOLD_INDEX_KEY));
}

/* A flat button for the cell at index: a note (text), or an arrow when
 * text is NULL */
static GtkWidget* fold_button(guint index, const gchar* text, const gchar* css_class) {
    GtkWidget* b = text != NULL ? gtk_button_new_with_label(text) : gtk_button_new();
    if (text == NULL) gtk_button_set_child(GTK_BUTTON(b), gtk_image_new());
    gtk_widget_add_css_class(b, css_class);
    gtk_widget_set_focus_on_click(b, FALSE);
    gtk_widget_set_valign(b, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(b), FOLD_INDEX_KEY, GUINT_TO_POINTER(index));
    return b;
}

/* Point an arrow (a GtkImage, or a button showing one) right (folded) or
 * down, with the icons GTK's tree expanders use (built into GTK; the right
 * one mirrored right to left) */
static void set_arrow(GtkWidget* arrow, gboolean folded) {
    GtkWidget* image = GTK_IS_BUTTON(arrow) ? gtk_button_get_child(GTK_BUTTON(arrow)) : arrow;
    gtk_image_set_from_icon_name(GTK_IMAGE(image), folded ? "pan-end-symbolic" : "pan-down-symbolic");
}

/* --- Sections --- */

/* The level of each cell and which cells are hidden (n_cells of each,
 * caller frees both) */
static void section_state(IpynbTab* t, guint** levels, gboolean** hidden) {
    guint n = ipynb_doc_n_cells(t->doc);
    *levels = g_new0(guint, MAX(n, 1));
    gboolean* collapsed = g_new0(gboolean, MAX(n, 1));
    *hidden = g_new0(gboolean, MAX(n, 1));
    for (guint i = 0; i < n; i++) {
        (*levels)[i] = ipynb_outline_cell_level(t->doc, i);
        collapsed[i] = ipynb_doc_cell_flag(t->doc, i, IPYNB_FLAG_HEADING_COLLAPSED);
    }
    ipynb_outline_hidden(*levels, collapsed, n, *hidden);
    g_free(collapsed);
}

void ipynb_fold_apply(IpynbTab* t) {
    guint n = ipynb_doc_n_cells(t->doc);
    if (t->views->len != n) return; /* being built */
    guint* levels;
    gboolean* hidden;
    section_state(t, &levels, &hidden);
    for (guint i = 0; i < n; i++) {
        GtkWidget* view = view_at(t, i);
        gtk_widget_set_visible(gtk_widget_get_parent(view), !hidden[i]);
        GtkWidget* arrow = g_object_get_data(G_OBJECT(view), FOLD_ARROW_KEY);
        GtkWidget* note = g_object_get_data(G_OBJECT(view), FOLD_NOTE_KEY);
        if (arrow == NULL || note == NULL) continue;
        gboolean folded = levels[i] > 0 && ipynb_doc_cell_flag(t->doc, i, IPYNB_FLAG_HEADING_COLLAPSED);
        gtk_widget_set_visible(arrow, levels[i] > 0);
        set_arrow(arrow, folded);
        gtk_widget_set_tooltip_text(arrow, folded ? _("Unfold Section (Tab)") : _("Fold Section (Tab)"));
        guint count = folded ? ipynb_outline_section_end(levels, n, i) - i - 1 : 0;
        gtk_widget_set_visible(note, count > 0);
        if (count > 0) {
            gchar* text = g_strdup_printf(ngettext("%u cell hidden", "%u cells hidden", count), count);
            gtk_button_set_label(GTK_BUTTON(note), text);
            g_free(text);
        }
    }
    g_free(levels);
    g_free(hidden);
}

static gboolean cell_hidden(IpynbTab* t, guint index) {
    GtkWidget* view = view_at(t, index);
    return view != NULL && !gtk_widget_get_visible(gtk_widget_get_parent(view));
}

static void set_collapsed(IpynbTab* t, guint index, gboolean on) {
    if (ipynb_doc_set_cell_flag(t->doc, index, IPYNB_FLAG_HEADING_COLLAPSED, on)) ipynb_tab_mark_changed(t);
}

void ipynb_fold_toggle_section(IpynbTab* t, guint index) {
    if (ipynb_outline_cell_level(t->doc, index) == 0) return;
    set_collapsed(t, index, !ipynb_doc_cell_flag(t->doc, index, IPYNB_FLAG_HEADING_COLLAPSED));
    ipynb_fold_apply(t);
    /* The current cell folded away: the heading takes the focus */
    if (cell_hidden(t, t->current)) ipynb_tab_focus_cell(t, index);
}

void ipynb_fold_cycle_all(IpynbTab* t) {
    guint n = ipynb_doc_n_cells(t->doc);
    gboolean all = TRUE;
    gboolean any = FALSE;
    for (guint i = 0; i < n; i++) {
        if (ipynb_outline_cell_level(t->doc, i) == 0) continue;
        any = TRUE;
        if (!ipynb_doc_cell_flag(t->doc, i, IPYNB_FLAG_HEADING_COLLAPSED)) all = FALSE;
    }
    if (!any) return;
    for (guint i = 0; i < n; i++) {
        if (ipynb_outline_cell_level(t->doc, i) > 0) set_collapsed(t, i, !all);
    }
    ipynb_fold_apply(t);
    /* Folded: the current cell goes to the heading still shown above it */
    guint at = t->current;
    while (at > 0 && cell_hidden(t, at)) at--;
    if (at != t->current) ipynb_tab_focus_cell(t, at);
}

gboolean ipynb_fold_reveal(IpynbTab* t, guint index) {
    guint n = ipynb_doc_n_cells(t->doc);
    if (index >= n || !cell_hidden(t, index)) return FALSE;
    guint* levels;
    gboolean* hidden;
    section_state(t, &levels, &hidden);
    for (guint i = 0; i < index; i++) {
        if (levels[i] > 0 && ipynb_outline_section_end(levels, n, i) > index) set_collapsed(t, i, FALSE);
    }
    g_free(levels);
    g_free(hidden);
    ipynb_fold_apply(t);
    return TRUE;
}

guint ipynb_fold_step(IpynbTab* t, guint index, gint step) {
    guint n = t->views->len;
    for (gint i = (gint)index + step; i >= 0 && i < (gint)n; i += step) {
        if (!cell_hidden(t, (guint)i)) return (guint)i;
    }
    return index;
}

static void on_section_clicked(GtkButton* button, gpointer data) {
    ipynb_fold_toggle_section(data, fold_index(GTK_WIDGET(button)));
}

void ipynb_fold_add_heading(IpynbTab* t, guint index, GtkWidget* view, GtkWidget* head, GtkWidget* cell) {
    ensure_css();
    GtkWidget* arrow = fold_button(index, NULL, "ipynb-fold");
    gtk_widget_set_valign(arrow, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(arrow, FALSE);
    g_signal_connect(arrow, "clicked", G_CALLBACK(on_section_clicked), t);
    gtk_box_prepend(GTK_BOX(head), arrow);
    GtkWidget* note = fold_button(index, "", "ipynb-fold-note");
    gtk_widget_set_halign(note, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(note, _("Unfold Section (Tab)"));
    gtk_widget_set_visible(note, FALSE);
    g_signal_connect(note, "clicked", G_CALLBACK(on_section_clicked), t);
    gtk_box_append(GTK_BOX(cell), note);
    g_object_set_data(G_OBJECT(view), FOLD_ARROW_KEY, arrow);
    g_object_set_data(G_OBJECT(view), FOLD_NOTE_KEY, note);
}

/* --- Outputs ---
 * A head over the outputs of a code cell, "▾ Out [n]:", folds them: then
 * it reads "▸ Out [n]: 3 outputs hidden" and the outputs are hidden. */

void ipynb_fold_refresh_outputs(IpynbTab* t, guint index, guint n_outputs) {
    GtkWidget* view = view_at(t, index);
    GtkWidget* area = view != NULL ? g_object_get_data(G_OBJECT(view), OUT_AREA_KEY) : NULL;
    if (area == NULL) return;
    GtkWidget* head = g_object_get_data(G_OBJECT(view), OUT_HEAD_KEY);
    GtkWidget* arrow = g_object_get_data(G_OBJECT(view), OUT_ARROW_KEY);
    GtkWidget* prompt = g_object_get_data(G_OBJECT(view), OUT_PROMPT_KEY);
    GtkWidget* box = g_object_get_data(G_OBJECT(view), OUT_BOX_KEY);
    GtkWidget* note = g_object_get_data(G_OBJECT(view), OUT_NOTE_KEY);
    gboolean folded = ipynb_doc_cell_flag(t->doc, index, IPYNB_FLAG_OUTPUTS_HIDDEN);
    gtk_widget_set_visible(area, n_outputs > 0);
    gtk_widget_set_visible(box, !folded);
    gtk_widget_set_visible(note, folded);
    if (folded) gtk_widget_add_css_class(head, "ipynb-folded");
    else gtk_widget_remove_css_class(head, "ipynb-folded");
    set_arrow(arrow, folded);
    gtk_widget_set_tooltip_text(head, folded ? _("Show Outputs") : _("Hide Outputs"));
    gint count = ipynb_doc_cell_execution_count(t->doc, index);
    gchar* text = count >= 0 ? g_strdup_printf("Out [%d]:", count) : g_strdup("Out:");
    gtk_label_set_text(GTK_LABEL(prompt), text);
    g_free(text);
    if (folded) {
        text = g_strdup_printf(ngettext("%u output hidden", "%u outputs hidden", n_outputs), n_outputs);
        gtk_label_set_text(GTK_LABEL(note), text);
        g_free(text);
    }
    g_object_set_data(G_OBJECT(view), OUT_COUNT_KEY, GUINT_TO_POINTER(n_outputs));
}

static void on_outputs_clicked(GtkButton* button, gpointer data) {
    IpynbTab* t = data;
    guint index = fold_index(GTK_WIDGET(button));
    gboolean folded = ipynb_doc_cell_flag(t->doc, index, IPYNB_FLAG_OUTPUTS_HIDDEN);
    if (!ipynb_doc_set_cell_flag(t->doc, index, IPYNB_FLAG_OUTPUTS_HIDDEN, !folded)) return;
    ipynb_tab_mark_changed(t);
    guint count = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(view_at(t, index)), OUT_COUNT_KEY));
    ipynb_fold_refresh_outputs(t, index, count);
}

GtkWidget* ipynb_fold_outputs_wrap(IpynbTab* t, guint index, GtkWidget* view, GtkWidget* outputs) {
    ensure_css();
    GtkWidget* area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(area, "ipynb-outputs");
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(row, GTK_ALIGN_START);
    gtk_widget_set_valign(row, GTK_ALIGN_CENTER);
    GtkWidget* arrow = gtk_image_new();
    gtk_widget_add_css_class(arrow, "ipynb-fold-arrow");
    gtk_box_append(GTK_BOX(row), arrow);
    GtkWidget* prompt = gtk_label_new(NULL);
    gtk_widget_add_css_class(prompt, "ipynb-out-prompt");
    gtk_box_append(GTK_BOX(row), prompt);
    GtkWidget* note = gtk_label_new(NULL);
    gtk_widget_add_css_class(note, "ipynb-out-note");
    gtk_box_append(GTK_BOX(row), note);
    GtkWidget* head = fold_button(index, NULL, "ipynb-out-head");
    gtk_button_set_child(GTK_BUTTON(head), row);
    g_signal_connect(head, "clicked", G_CALLBACK(on_outputs_clicked), t);
    gtk_box_append(GTK_BOX(area), head);
    gtk_widget_add_css_class(outputs, "ipynb-out-box");
    gtk_box_append(GTK_BOX(area), outputs);
    g_object_set_data(G_OBJECT(view), OUT_AREA_KEY, area);
    g_object_set_data(G_OBJECT(view), OUT_HEAD_KEY, head);
    g_object_set_data(G_OBJECT(view), OUT_ARROW_KEY, arrow);
    g_object_set_data(G_OBJECT(view), OUT_PROMPT_KEY, prompt);
    g_object_set_data(G_OBJECT(view), OUT_BOX_KEY, outputs);
    g_object_set_data(G_OBJECT(view), OUT_NOTE_KEY, note);
    return area;
}
