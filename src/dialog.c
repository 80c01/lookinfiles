/* Modal dialog drawn inside the main window (see dialog.h) */
#include "dialog.h"

struct _AppDialog {
    GtkBox parent_instance;     /* the dimmed layer covering the window */
    GtkWindow* window;          /* parent window (not referenced) */
    GtkWidget* card;
    GtkWidget* title;
    GtkWidget* content_area;
    GtkWidget* action_area;
    GtkWidget* default_button;
    GtkWidget* previous_focus;  /* weak pointer: focus to restore */
    gboolean shown;
};

G_DEFINE_FINAL_TYPE(AppDialog, app_dialog, GTK_TYPE_BOX)

enum { SIGNAL_RESPONSE, N_SIGNALS };
static guint signals[N_SIGNALS];

static const gchar* RESPONSE_KEY = "app-dialog-response";
static const gchar* STACK_KEY = "app-dialog-stack"; /* GList* of shown dialogs, on the overlay */

/* ------------------------------------------------------------------------- */
/* Window overlay and dialog stack                                           */
/* ------------------------------------------------------------------------- */

GtkWidget* app_dialog_window_overlay_new(GtkWidget* content) {
    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_name(overlay, "dialog-overlay");
    gtk_overlay_set_child(GTK_OVERLAY(overlay), content);
    return overlay;
}

static GtkOverlay* window_overlay(GtkWindow* window) {
    GtkWidget* child = (window != NULL) ? gtk_window_get_child(window) : NULL;
    return GTK_IS_OVERLAY(child) ? GTK_OVERLAY(child) : NULL;
}

static GList* dialog_stack(GtkOverlay* overlay) {
    return overlay != NULL ? g_object_get_data(G_OBJECT(overlay), STACK_KEY) : NULL;
}

gboolean app_dialog_is_open(GtkWindow* window) {
    return dialog_stack(window_overlay(window)) != NULL;
}

void app_dialog_dismiss_top(GtkWindow* window) {
    GList* stack = dialog_stack(window_overlay(window));
    if (stack != NULL) {
        app_dialog_response(APP_DIALOG(g_list_last(stack)->data), APP_RESPONSE_DELETE_EVENT);
    }
}

/* Only the topmost layer takes focus and clicks: the window content while no
 * dialog is shown, otherwise the last dialog */
static void update_interactive_layer(GtkOverlay* overlay) {
    GList* stack = dialog_stack(overlay);
    GtkWidget* content = gtk_overlay_get_child(overlay);
    gboolean content_active = (stack == NULL);
    if (content != NULL) {
        gtk_widget_set_can_focus(content, content_active);
        gtk_widget_set_can_target(content, content_active);
    }
    for (GList* l = stack; l != NULL; l = l->next) {
        gboolean top = (l->next == NULL);
        gtk_widget_set_can_focus(GTK_WIDGET(l->data), top);
        gtk_widget_set_can_target(GTK_WIDGET(l->data), top);
    }
}

/* ------------------------------------------------------------------------- */
/* Type                                                                      */
/* ------------------------------------------------------------------------- */

static gboolean on_key_pressed(GtkEventControllerKey* controller, guint keyval, guint keycode,
                               GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    (void)state;
    if (keyval == GDK_KEY_Escape) {
        app_dialog_response(APP_DIALOG(data), APP_RESPONSE_DELETE_EVENT);
        return TRUE;
    }
    return FALSE;
}

static void app_dialog_dispose(GObject* object) {
    AppDialog* self = APP_DIALOG(object);
    if (self->previous_focus != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(self->previous_focus), (gpointer*)&self->previous_focus);
        self->previous_focus = NULL;
    }
    G_OBJECT_CLASS(app_dialog_parent_class)->dispose(object);
}

static void app_dialog_class_init(AppDialogClass* klass) {
    G_OBJECT_CLASS(klass)->dispose = app_dialog_dispose;
    signals[SIGNAL_RESPONSE] = g_signal_new("response", G_TYPE_FROM_CLASS(klass),
                                            G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                            G_TYPE_NONE, 1, G_TYPE_INT);
}

