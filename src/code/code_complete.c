/* In-editor autocompletion popover (see code_complete.h) */
#include "code/code_complete.h"
#include "code/code_intel.h"
#include "code/code_nav.h"
#include "code/code_symbol.h"
#include "code/lsp_edit.h"
#include "theme/palette.h"
#include "theme/theme.h"
#include <string.h>

#define COMPLETE_KEY     "code-complete-state"
#define AUTO_DELAY_MS    150
#define TRIGGER_DELAY_MS 40
#define MAX_COMP_ITEMS   60

#define CODE_TYPE_COMP_ITEM (code_comp_item_get_type())
G_DECLARE_FINAL_TYPE(CodeCompItem, code_comp_item, CODE, COMP_ITEM, GObject)

struct _CodeCompItem {
    GObject parent;
    gchar* label;
    gchar* detail;
    gchar* documentation;
    gchar* insert_text;
    gchar* filter_text;
    gchar* sort_text;
    CodeSymbolKind kind;
    gboolean has_edit;
    gint edit_start_line, edit_start_col;
    gint edit_end_line, edit_end_col;
    gchar* edit_text;
};

G_DEFINE_FINAL_TYPE(CodeCompItem, code_comp_item, G_TYPE_OBJECT)

static void code_comp_item_finalize(GObject* obj) {
    CodeCompItem* it = CODE_COMP_ITEM(obj);
    g_free(it->label);
    g_free(it->detail);
    g_free(it->documentation);
    g_free(it->insert_text);
    g_free(it->filter_text);
    g_free(it->sort_text);
    g_free(it->edit_text);
    G_OBJECT_CLASS(code_comp_item_parent_class)->finalize(obj);
}

static void code_comp_item_class_init(CodeCompItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = code_comp_item_finalize;
}

static void code_comp_item_init(CodeCompItem* it) {
    (void)it;
}

typedef struct {
    AppData* app;
    EditorTab* tab;
    CodeCompleteCanAutoFunc can_auto;
    GtkWidget* popover;
    GtkWidget* list;
    GtkWidget* doc_label;
    GListStore* store;
    GtkSingleSelection* selection;
    GPtrArray* all_items; /* owned CodeCompItem* (raw unfiltered set) */
    gboolean incomplete;
    gboolean visible;
    gboolean applying;
    gint anchor_line;
    gint prefix_start_col; /* byte offset of prefix start on anchor_line */
    guint timer_id;
    gchar* pending_trigger;
    gint64 lsp_req_id;
    guint serial;
} CodeComplete;

static CodeComplete* comp_of(EditorTab* tab) {
    return (tab != NULL && tab->view != NULL)
               ? g_object_get_data(G_OBJECT(tab->view), COMPLETE_KEY)
               : NULL;
}

static const gchar* kind_short(CodeSymbolKind k) {
    switch (k) {
    case CODE_SYM_FUNCTION: return "fn";
    case CODE_SYM_METHOD:   return "mtd";
    case CODE_SYM_MACRO:    return "mac";
    case CODE_SYM_TYPE:     return "typ";
    case CODE_SYM_CLASS:    return "cls";
    case CODE_SYM_VARIABLE: return "var";
    case CODE_SYM_CONSTANT: return "cst";
    case CODE_SYM_FIELD:    return "fld";
    case CODE_SYM_MODULE:   return "mod";
    case CODE_SYM_TARGET:   return "tgt";
    case CODE_SYM_LABEL:    return "lbl";
    case CODE_SYM_REFERENCE: return "bib";
    default:                return "sym";
    }
}

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    static const gchar* const CSS =
        "popover.code-complete-popover > contents {\n"
        "  padding: 2px;\n"
        "  background-color: @bg_raised;\n"
        "  border: 1px solid @border_strong;\n"
        "}\n"
        ".code-complete-kind {\n"
        "  font-family: @font_code;\n"
        "  font-size: @size_small;\n"
        "  color: @accent_fg;\n"
        "  background-color: @bg_block;\n"
        "  border-radius: 3px;\n"
        "  padding: 0px 4px;\n"
        "}\n"
        ".code-complete-doc {\n"
        "  font-size: @size_small;\n"
        "  color: @fg_muted;\n"
        "  border-top: 1px solid @border;\n"
        "  padding: 4px 6px;\n"
        "}\n";
    theme_add_css(CSS);
}

