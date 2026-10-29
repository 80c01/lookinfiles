#ifndef DIALOG_H
#define DIALOG_H

/* Modal dialog window with a content area and a row of response buttons.
 *
 * Replaces the deprecated GtkDialog with the same model: buttons carry a
 * response id, the "response" signal reports the chosen one
 * (void handler(AppDialog* dialog, gint response, gpointer data)), Escape and
 * the window close button report APP_RESPONSE_DELETE_EVENT. The dialog is
 * modal, transient for its parent and destroyed with it. */
#include <gtk/gtk.h>

typedef enum {
    APP_RESPONSE_NONE = -1,
    APP_RESPONSE_REJECT = -2,
    APP_RESPONSE_ACCEPT = -3,
    APP_RESPONSE_DELETE_EVENT = -4,
    APP_RESPONSE_OK = -5,
    APP_RESPONSE_CANCEL = -6,
    APP_RESPONSE_CLOSE = -7,
    APP_RESPONSE_YES = -8,
    APP_RESPONSE_NO = -9,
    APP_RESPONSE_APPLY = -10
} AppResponse;

#define APP_TYPE_DIALOG (app_dialog_get_type())
G_DECLARE_FINAL_TYPE(AppDialog, app_dialog, APP, DIALOG, GtkWindow)

/* New dialog; the variable arguments are (button label, response id) pairs
 * ended by NULL. first_button_label may be NULL (add buttons later). */
GtkWidget* app_dialog_new(const gchar* title, GtkWindow* parent,
                          const gchar* first_button_label, ...) G_GNUC_NULL_TERMINATED;

/* Vertical box above the buttons, where the caller puts its widgets */
GtkWidget* app_dialog_get_content_area(AppDialog* dialog);

/* Add a button (label with mnemonic) emitting response when clicked */
GtkWidget* app_dialog_add_button(AppDialog* dialog, const gchar* label, gint response);

/* Button activated by Enter (in entries with activates-default) */
void app_dialog_set_default_response(AppDialog* dialog, gint response);

GtkWidget* app_dialog_get_widget_for_response(AppDialog* dialog, gint response);

/* Emit "response" */
void app_dialog_response(AppDialog* dialog, gint response);

/* Present the dialog, focus `focus` (if not NULL) and wait in a nested main
 * loop until a response is chosen. Returns it; the caller destroys the dialog. */
gint app_dialog_run(AppDialog* dialog, GtkWidget* focus);

#endif /* DIALOG_H */