static void app_dialog_init(AppDialog* self) {
    /* Dimmed layer: fills the window and swallows clicks on the interface behind */
    gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class(GTK_WIDGET(self), "dialog-scrim");
    gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_FILL);
    gtk_widget_set_valign(GTK_WIDGET(self), GTK_ALIGN_FILL);
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(gtk_gesture_click_new()));

    self->card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(self->card, "dialog-card");
    gtk_widget_set_halign(self->card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(self->card, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(self->card, TRUE);
    gtk_widget_set_vexpand(self->card, TRUE);
    gtk_box_append(GTK_BOX(self), self->card);

    self->title = gtk_label_new(NULL);
    gtk_widget_add_css_class(self->title, "dialog-title");
    gtk_label_set_xalign(GTK_LABEL(self->title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(self->title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_visible(self->title, FALSE);
    gtk_box_append(GTK_BOX(self->card), self->title);

    self->content_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(self->content_area, TRUE);
    gtk_box_append(GTK_BOX(self->card), self->content_area);

    /* Spacing and margins come from the theme (.dialog-action-area) */
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(action_box, "dialog-action-box");
    self->action_area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(self->action_area, "dialog-action-area");
    gtk_widget_set_hexpand(self->action_area, TRUE);
    gtk_widget_set_halign(self->action_area, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(action_box), self->action_area);
    gtk_box_append(GTK_BOX(self->card), action_box);

    GtkEventController* keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), self);
    gtk_widget_add_controller(GTK_WIDGET(self), keys);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

GtkWidget* app_dialog_new(const gchar* title, GtkWindow* parent,
                          const gchar* first_button_label, ...) {
    AppDialog* self = g_object_new(APP_TYPE_DIALOG, NULL);
    g_object_ref_sink(self); /* owned by the caller until presented */
    self->window = parent;
    if (title != NULL && title[0] != '\0') {
        gtk_label_set_text(GTK_LABEL(self->title), title);
        gtk_widget_set_visible(self->title, TRUE);
    }
    va_list args;
    va_start(args, first_button_label);
    for (const gchar* label = first_button_label; label != NULL; label = va_arg(args, const gchar*)) {
        gint response = va_arg(args, gint);
        app_dialog_add_button(self, label, response);
    }
    va_end(args);
    return GTK_WIDGET(self);
}

GtkWidget* app_dialog_get_content_area(AppDialog* dialog) {
    g_return_val_if_fail(APP_IS_DIALOG(dialog), NULL);
    return dialog->content_area;
}

static void on_button_clicked(GtkButton* button, gpointer data) {
    gint response = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), RESPONSE_KEY));
    app_dialog_response(APP_DIALOG(data), response);
}

GtkWidget* app_dialog_add_button(AppDialog* dialog, const gchar* label, gint response) {
    g_return_val_if_fail(APP_IS_DIALOG(dialog), NULL);
    GtkWidget* button = gtk_button_new_with_mnemonic(label);
    g_object_set_data(G_OBJECT(button), RESPONSE_KEY, GINT_TO_POINTER(response));
    g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), dialog);
    gtk_box_append(GTK_BOX(dialog->action_area), button);
    return button;
}

GtkWidget* app_dialog_get_widget_for_response(AppDialog* dialog, gint response) {
    g_return_val_if_fail(APP_IS_DIALOG(dialog), NULL);
    for (GtkWidget* child = gtk_widget_get_first_child(dialog->action_area); child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), RESPONSE_KEY)) == response) {
            return child;
        }
    }
    return NULL;
}

/* The default button is the primary one (blue), unless it only dismisses
 * the dialog (destructive confirmations default to Cancel) */
static gboolean is_dismiss_response(gint response) {
    return response == APP_RESPONSE_CANCEL || response == APP_RESPONSE_NO ||
           response == APP_RESPONSE_REJECT || response == APP_RESPONSE_DELETE_EVENT;
}

void app_dialog_set_default_response(AppDialog* dialog, gint response) {
    g_return_if_fail(APP_IS_DIALOG(dialog));
    if (dialog->default_button != NULL &&
        g_object_get_data(G_OBJECT(dialog->default_button), "auto-suggested") != NULL) {
        gtk_widget_remove_css_class(dialog->default_button, "suggested-action");
        g_object_set_data(G_OBJECT(dialog->default_button), "auto-suggested", NULL);
    }
    dialog->default_button = app_dialog_get_widget_for_response(dialog, response);
    GtkWidget* b = dialog->default_button;
    if (b != NULL && !is_dismiss_response(response) && !gtk_widget_has_css_class(b, "destructive-action") &&
        !gtk_widget_has_css_class(b, "suggested-action")) {
        gtk_widget_add_css_class(b, "suggested-action");
        g_object_set_data(G_OBJECT(b), "auto-suggested", GINT_TO_POINTER(1));
    }
    if (dialog->shown && dialog->window != NULL) {
        gtk_window_set_default_widget(dialog->window, dialog->default_button);
    }
}

