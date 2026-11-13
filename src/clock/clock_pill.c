/* The clock and the timer of the top bar (see clock_pill.h). */
#include "clock/clock_pill.h"
#include "core/countdown.h"
#include "core/i18n.h"
#include "theme/theme.h"

#include <string.h>

#define CLOCK_KEY "lookin-clock-pill"
#define MINUTE (60 * COUNTDOWN_SECOND)

typedef struct {
    GtkWidget* clock;  /* GtkLabel */
    GtkWidget* timer;  /* GtkButton */
    GtkWidget* timer_label;
    Countdown countdown;
    gboolean enabled;  /* a timer duration is set */
    gboolean frozen;   /* LOOKIN_CLOCK_FROZEN: nothing moves */
    gchar* clock_text; /* shown, to skip unchanged updates */
    gint64 tooltip_minute; /* minute (and pause) of the timer tooltip shown */
    guint source;
} ClockPill;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".clock-pill label { font-feature-settings: \"tnum\"; }"
                  "label.clock-time { color: @fg_muted; font-size: @size_bar; margin: 0px 6px 0px 2px; }"
                  "button.status-pill.clock-timer-paused label { color: @fg_faint; }"
                  "button.status-pill.clock-timer-inverse,"
                  "button.status-pill.clock-timer-inverse:hover { background-color: @fg_bright; }"
                  "button.status-pill.clock-timer-inverse label,"
                  "button.status-pill.clock-timer-inverse:hover label { color: @bg_bar; }");
}

static gboolean animations_on(GtkWidget* widget) {
    gboolean on = TRUE;
    g_object_get(gtk_widget_get_settings(widget), "gtk-enable-animations", &on, NULL);
    return on;
}

static void set_class(GtkWidget* widget, const gchar* name, gboolean on) {
    if (on) {
        gtk_widget_add_css_class(widget, name);
    } else {
        gtk_widget_remove_css_class(widget, name);
    }
}

static void update_clock(ClockPill* p, gint64 now) {
    GDateTime* dt = p->frozen ? g_date_time_new_local(2026, 1, 1, 12, 0, 0)
                              : g_date_time_new_from_unix_local_usec(now);
    gchar* text = g_date_time_format(dt, "%H:%M");
    if (g_strcmp0(text, p->clock_text) != 0) {
        gtk_label_set_text(GTK_LABEL(p->clock), text);
        g_free(p->clock_text);
        p->clock_text = text;
        text = NULL;
        /* TRANSLATORS: strftime format of the date in the clock tooltip */
        gchar* tip = g_date_time_format(dt, _("%A, %B %-d, %Y"));
        gtk_widget_set_tooltip_text(p->clock, tip);
        g_free(tip);
    }
    g_free(text);
    g_date_time_unref(dt);
}

static void update_timer(ClockPill* p, gint64 now, gboolean blink) {
    if (!p->enabled) return;
    Countdown* c = &p->countdown;
    gint64 left = countdown_left(c, now);
    gchar* text = countdown_format(left, c->duration);
    gtk_label_set_text(GTK_LABEL(p->timer_label), text);
    set_class(p->timer, "clock-timer-inverse", !c->paused && countdown_inverted(left, blink));
    set_class(p->timer, "clock-timer-paused", c->paused && !p->frozen);

    /* The tooltip changes once a minute (and on a click) */
    gint64 minute = (left + MINUTE - 1) / MINUTE * 2 + (c->paused ? 1 : 0);
    if (minute != p->tooltip_minute) {
        p->tooltip_minute = minute;
        gchar* total = countdown_format(c->duration, c->duration);
        gchar* state = c->paused ? g_strdup_printf(_("Timer paused: %1$s left of %2$s"), text, total)
                                 : g_strdup_printf(_("Timer: %1$s left of %2$s"), text, total);
        gchar* cycle = g_strdup_printf(_("Cycle %u (restarts when it expires)"), c->cycle);
        gchar* tip = g_strdup_printf("%s\n%s\n%s", state, cycle,
                                     _("Click: Pause / Resume — Right-Click: Restart — Duration: Preferences, Clock"));
        gtk_widget_set_tooltip_text(p->timer, tip);
        g_free(tip);
        g_free(cycle);
        g_free(state);
        g_free(total);
    }
    g_free(text);
}