static void cancel_pending(CodeComplete* cc) {
    g_clear_handle_id(&cc->timer_id, g_source_remove);
    g_clear_pointer(&cc->pending_trigger, g_free);
    if (cc->lsp_req_id > 0) {
        code_intel_cancel(cc->app, cc->tab, cc->lsp_req_id);
        cc->lsp_req_id = 0;
    }
}

void code_complete_hide(EditorTab* tab) {
    CodeComplete* cc = comp_of(tab);
    if (cc == NULL) return;
    cancel_pending(cc);
    cc->serial++;
    if (cc->visible && cc->popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(cc->popover));
    }
    cc->visible = FALSE;
}

static gchar* current_prefix(EditorTab* tab, gint* out_line, gint* out_start_col, gint* out_cur_col) {
    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cur, gtk_text_buffer_get_insert(tab->buffer));
    gint line = gtk_text_iter_get_line(&cur);
    gint col = gtk_text_iter_get_line_index(&cur);
    GtkTextIter ls = cur;
    gtk_text_iter_set_line_offset(&ls, 0);
    gchar* text = gtk_text_iter_get_slice(&ls, &cur);
    CodeLang lang = tab->path != NULL ? code_lang_from_name(tab->path) : CODE_LANG_C;
    if (lang == CODE_LANG_NONE) lang = CODE_LANG_C;

    gint start = col;
    while (start > 0 && code_is_word_char(lang, (guchar)text[start - 1])) start--;
    gchar* pfx = g_strndup(text + start, (gsize)(col - start));
    g_free(text);
    if (out_line != NULL) *out_line = line;
    if (out_start_col != NULL) *out_start_col = start;
    if (out_cur_col != NULL) *out_cur_col = col;
    return pfx;
}

static void update_doc_footer(CodeComplete* cc) {
    if (cc->doc_label == NULL) return;
    CodeCompItem* it = gtk_single_selection_get_selected_item(cc->selection);
    if (it != NULL && it->documentation != NULL && it->documentation[0] != '\0') {
        gchar* flat = g_strdup(it->documentation);
        for (gchar* p = flat; *p != '\0'; p++) {
            if (*p == '\n' || *p == '\r') *p = ' ';
        }
        g_strstrip(flat);
        gtk_label_set_text(GTK_LABEL(cc->doc_label), flat);
        gtk_widget_set_visible(cc->doc_label, flat[0] != '\0');
        g_free(flat);
    } else {
        gtk_widget_set_visible(cc->doc_label, FALSE);
    }
}

static void on_selection_changed(GtkSingleSelection* sel, GParamSpec* ps, gpointer data) {
    (void)sel; (void)ps;
    update_doc_footer(data);
}

static void apply_selected(CodeComplete* cc) {
    if (cc == NULL || !cc->visible || cc->tab == NULL || cc->tab->buffer == NULL) return;
    CodeCompItem* it = gtk_single_selection_get_selected_item(cc->selection);
    if (it == NULL) {
        code_complete_hide(cc->tab);
        return;
    }

    GtkTextBuffer* buf = cc->tab->buffer;
    cc->applying = TRUE;
    gtk_text_buffer_begin_user_action(buf);

    if (it->has_edit && it->edit_text != NULL) {
        gint lines = gtk_text_buffer_get_line_count(buf);
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_line(buf, &s, CLAMP(it->edit_start_line, 0, lines - 1));
        GtkTextIter seol = s;
        if (!gtk_text_iter_ends_line(&seol)) gtk_text_iter_forward_to_line_end(&seol);
        gtk_text_iter_set_line_index(&s, CLAMP(it->edit_start_col, 0, gtk_text_iter_get_line_index(&seol)));

        gtk_text_buffer_get_iter_at_line(buf, &e, CLAMP(it->edit_end_line, 0, lines - 1));
        GtkTextIter eeol = e;
        if (!gtk_text_iter_ends_line(&eeol)) gtk_text_iter_forward_to_line_end(&eeol);
        /* Extend end to current cursor if user typed extra characters since request */
        GtkTextIter cur;
        gtk_text_buffer_get_iter_at_mark(buf, &cur, gtk_text_buffer_get_insert(buf));
        gtk_text_iter_set_line_index(&e, CLAMP(it->edit_end_col, 0, gtk_text_iter_get_line_index(&eeol)));
        if (gtk_text_iter_get_line(&cur) == gtk_text_iter_get_line(&e) && gtk_text_iter_compare(&cur, &e) > 0) {
            e = cur;
        }
        gtk_text_buffer_delete(buf, &s, &e);
        gtk_text_buffer_insert(buf, &s, it->edit_text, -1);
    } else {
        gint line = 0, pfx_start = 0, cur_col = 0;
        gchar* pfx = current_prefix(cc->tab, &line, &pfx_start, &cur_col);
        g_free(pfx);
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_line(buf, &s, line);
        gtk_text_iter_set_line_index(&s, pfx_start);
        gtk_text_buffer_get_iter_at_mark(buf, &e, gtk_text_buffer_get_insert(buf));
        const gchar* ins = (it->insert_text != NULL && it->insert_text[0] != '\0') ? it->insert_text : it->label;
        gtk_text_buffer_delete(buf, &s, &e);
        gtk_text_buffer_insert(buf, &s, ins, -1);
    }

    gtk_text_buffer_end_user_action(buf);
    cc->applying = FALSE;
    code_complete_hide(cc->tab);
}