void app_dialog_set_width(AppDialog* dialog, gint width) {
    g_return_if_fail(APP_IS_DIALOG(dialog));
    gtk_widget_set_size_request(dialog->card, width, -1);
}

void app_dialog_response(AppDialog* dialog, gint response) {
    g_return_if_fail(APP_IS_DIALOG(dialog));
    g_signal_emit(dialog, signals[SIGNAL_RESPONSE], 0, response);
}

/* First focusable widget of the card: an entry or the default button */
static void focus_first(AppDialog* self) {
    if (!gtk_widget_child_focus(self->card, GTK_DIR_TAB_FORWARD) && self->default_button != NULL) {
        gtk_widget_grab_focus(self->default_button);
    }
}

void app_dialog_present(AppDialog* dialog) {
    g_return_if_fail(APP_IS_DIALOG(dialog));
    GtkOverlay* overlay = window_overlay(dialog->window);
    if (dialog->shown) {
        return;
    }
    if (overlay == NULL) {
        g_critical("app_dialog_present: the parent window has no dialog overlay "
                   "(see app_dialog_window_overlay_new())");
        return;
    }
    dialog->shown = TRUE;

    GtkWidget* focus = gtk_window_get_focus(dialog->window);
    if (focus != NULL) {
        dialog->previous_focus = focus;
        g_object_add_weak_pointer(G_OBJECT(focus), (gpointer*)&dialog->previous_focus);
    }

    gtk_overlay_add_overlay(overlay, GTK_WIDGET(dialog));
    g_object_unref(dialog); /* now owned by the overlay */
    GList* stack = g_list_append(dialog_stack(overlay), dialog);
    g_object_set_data(G_OBJECT(overlay), STACK_KEY, stack);
    update_interactive_layer(overlay);

    gtk_window_set_default_widget(dialog->window, dialog->default_button);
    focus_first(dialog);
}

void app_dialog_destroy(AppDialog* dialog) {
    g_return_if_fail(APP_IS_DIALOG(dialog));
    if (!dialog->shown) {
        g_object_unref(dialog); /* never presented: drop the caller's reference */
        return;
    }
    GtkOverlay* overlay = window_overlay(dialog->window);
    GtkWindow* window = dialog->window;
    GtkWidget* previous = dialog->previous_focus;
    dialog->shown = FALSE;
    if (overlay == NULL) {
        return;
    }
    GList* stack = g_list_remove(dialog_stack(overlay), dialog);
    g_object_set_data(G_OBJECT(overlay), STACK_KEY, stack);
    update_interactive_layer(overlay);

    /* The dialog below (if any) gets Enter and the focus back */
    AppDialog* below = (stack != NULL) ? APP_DIALOG(g_list_last(stack)->data) : NULL;
    gtk_window_set_default_widget(window, below != NULL ? below->default_button : NULL);
    gtk_overlay_remove_overlay(overlay, GTK_WIDGET(dialog)); /* may finalize dialog */
    if (previous != NULL && gtk_widget_get_root(previous) == GTK_ROOT(window)) {
        gtk_widget_grab_focus(previous);
    } else if (below != NULL) {
        focus_first(below);
    }
}

typedef struct {
    GMainLoop* loop;
    gint response;
} RunData;

static void on_run_response(AppDialog* dialog, gint response, gpointer data) {
    (void)dialog;
    RunData* run = data;
    run->response = response;
    if (g_main_loop_is_running(run->loop)) {
        g_main_loop_quit(run->loop);
    }
}

gint app_dialog_run(AppDialog* dialog, GtkWidget* focus) {
    g_return_val_if_fail(APP_IS_DIALOG(dialog), APP_RESPONSE_NONE);
    RunData run = { g_main_loop_new(NULL, FALSE), APP_RESPONSE_NONE };
    g_object_ref(dialog);
    gulong id = g_signal_connect(dialog, "response", G_CALLBACK(on_run_response), &run);
    app_dialog_present(dialog);
    if (focus != NULL) {
        gtk_widget_grab_focus(focus);
    }
    g_main_loop_run(run.loop);
    g_main_loop_unref(run.loop);
    g_signal_handler_disconnect(dialog, id);
    g_object_unref(dialog);
    return run.response;
}
