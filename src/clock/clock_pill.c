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
                  "label.clock-time { color: @fg_muted; font-size: @size_bar; margin: 0px 2px; }"
                  "button.status-pill.clock-timer-paused > label { color: @fg_faint; }"
                  "button.status-pill.clock-timer-inverse,"
                  "button.status-pill.clock-timer-inverse:hover { background-color: @fg_bright; }"
                  "button.status-pill.clock-timer-inverse > label,"
                  "button.status-pill.clock-timer-inverse:hover > label { color: @bg_bar; }");
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
                                     _("Click: Durations, Start, Pause, Reset — Right-Click: Pause / Resume"));
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

static void toggle_pause(ClockPill* p) {
    gint64 now = g_get_real_time();
    if (p->countdown.paused) {
        countdown_resume(&p->countdown, now);
    } else {
        countdown_pause(&p->countdown, now);
    }
    refresh(p);
}

/* A new cycle of minutes, starting now */
static void start_minutes(ClockPill* p, gint minutes) {
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

/* ---- The popover of a click on the timer ---- */

static const gint PRESETS[] = {5, 15, 25, 60};

#define POPOVER_PILL_KEY "lookin-clock-popover-pill"

static ClockPill* popover_pill(GtkWidget* widget) {
    GtkWidget* popover = gtk_widget_get_ancestor(widget, GTK_TYPE_POPOVER);
    return g_object_get_data(G_OBJECT(popover), POPOVER_PILL_KEY);
}

static void popdown(GtkWidget* widget) {
    gtk_popover_popdown(GTK_POPOVER(gtk_widget_get_ancestor(widget, GTK_TYPE_POPOVER)));
}

static void on_preset_clicked(GtkButton* button, gpointer data) {
    start_minutes(popover_pill(GTK_WIDGET(button)), GPOINTER_TO_INT(data));
    popdown(GTK_WIDGET(button));
}

static void start_spin(GtkWidget* spin) {
    gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
    start_minutes(popover_pill(spin), gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)));
    popdown(spin);
}

static void on_start_clicked(GtkButton* button, gpointer data) {
    (void)button;
    start_spin(GTK_WIDGET(data));
}

static void on_spin_activate(GtkSpinButton* spin, gpointer data) {
    (void)data;
    start_spin(GTK_WIDGET(spin));
}

static void on_pause_clicked(GtkButton* button, gpointer data) {
    (void)data;
    toggle_pause(popover_pill(GTK_WIDGET(button)));
    popdown(GTK_WIDGET(button));
}

static void on_reset_clicked(GtkButton* button, gpointer data) {
    (void)data;
    ClockPill* p = popover_pill(GTK_WIDGET(button));
    start_minutes(p, (gint)(p->countdown.duration / MINUTE));
    popdown(GTK_WIDGET(button));
}

static gboolean unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void on_popover_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    g_idle_add(unparent_idle, g_object_ref(popover));
}

static GtkWidget* popover_button(const gchar* label, GCallback callback, gpointer data) {
    GtkWidget* button = gtk_button_new_with_mnemonic(label);
    g_signal_connect(button, "clicked", callback, data);
    return button;
}

static void on_timer_clicked(GtkButton* button, gpointer data) {
    ClockPill* p = data;
    /* On the button, so it opens under the timer (the pill styles only reach its own label) */
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(button));
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    g_object_set_data(G_OBJECT(popover), POPOVER_PILL_KEY, p);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(box, "clock-popover");
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);

    GtkWidget* presets = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(presets), TRUE);
    for (gsize i = 0; i < G_N_ELEMENTS(PRESETS); i++) {
        gchar* label = g_strdup_printf(_("%d min"), PRESETS[i]);
        GtkWidget* preset = gtk_button_new_with_label(label);
        g_free(label);
        gtk_widget_set_tooltip_text(preset, _("Start the timer with this duration"));
        g_signal_connect(preset, "clicked", G_CALLBACK(on_preset_clicked), GINT_TO_POINTER(PRESETS[i]));
        gtk_box_append(GTK_BOX(presets), preset);
    }
    gtk_box_append(GTK_BOX(box), presets);

    GtkWidget* custom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* spin = gtk_spin_button_new_with_range(1, 1440, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), (gdouble)(p->countdown.duration / MINUTE));
    gtk_widget_set_tooltip_text(spin, _("Duration in minutes (Enter starts the timer)"));
    g_signal_connect(spin, "activate", G_CALLBACK(on_spin_activate), NULL);
    GtkWidget* unit = gtk_label_new(_("minutes"));
    gtk_widget_set_hexpand(unit, TRUE);
    gtk_label_set_xalign(GTK_LABEL(unit), 0.0f);
    gtk_box_append(GTK_BOX(custom), spin);
    gtk_box_append(GTK_BOX(custom), unit);
    gtk_box_append(GTK_BOX(custom), popover_button(_("_Start"), G_CALLBACK(on_start_clicked), spin));
    gtk_box_append(GTK_BOX(box), custom);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(actions), TRUE);
    GtkWidget* pause = popover_button(p->countdown.paused ? _("_Resume") : _("_Pause"),
                                      G_CALLBACK(on_pause_clicked), NULL);
    gtk_widget_set_tooltip_text(pause, _("Right-click on the timer does the same"));
    GtkWidget* reset = popover_button(_("_Reset"), G_CALLBACK(on_reset_clicked), NULL);
    gtk_widget_set_tooltip_text(reset, _("Start the current duration again from the beginning"));
    gtk_box_append(GTK_BOX(actions), pause);
    gtk_box_append(GTK_BOX(actions), reset);
    gtk_box_append(GTK_BOX(box), actions);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
    gtk_widget_grab_focus(spin);
}

static void on_timer_right_clicked(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    toggle_pause(data);
}

static void clock_pill_free(gpointer data) {
    ClockPill* p = data;
    if (p->source != 0) g_source_remove(p->source);
    g_free(p->clock_text);
    g_free(p);
}

void clock_pill_set_timer(GtkWidget* pill, gint minutes) {
    ClockPill* p = (pill != NULL) ? g_object_get_data(G_OBJECT(pill), CLOCK_KEY) : NULL;
    if (p != NULL) start_minutes(p, minutes);
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
    gtk_box_append(GTK_BOX(box), p->clock);
    gtk_box_append(GTK_BOX(box), p->timer);
    g_object_set_data_full(G_OBJECT(box), CLOCK_KEY, p, clock_pill_free);
    clock_pill_set_timer(box, minutes);
    return box;
}
