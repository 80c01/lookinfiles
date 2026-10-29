/* Modal dialog window with response buttons (see dialog.h) */
#include "dialog.h"

struct _AppDialog {
    GtkWindow parent_instance;
    GtkWidget* content_area;
    GtkWidget* action_area;
};

G_DEFINE_FINAL_TYPE(AppDialog, app_dialog, GTK_TYPE_WINDOW)

enum { SIGNAL_RESPONSE, N_SIGNALS };
static guint signals[N_SIGNALS];

static const gchar* RESPONSE_KEY = "app-dialog-response";

static gboolean app_dialog_close_request(GtkWindow* window) {
    /* The response handler (or the caller of app_dialog_run()) destroys the
     * window: keep it alive here so it is never destroyed twice. */
    app_dialog_response(APP_DIALOG(window), APP_RESPONSE_DELETE_EVENT);
    return TRUE;
}

static void app_dialog_class_init(AppDialogClass* klass) {
    GTK_WINDOW_CLASS(klass)->close_request = app_dialog_close_request;
    signals[SIGNAL_RESPONSE] = g_signal_new("response", G_TYPE_FROM_CLASS(klass),
                                            G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                            G_TYPE_NONE, 1, G_TYPE_INT);
    /* Escape closes the dialog, as with GtkDialog */
    gtk_widget_class_add_binding_action(GTK_WIDGET_CLASS(klass), GDK_KEY_Escape, 0,
                                        "window.close", NULL);
}

static void app_dialog_init(AppDialog* self) {
    gtk_widget_add_css_class(GTK_WIDGET(self), "dialog");
    gtk_window_set_modal(GTK_WINDOW(self), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(self), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(self), FALSE);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    self->content_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(self->content_area, TRUE);
    gtk_box_append(GTK_BOX(vbox), self->content_area);

    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(action_box, "dialog-action-box");
    /* Spacing and margins come from the theme (.dialog-action-area) */
    self->action_area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(self->action_area, "dialog-action-area");
    gtk_widget_set_hexpand(self->action_area, TRUE);
    gtk_widget_set_halign(self->action_area, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(action_box), self->action_area);
    gtk_box_append(GTK_BOX(vbox), action_box);

    gtk_window_set_child(GTK_WINDOW(self), vbox);
}

GtkWidget* app_dialog_new(const gchar* title, GtkWindow* parent,
                          const gchar* first_button_label, ...) {
    AppDialog* self = g_object_new(APP_TYPE_DIALOG, NULL);
    if (title != NULL) {
        gtk_window_set_title(GTK_WINDOW(self), title);
    }
    if (parent != NULL) {
        gtk_window_set_transient_for(GTK_WINDOW(self), parent);
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

void app_dialog_set_default_response(AppDialog* dialog, gint response) {
    g_return_if_fail(APP_IS_DIALOG(dialog));
    GtkWidget* button = app_dialog_get_widget_for_response(dialog, response);
    if (button != NULL) {
        gtk_window_set_default_widget(GTK_WINDOW(dialog), button);
    }
}

void app_dialog_response(AppDialog* dialog, gint response) {
    g_return_if_fail(APP_IS_DIALOG(dialog));
    g_signal_emit(dialog, signals[SIGNAL_RESPONSE], 0, response);
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
    gtk_window_present(GTK_WINDOW(dialog));
    if (focus != NULL) {
        gtk_widget_grab_focus(focus);
    }
    g_main_loop_run(run.loop);
    g_main_loop_unref(run.loop);
    g_signal_handler_disconnect(dialog, id);
    g_object_unref(dialog);
    return run.response;
}