static gboolean on_tick(gpointer data);

/* One wake-up at the next change: the timer's second (half second while it
 * blinks), else the clock's minute */
static void schedule(ClockPill* p) {
    if (p->source != 0) g_source_remove(p->source);
    p->source = 0;
    if (p->frozen) return;
    gint64 now = g_get_real_time();
    gint64 delay = MINUTE - now % MINUTE;
    if (p->enabled && !p->countdown.paused) {
        gint64 left = countdown_left(&p->countdown, now);
        delay = MIN(delay, countdown_next_change(left, animations_on(p->timer)));
    }
    p->source = g_timeout_add((guint)(delay / 1000) + 1, on_tick, p);
}

static void refresh(ClockPill* p) {
    gint64 now = g_get_real_time();
    update_clock(p, now);
    update_timer(p, now, animations_on(p->timer));
    schedule(p);
}

static gboolean on_tick(gpointer data) {
    ClockPill* p = data;
    p->source = 0;
    refresh(p);
    return G_SOURCE_REMOVE;
}

static void on_timer_clicked(GtkButton* button, gpointer data) {
    (void)button;
    ClockPill* p = data;
    gint64 now = g_get_real_time();
    if (p->countdown.paused) {
        countdown_resume(&p->countdown, now);
    } else {
        countdown_pause(&p->countdown, now);
    }
    refresh(p);
}

static void on_timer_right_clicked(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    ClockPill* p = data;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    countdown_start(&p->countdown, p->countdown.duration, g_get_real_time());
    p->tooltip_minute = -1;
    refresh(p);
}

static void clock_pill_free(gpointer data) {
    ClockPill* p = data;
    if (p->source != 0) g_source_remove(p->source);
    g_free(p->clock_text);
    g_free(p);
}

void clock_pill_set_timer(GtkWidget* pill, gint minutes) {
    ClockPill* p = (pill != NULL) ? g_object_get_data(G_OBJECT(pill), CLOCK_KEY) : NULL;
    if (p == NULL) return;
    p->enabled = (minutes > 0);
    gtk_widget_set_visible(p->timer, p->enabled);
    countdown_start(&p->countdown, MAX(minutes, 1) * MINUTE, g_get_real_time());
    if (p->frozen) countdown_pause(&p->countdown, g_get_real_time());
    gchar* full = countdown_format(p->countdown.duration, p->countdown.duration);
    gtk_label_set_width_chars(GTK_LABEL(p->timer_label), (gint)strlen(full)); /* the bar does not move */
    g_free(full);
    set_class(p->timer, "clock-timer-inverse", FALSE);
    p->tooltip_minute = -1;
    refresh(p);
}

GtkWidget* clock_pill_new(gint minutes) {
    ensure_css();
    ClockPill* p = g_new0(ClockPill, 1);
    p->frozen = (g_getenv("LOOKIN_CLOCK_FROZEN") != NULL);

    p->timer_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(p->timer_label), 1.0f);
    p->timer = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(p->timer), p->timer_label);
    gtk_button_set_has_frame(GTK_BUTTON(p->timer), FALSE);
    gtk_widget_set_focusable(p->timer, FALSE);
    gtk_widget_add_css_class(p->timer, "status-pill");
    gtk_widget_add_css_class(p->timer, "clock-timer");
    g_signal_connect(p->timer, "clicked", G_CALLBACK(on_timer_clicked), p);
    GtkGesture* right = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
    g_signal_connect(right, "pressed", G_CALLBACK(on_timer_right_clicked), p);
    gtk_widget_add_controller(p->timer, GTK_EVENT_CONTROLLER(right));

    p->clock = gtk_label_new(NULL);
    gtk_widget_add_css_class(p->clock, "clock-time");
    gtk_widget_set_valign(p->clock, GTK_ALIGN_CENTER);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(box, "clock-pill");
    gtk_box_append(GTK_BOX(box), p->timer);
    gtk_box_append(GTK_BOX(box), p->clock);
    g_object_set_data_full(G_OBJECT(box), CLOCK_KEY, p, clock_pill_free);
    clock_pill_set_timer(box, minutes);
    return box;
}
