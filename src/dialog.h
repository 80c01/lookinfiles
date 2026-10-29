#ifndef DIALOG_H
#define DIALOG_H

/* Modal dialog drawn inside the main window: a card with a title, a content
 * area and a row of response buttons, centred over a dimmed layer that covers
 * the window and blocks the interface behind it. No separate window is opened
 * (project rule: every interaction stays inside the application).
 *
 * Same model as GtkDialog: buttons carry a response id, the "response" signal
 * reports the chosen one (void handler(AppDialog* dialog, gint response,
 * gpointer data)), Escape reports APP_RESPONSE_DELETE_EVENT. The caller
 * removes the dialog with app_dialog_destroy().
 *
 * The parent window's child must be the GtkOverlay made by
 * app_dialog_window_overlay_new() (main.c does this). */
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
G_DECLARE_FINAL_TYPE(AppDialog, app_dialog, APP, DIALOG, GtkBox)

/* Overlay to use as the main window's child, around its content; dialogs are
 * shown on top of it */
GtkWidget* app_dialog_window_overlay_new(GtkWidget* content);

/* New dialog for window `parent`; the variable arguments are (button label,
 * response id) pairs ended by NULL. first_button_label may be NULL (add
 * buttons later). Not shown until app_dialog_present() / app_dialog_run(). */
GtkWidget* app_dialog_new(const gchar* title, GtkWindow* parent,
                          const gchar* first_button_label, ...) G_GNUC_NULL_TERMINATED;

/* Vertical box above the buttons, where the caller puts its widgets */
GtkWidget* app_dialog_get_content_area(AppDialog* dialog);

/* Add a button (label with mnemonic) emitting response when clicked */
GtkWidget* app_dialog_add_button(AppDialog* dialog, const gchar* label, gint response);

/* Button activated by Enter (in entries with activates-default) */
void app_dialog_set_default_response(AppDialog* dialog, gint response);

GtkWidget* app_dialog_get_widget_for_response(AppDialog* dialog, gint response);

/* Minimum card width in pixels */
void app_dialog_set_width(AppDialog* dialog, gint width);

/* Emit "response" */
void app_dialog_response(AppDialog* dialog, gint response);

/* Show the dialog over its window and move the focus into it */
void app_dialog_present(AppDialog* dialog);

/* Remove the dialog and give the focus back to where it was */
void app_dialog_destroy(AppDialog* dialog);

/* Present the dialog, focus `focus` (if not NULL) and wait in a nested main
 * loop until a response is chosen. Returns it; the caller destroys the dialog. */
gint app_dialog_run(AppDialog* dialog, GtkWidget* focus);

/* TRUE while a dialog is shown over this window (window-wide shortcuts are
 * then disabled) */
gboolean app_dialog_is_open(GtkWindow* window);

#endif /* DIALOG_H */