static void on_row_activate(GtkListView* view, guint pos, gpointer data) {
    (void)view;
    CodeComplete* cc = data;
    gtk_single_selection_set_selected(cc->selection, pos);
    apply_selected(cc);
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f; (void)data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 1);
    gtk_widget_set_margin_bottom(box, 1);

    GtkWidget* kind = gtk_label_new(NULL);
    gtk_widget_add_css_class(kind, "code-complete-kind");
    gtk_label_set_width_chars(GTK_LABEL(kind), 3);
    gtk_box_append(GTK_BOX(box), kind);

    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* detail = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(detail), 1.0f);
    gtk_label_set_ellipsize(GTK_LABEL(detail), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(detail), 26);
    gtk_box_append(GTK_BOX(box), detail);

    gtk_list_item_set_child(item, box);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f; (void)data;
    CodeCompItem* it = gtk_list_item_get_item(item);
    GtkWidget* box = gtk_list_item_get_child(item);
    GtkWidget* kind = gtk_widget_get_first_child(box);
    GtkWidget* label = gtk_widget_get_next_sibling(kind);
    GtkWidget* detail = gtk_widget_get_last_child(box);

    gtk_label_set_text(GTK_LABEL(kind), kind_short(it->kind));
    gtk_label_set_text(GTK_LABEL(label), it->label != NULL ? it->label : "");
    if (it->detail != NULL && it->detail[0] != '\0') {
        gchar* m = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>",
                                           palette_color(PAL_FG_SUBTLE), it->detail);
        gtk_label_set_markup(GTK_LABEL(detail), m);
        g_free(m);
        gtk_widget_set_visible(detail, TRUE);
    } else {
        gtk_widget_set_visible(detail, FALSE);
    }
}

