/* Date picker popover of the Deep Search form (see deep_search_date.h) */
#include "search/deep_search_date.h"
#include "core/i18n.h"
#include "search/deep_query.h"
#include "theme/icons.h"

#define CAL_ENTRY_KEY "lookin-deep-search-date-entry"
#define CAL_SHOWN_KEY "lookin-deep-search-date-shown" /* year * 12 + month shown */
#define CAL_SYNC_KEY "lookin-deep-search-date-sync"   /* set while the code moves it */

static void calendar_show_date(GtkCalendar* cal, GDateTime* t) {
#if GTK_CHECK_VERSION(4, 14, 0)
    gtk_calendar_set_day(cal, 1); /* a valid day in any month while it changes */
    gtk_calendar_set_year(cal, g_date_time_get_year(t));
    gtk_calendar_set_month(cal, g_date_time_get_month(t) - 1);
    gtk_calendar_set_day(cal, g_date_time_get_day_of_month(t));
#else
    gtk_calendar_select_day(cal, t);
#endif
}

static gint shown_month(GtkCalendar* cal) {
    GDateTime* t = gtk_calendar_get_date(cal);
    gint m = g_date_time_get_year(t) * 12 + g_date_time_get_month(t);
    g_date_time_unref(t);
    return m;
}

/* The popover opens on the date of the field, else today */
static void on_calendar_shown(GtkPopover* popover, gpointer data) {
    (void)popover;
    GtkCalendar* cal = data;
    GtkWidget* entry = g_object_get_data(G_OBJECT(cal), CAL_ENTRY_KEY);
    GDateTime* now = g_date_time_new_now_local();
    gint64 when = 0;
    GDateTime* t = NULL;
    if (deep_query_parse_date(gtk_editable_get_text(GTK_EDITABLE(entry)), now, FALSE, &when) && when > 0) {
        t = g_date_time_new_from_unix_local(when);
    }
    g_object_set_data(G_OBJECT(cal), CAL_SYNC_KEY, GINT_TO_POINTER(1));
    calendar_show_date(cal, t != NULL ? t : now);
    g_object_set_data(G_OBJECT(cal), CAL_SYNC_KEY, NULL);
    g_object_set_data(G_OBJECT(cal), CAL_SHOWN_KEY, GINT_TO_POINTER(shown_month(cal)));
    if (t != NULL) g_date_time_unref(t);
    g_date_time_unref(now);
}

/* The arrows moved to another month or year */
static void on_calendar_moved(GtkCalendar* cal, gpointer data) {
    (void)data;
    g_object_set_data(G_OBJECT(cal), CAL_SHOWN_KEY, GINT_TO_POINTER(shown_month(cal)));
}

/* A day of the month shown was clicked (a day of the month before or after
 * moves to that month: then only the month shown is kept) */
static void on_calendar_day(GtkCalendar* cal, gpointer data) {
    GtkWidget* button = data;
    if (g_object_get_data(G_OBJECT(cal), CAL_SYNC_KEY) != NULL) return;
    gint month = shown_month(cal);
    if (month != GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cal), CAL_SHOWN_KEY))) {
        g_object_set_data(G_OBJECT(cal), CAL_SHOWN_KEY, GINT_TO_POINTER(month));
        return;
    }
    GDateTime* t = gtk_calendar_get_date(cal);
    gchar* text = g_date_time_format(t, "%Y-%m-%d");
    gtk_editable_set_text(GTK_EDITABLE(g_object_get_data(G_OBJECT(cal), CAL_ENTRY_KEY)), text);
    g_free(text);
    g_date_time_unref(t);
    gtk_menu_button_popdown(GTK_MENU_BUTTON(button));
}

/* Clear: no date in the field */
static void on_calendar_clear(GtkButton* clear, gpointer data) {
    GtkWidget* entry = g_object_get_data(G_OBJECT(clear), CAL_ENTRY_KEY);
    gtk_editable_set_text(GTK_EDITABLE(entry), "");
    gtk_menu_button_popdown(GTK_MENU_BUTTON(data));
}

GtkWidget* deep_search_calendar_button(GtkWidget* entry) {
    GtkWidget* button = gtk_menu_button_new();
    gtk_menu_button_set_child(GTK_MENU_BUTTON(button), icon_image_new("x-office-calendar"));
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(button), FALSE);
    gtk_widget_set_focus_on_click(button, FALSE);
    gtk_widget_set_tooltip_text(button, _("Pick a Day"));
    GtkWidget* cal = gtk_calendar_new();
    g_object_set_data(G_OBJECT(cal), CAL_ENTRY_KEY, entry);
    g_signal_connect(cal, "day-selected", G_CALLBACK(on_calendar_day), button);
    const gchar* moves[] = {"prev-month", "next-month", "prev-year", "next-year"};
    for (gsize i = 0; i < G_N_ELEMENTS(moves); i++) g_signal_connect(cal, moves[i], G_CALLBACK(on_calendar_moved), NULL);
    GtkWidget* clear = gtk_button_new();
    icon_button_set_content(clear, "edit-clear", _("Clear the Date"));
    gtk_widget_set_halign(clear, GTK_ALIGN_END);
    g_object_set_data(G_OBJECT(clear), CAL_ENTRY_KEY, entry);
    g_signal_connect(clear, "clicked", G_CALLBACK(on_calendar_clear), button);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(box), cal);
    gtk_box_append(GTK_BOX(box), clear);
    GtkWidget* popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "show", G_CALLBACK(on_calendar_shown), cal);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
    return button;
}
