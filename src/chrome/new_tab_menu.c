/* The + button of the tab bars and its menu of new tabs (see new_tab_menu.h) */
#include "chrome/new_tab_menu.h"
#include "core/i18n.h"
#include "editor/editor.h"
#include "jupyter/ipynb_tab.h"
#include "notes/notes.h"
#include "preview/browser.h"
#include "terminal/terminal.h"
#include "widgets/add_button.h"

#define NOTEBOOK_KEY "lookin-new-tab-notebook"
#define KIND_KEY "lookin-new-tab-kind"

typedef enum {
    NEW_TAB_WEB,
    NEW_TAB_CANVAS,
    NEW_TAB_JUPYTER,
    NEW_TAB_VIM,
    NEW_TAB_EMACS,
    NEW_TAB_NOTEPAD,
    NEW_TAB_TERMINAL
} NewTabKind;

static void open_new_tab(AppData* app, GtkNotebook* nb, NewTabKind kind) {
    switch (kind) {
    case NEW_TAB_WEB:
        browser_open(app, NULL);
        break;
    case NEW_TAB_CANVAS: {
        GtkWidget* page = notes_new_canvas(app);
        if (page != NULL) editor_move_page_to(app, page, nb, -1);
        break;
    }
    case NEW_TAB_JUPYTER:
        ipynb_tab_new_untitled(app, nb);
        break;
    case NEW_TAB_VIM:
        editor_open_untitled(app, nb, EDITOR_MODE_VIM);
        break;
    case NEW_TAB_EMACS:
        editor_open_untitled(app, nb, EDITOR_MODE_EMACS);
        break;
    case NEW_TAB_NOTEPAD:
        editor_open_untitled(app, nb, EDITOR_MODE_NOTEPAD);
        break;
    case NEW_TAB_TERMINAL:
#ifdef HAVE_VTE
        gtk_widget_grab_focus(terminal_add_tab(app, nb));
#endif
        break;
    }
}

static void on_item_clicked(GtkButton* item, gpointer data) {
    AppData* app = data;
    GtkWidget* popover = gtk_widget_get_ancestor(GTK_WIDGET(item), GTK_TYPE_POPOVER);
    GtkNotebook* nb = g_object_get_data(G_OBJECT(item), NOTEBOOK_KEY);
    NewTabKind kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), KIND_KEY));
    /* The notebook is referenced: it may go while the menu is open */
    g_object_ref(nb);
    if (popover != NULL) gtk_popover_popdown(GTK_POPOVER(popover));
    if (gtk_widget_get_root(GTK_WIDGET(nb)) != NULL) open_new_tab(app, nb, kind);
    g_object_unref(nb);
}

static void menu_item(GtkWidget* box, AppData* app, GtkNotebook* nb, const gchar* label, NewTabKind kind,
                      gboolean available, const gchar* missing) {
    GtkWidget* item = gtk_button_new();
    gtk_widget_set_name(item, "tab-context-btn");
    gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
    GtkWidget* text = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
    gtk_button_set_child(GTK_BUTTON(item), text);
    g_object_set_data(G_OBJECT(item), NOTEBOOK_KEY, nb);
    g_object_set_data(G_OBJECT(item), KIND_KEY, GINT_TO_POINTER(kind));
    g_signal_connect(item, "clicked", G_CALLBACK(on_item_clicked), app);
    if (!available) {
        gtk_widget_set_sensitive(item, FALSE);
        gtk_widget_set_tooltip_text(item, missing);
    }
    gtk_box_append(GTK_BOX(box), item);
}

static void on_plus_clicked(GtkButton* button, gpointer data) {
    AppData* app = data;
    GtkNotebook* nb = g_object_get_data(G_OBJECT(button), NOTEBOOK_KEY);
#ifdef HAVE_VTE
    gboolean has_terminal = TRUE;
#else
    gboolean has_terminal = FALSE;
#endif
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(button));
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    menu_item(box, app, nb, _("Web Browser"), NEW_TAB_WEB, browser_available(), _("Needs WebKitGTK 6.0"));
    menu_item(box, app, nb, _("Canvas"), NEW_TAB_CANVAS, TRUE, NULL);
    menu_item(box, app, nb, _("Jupyter Notebook"), NEW_TAB_JUPYTER, TRUE, NULL);
    menu_item(box, app, nb, _("Vim"), NEW_TAB_VIM, TRUE, NULL);
    menu_item(box, app, nb, _("Emacs"), NEW_TAB_EMACS, TRUE, NULL);
    menu_item(box, app, nb, _("Notepad"), NEW_TAB_NOTEPAD, TRUE, NULL);
    menu_item(box, app, nb, _("Terminal"), NEW_TAB_TERMINAL, has_terminal, _("Needs VTE for GTK 4"));
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void new_tab_menu_attach(AppData* app, GtkNotebook* nb) {
    if (app == NULL || nb == NULL) return;
    /* The + of the workspace switcher */
    GtkWidget* button = add_button_new(_("New Tab"));
    gtk_widget_set_name(button, "tab-new-btn");
    gtk_widget_set_focusable(button, FALSE);
    g_object_set_data(G_OBJECT(button), NOTEBOOK_KEY, nb);
    g_signal_connect(button, "clicked", G_CALLBACK(on_plus_clicked), app);
    gtk_notebook_set_action_widget(nb, button, GTK_PACK_END);
}

void new_tab_menu_init(void) {
    editor_set_notebook_hook(new_tab_menu_attach);
}