static void ensure_popover(CodeComplete* cc) {
    if (cc->popover != NULL) return;
    ensure_css();

    cc->popover = gtk_popover_new();
    gtk_widget_add_css_class(cc->popover, "code-complete-popover");
    gtk_popover_set_autohide(GTK_POPOVER(cc->popover), FALSE);
    gtk_popover_set_has_arrow(GTK_POPOVER(cc->popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(cc->popover), GTK_POS_BOTTOM);
    gtk_widget_set_can_focus(cc->popover, FALSE);
    gtk_widget_set_parent(cc->popover, cc->tab->container);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    cc->store = g_list_store_new(CODE_TYPE_COMP_ITEM);
    cc->selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(cc->store)));
    g_signal_connect(cc->selection, "notify::selected-item", G_CALLBACK(on_selection_changed), cc);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), NULL);

    cc->list = gtk_list_view_new(GTK_SELECTION_MODEL(cc->selection), factory);
    gtk_widget_set_can_focus(cc->list, FALSE);
    g_signal_connect(cc->list, "activate", G_CALLBACK(on_row_activate), cc);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 240);
    gtk_widget_set_size_request(scroll, 360, -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), cc->list);
    gtk_box_append(GTK_BOX(vbox), scroll);

    cc->doc_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(cc->doc_label, "code-complete-doc");
    gtk_label_set_xalign(GTK_LABEL(cc->doc_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(cc->doc_label), TRUE);
    gtk_label_set_lines(GTK_LABEL(cc->doc_label), 2);
    gtk_label_set_ellipsize(GTK_LABEL(cc->doc_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(cc->doc_label), 50);
    gtk_widget_set_visible(cc->doc_label, FALSE);
    gtk_box_append(GTK_BOX(vbox), cc->doc_label);

    gtk_popover_set_child(GTK_POPOVER(cc->popover), vbox);
}

static void position_and_show(CodeComplete* cc) {
    if (cc->tab == NULL || cc->tab->view == NULL || cc->tab->container == NULL) return;
    GtkTextView* view = GTK_TEXT_VIEW(cc->tab->view);
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(cc->tab->buffer, &it, gtk_text_buffer_get_insert(cc->tab->buffer));
    GdkRectangle r;
    gtk_text_view_get_iter_location(view, &it, &r);
    gint wx = 0, wy = 0;
    gtk_text_view_buffer_to_window_coords(view, GTK_TEXT_WINDOW_WIDGET, r.x, r.y, &wx, &wy);
    graphene_point_t in_view = GRAPHENE_POINT_INIT((float)wx, (float)wy), in_parent;
    if (!gtk_widget_compute_point(cc->tab->view, cc->tab->container, &in_view, &in_parent)) {
        code_complete_hide(cc->tab);
        return;
    }
    GdkRectangle at = {(gint)in_parent.x, (gint)in_parent.y, MAX(r.width, 1), MAX(r.height, 1)};
    gtk_popover_set_pointing_to(GTK_POPOVER(cc->popover), &at);
    if (!cc->visible) {
        cc->visible = TRUE;
        gtk_popover_popup(GTK_POPOVER(cc->popover));
    }
}

typedef struct {
    CodeCompItem* item;
    gint rank;
} CompRanked;

static gint cmp_comp_ranked(gconstpointer a, gconstpointer b) {
    const CompRanked* x = a;
    const CompRanked* y = b;
    if (x->rank != y->rank) return x->rank - y->rank;
    const gchar* sx = x->item->sort_text != NULL ? x->item->sort_text : x->item->label;
    const gchar* sy = y->item->sort_text != NULL ? y->item->sort_text : y->item->label;
    return g_strcmp0(sx, sy);
}

static void refilter_and_present(CodeComplete* cc, const gchar* prefix) {
    if (cc->all_items == NULL || cc->all_items->len == 0) {
        code_complete_hide(cc->tab);
        return;
    }
    ensure_popover(cc);
    gsize plen = prefix != NULL ? strlen(prefix) : 0;
    gchar* plow = g_ascii_strdown(prefix != NULL ? prefix : "", (gssize)plen);
    GArray* ranked = g_array_new(FALSE, FALSE, sizeof(CompRanked));

    for (guint i = 0; i < cc->all_items->len; i++) {
        CodeCompItem* it = g_ptr_array_index(cc->all_items, i);
        const gchar* f = (it->filter_text != NULL && it->filter_text[0] != '\0') ? it->filter_text : it->label;
        gint rank = -1;
        if (plen == 0) {
            rank = 0;
        } else if (strncmp(f, prefix, plen) == 0) {
            rank = 0;
        } else {
            gchar* flow = g_ascii_strdown(f, -1);
            if (strncmp(flow, plow, plen) == 0) rank = 1;
            else if (strstr(flow, plow) != NULL) rank = 2;
            g_free(flow);
        }
        if (rank >= 0) {
            CompRanked r = {.item = it, .rank = rank};
            g_array_append_val(ranked, r);
        }
    }
    g_free(plow);

    if (ranked->len == 0) {
        g_array_free(ranked, TRUE);
        code_complete_hide(cc->tab);
        return;
    }

    g_array_sort(ranked, cmp_comp_ranked);
    g_list_store_remove_all(cc->store);
    guint take = MIN(ranked->len, (guint)MAX_COMP_ITEMS);
    for (guint i = 0; i < take; i++) {
        g_list_store_append(cc->store, g_array_index(ranked, CompRanked, i).item);
    }
    g_array_free(ranked, TRUE);

    gtk_single_selection_set_selected(cc->selection, 0);
    update_doc_footer(cc);
    position_and_show(cc);
}

static void show_buffer_fallback(CodeComplete* cc, const gchar* prefix) {
    if (cc->tab == NULL || cc->tab->buffer == NULL) return;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(cc->tab->buffer, &s, &e);
    gchar* text = gtk_text_buffer_get_text(cc->tab->buffer, &s, &e, TRUE);
    CodeLang lang = cc->tab->path != NULL ? code_lang_from_name(cc->tab->path) : CODE_LANG_C;
    if (lang == CODE_LANG_NONE) lang = CODE_LANG_C;
    GPtrArray* words = lsp_edit_buffer_identifiers(text, strlen(text), lang, prefix, MAX_COMP_ITEMS);
    g_free(text);

    g_clear_pointer(&cc->all_items, g_ptr_array_unref);
    cc->all_items = g_ptr_array_new_with_free_func(g_object_unref);
    cc->incomplete = FALSE;
    for (guint i = 0; i < words->len; i++) {
        CodeCompItem* it = g_object_new(CODE_TYPE_COMP_ITEM, NULL);
        it->label = g_strdup(g_ptr_array_index(words, i));
        it->kind = CODE_SYM_VARIABLE;
        g_ptr_array_add(cc->all_items, it);
    }
    g_ptr_array_unref(words);
    refilter_and_present(cc, prefix);
}

typedef struct {
    CodeComplete* cc;
    guint serial;
    gboolean manual;
} CompReq;

static void on_lsp_completions(AppData* app, EditorTab* tab, const LspCompletionList* list, gpointer data) {
    (void)app;
    CompReq* req = data;
    CodeComplete* cc = req->cc;
    guint serial = req->serial;
    gboolean manual = req->manual;
    g_free(req);

    if (cc == NULL || comp_of(tab) != cc || serial != cc->serial) return;
    cc->lsp_req_id = 0;
    if (!gtk_widget_has_focus(tab->view)) return;

    gint line = 0, pfx_start = 0, cur_col = 0;
    gchar* pfx = current_prefix(tab, &line, &pfx_start, &cur_col);
    if (line != cc->anchor_line) {
        g_free(pfx);
        code_complete_hide(tab);
        return;
    }

    if (list == NULL || list->items == NULL || list->items->len == 0) {
        if (manual && pfx[0] != '\0') show_buffer_fallback(cc, pfx);
        else code_complete_hide(tab);
        g_free(pfx);
        return;
    }

    g_clear_pointer(&cc->all_items, g_ptr_array_unref);
    cc->all_items = g_ptr_array_new_with_free_func(g_object_unref);
    cc->incomplete = list->incomplete;
    for (guint i = 0; i < list->items->len; i++) {
        const LspCompletionItem* src = g_ptr_array_index(list->items, i);
        CodeCompItem* it = g_object_new(CODE_TYPE_COMP_ITEM, NULL);
        it->label = g_strdup(src->label);
        it->detail = g_strdup(src->detail);
        it->documentation = g_strdup(src->documentation);
        it->insert_text = g_strdup(src->insert_text);
        it->filter_text = g_strdup(src->filter_text);
        it->sort_text = g_strdup(src->sort_text);
        it->kind = src->kind;
        it->has_edit = src->has_edit;
        it->edit_start_line = src->edit_start_line;
        it->edit_start_col = src->edit_start_col;
        it->edit_end_line = src->edit_end_line;
        it->edit_end_col = src->edit_end_col;
        it->edit_text = g_strdup(src->edit_text);
        g_ptr_array_add(cc->all_items, it);
    }

    refilter_and_present(cc, pfx);
    g_free(pfx);
}

static void start_completion(CodeComplete* cc, const gchar* trigger_char, gboolean manual) {
    if (cc == NULL || cc->tab == NULL || cc->tab->buffer == NULL) return;
    cancel_pending(cc);
    cc->serial++;

    gint line = 0, pfx_start = 0, cur_col = 0;
    gchar* pfx = current_prefix(cc->tab, &line, &pfx_start, &cur_col);
    cc->anchor_line = line;
    cc->prefix_start_col = pfx_start;

    CompReq* req = g_new0(CompReq, 1);
    req->cc = cc;
    req->serial = cc->serial;
    req->manual = manual;
    gint64 id = code_intel_completion(cc->app, cc->tab, line, cur_col, trigger_char, on_lsp_completions, req);
    if (id > 0) {
        cc->lsp_req_id = id;
    } else {
        g_free(req);
        if (trigger_char == NULL && (manual || strlen(pfx) >= 2)) {
            show_buffer_fallback(cc, pfx);
        }
    }
    g_free(pfx);
}

static gboolean on_auto_timer(gpointer data) {
    CodeComplete* cc = data;
    cc->timer_id = 0;
    gchar* trig = cc->pending_trigger;
    cc->pending_trigger = NULL;
    if (cc->tab != NULL && cc->tab->view != NULL && gtk_widget_has_focus(cc->tab->view)) {
        start_completion(cc, trig, FALSE);
    }
    g_free(trig);
    return G_SOURCE_REMOVE;
}

void code_complete_trigger(AppData* app, EditorTab* tab) {
    (void)app;
    CodeComplete* cc = comp_of(tab);
    if (cc == NULL || tab == NULL || tab->buffer == NULL || tab->mode == EDITOR_MODE_ORGMODE) return;
    start_completion(cc, NULL, TRUE);
}

gboolean code_complete_handle_key(EditorTab* tab, guint keyval, GdkModifierType state) {
    CodeComplete* cc = comp_of(tab);
    if (cc == NULL || !cc->visible || cc->store == NULL) return FALSE;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(cc->store));
    if (n == 0) return FALSE;
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;

    if (keyval == GDK_KEY_Escape) {
        code_complete_hide(tab);
        return TRUE;
    }
    if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab ||
        keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        if (!ctrl) {
            apply_selected(cc);
            return TRUE;
        }
    }
    if (keyval == GDK_KEY_Down || (ctrl && (keyval == GDK_KEY_n || keyval == GDK_KEY_N))) {
        guint cur = gtk_single_selection_get_selected(cc->selection);
        guint next = (cur == GTK_INVALID_LIST_POSITION || cur + 1 >= n) ? 0 : cur + 1;
        gtk_single_selection_set_selected(cc->selection, next);
        gtk_list_view_scroll_to(GTK_LIST_VIEW(cc->list), next, GTK_LIST_SCROLL_NONE, NULL);
        return TRUE;
    }
    if (keyval == GDK_KEY_Up || (ctrl && (keyval == GDK_KEY_p || keyval == GDK_KEY_P))) {
        guint cur = gtk_single_selection_get_selected(cc->selection);
        guint prev = (cur == GTK_INVALID_LIST_POSITION || cur == 0) ? n - 1 : cur - 1;
        gtk_single_selection_set_selected(cc->selection, prev);
        gtk_list_view_scroll_to(GTK_LIST_VIEW(cc->list), prev, GTK_LIST_SCROLL_NONE, NULL);
        return TRUE;
    }
    if (keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_Page_Up) {
        guint cur = gtk_single_selection_get_selected(cc->selection);
        if (cur == GTK_INVALID_LIST_POSITION) cur = 0;
        guint next = keyval == GDK_KEY_Page_Down ? MIN(cur + 5, n - 1) : (cur > 5 ? cur - 5 : 0);
        gtk_single_selection_set_selected(cc->selection, next);
        gtk_list_view_scroll_to(GTK_LIST_VIEW(cc->list), next, GTK_LIST_SCROLL_NONE, NULL);
        return TRUE;
    }
    return FALSE;
}

static gboolean is_trigger_char(CodeComplete* cc, const gchar* text, gint len) {
    if (len != 1) return FALSE;
    const gchar* srv = code_intel_completion_triggers(cc->app, cc->tab);
    if (srv != NULL) return strchr(srv, text[0]) != NULL;
    return FALSE;
}

static void on_insert_text_after(GtkTextBuffer* buf, GtkTextIter* loc, gchar* text, gint len, gpointer data) {
    (void)buf; (void)loc;
    CodeComplete* cc = data;
    if (cc->applying || cc->tab->view_passive || !gtk_widget_has_focus(cc->tab->view)) return;
    if (cc->tab->mode == EDITOR_MODE_ORGMODE || !code_nav_is_code(cc->tab)) return;
    if (cc->can_auto != NULL && !cc->can_auto(cc->tab)) return;
    if (len <= 0 || len > 4 || memchr(text, '\n', (gsize)len) != NULL) {
        code_complete_hide(cc->tab);
        return;
    }

    if (is_trigger_char(cc, text, len)) {
        cancel_pending(cc);
        cc->pending_trigger = g_strndup(text, (gsize)len);
        cc->timer_id = g_timeout_add(TRIGGER_DELAY_MS, on_auto_timer, cc);
        return;
    }

    gint line = 0, pfx_start = 0, cur_col = 0;
    gchar* pfx = current_prefix(cc->tab, &line, &pfx_start, &cur_col);
    if (cc->visible && !cc->incomplete && line == cc->anchor_line && pfx_start == cc->prefix_start_col && pfx[0] != '\0') {
        refilter_and_present(cc, pfx);
    } else if (strlen(pfx) >= 2) {
        cancel_pending(cc);
        cc->timer_id = g_timeout_add(AUTO_DELAY_MS, on_auto_timer, cc);
    } else if (cc->visible) {
        code_complete_hide(cc->tab);
    }
    g_free(pfx);
}

static void on_delete_range_after(GtkTextBuffer* buf, GtkTextIter* start, GtkTextIter* end, gpointer data) {
    (void)buf; (void)start; (void)end;
    CodeComplete* cc = data;
    if (cc->applying || !cc->visible) return;
    gint line = 0, pfx_start = 0, cur_col = 0;
    gchar* pfx = current_prefix(cc->tab, &line, &pfx_start, &cur_col);
    if (line != cc->anchor_line || pfx_start != cc->prefix_start_col || pfx[0] == '\0') {
        code_complete_hide(cc->tab);
    } else {
        refilter_and_present(cc, pfx);
    }
    g_free(pfx);
}

static void on_cursor_moved(GObject* buf, GParamSpec* ps, gpointer data) {
    (void)buf; (void)ps;
    CodeComplete* cc = data;
    if (cc->applying || !cc->visible) return;
    if (cc->can_auto != NULL && !cc->can_auto(cc->tab)) {
        code_complete_hide(cc->tab);
        return;
    }
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(cc->tab->buffer, &it, gtk_text_buffer_get_insert(cc->tab->buffer));
    if (gtk_text_iter_get_line(&it) != cc->anchor_line) {
        code_complete_hide(cc->tab);
    }
}

static void on_focus_leave(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    CodeComplete* cc = data;
    code_complete_hide(cc->tab);
}

static void on_click_press(GtkGestureClick* g, gint n, gdouble x, gdouble y, gpointer data) {
    (void)g; (void)n; (void)x; (void)y;
    CodeComplete* cc = data;
    if (cc->visible) code_complete_hide(cc->tab);
}

static void on_view_destroy(GtkWidget* view, gpointer data) {
    (void)view;
    CodeComplete* cc = data;
    cancel_pending(cc);
    if (cc->popover != NULL) {
        gtk_widget_unparent(cc->popover);
        cc->popover = NULL;
    }
    if (cc->tab != NULL && cc->tab->buffer != NULL) {
        g_signal_handlers_disconnect_by_data(cc->tab->buffer, cc);
    }
}

static void comp_free(gpointer data) {
    CodeComplete* cc = data;
    cancel_pending(cc);
    g_clear_pointer(&cc->all_items, g_ptr_array_unref);
    g_clear_object(&cc->store);
    g_free(cc);
}

void code_complete_detach(EditorTab* tab) {
    CodeComplete* cc = comp_of(tab);
    if (cc == NULL) return;
    g_signal_handlers_disconnect_by_func(tab->view, (gpointer)on_view_destroy, cc);
    on_view_destroy(tab->view, cc);
}

void code_complete_attach(AppData* app, EditorTab* tab, CodeCompleteCanAutoFunc can_auto) {
    if (tab == NULL || tab->view == NULL || tab->buffer == NULL || comp_of(tab) != NULL) return;
    CodeComplete* cc = g_new0(CodeComplete, 1);
    cc->app = app;
    cc->tab = tab;
    cc->can_auto = can_auto;
    g_object_set_data_full(G_OBJECT(tab->view), COMPLETE_KEY, cc, comp_free);
    g_signal_connect(tab->view, "destroy", G_CALLBACK(on_view_destroy), cc);

    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "leave", G_CALLBACK(on_focus_leave), cc);
    gtk_widget_add_controller(tab->view, focus);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click_press), cc);
    gtk_widget_add_controller(tab->view, GTK_EVENT_CONTROLLER(click));

    g_signal_connect_after(tab->buffer, "insert-text", G_CALLBACK(on_insert_text_after), cc);
    g_signal_connect_after(tab->buffer, "delete-range", G_CALLBACK(on_delete_range_after), cc);
    g_signal_connect(tab->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), cc);
}
