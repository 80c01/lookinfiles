/* The clock and the three timers of the top bar (see clock_pill.h). */
#include "clock/clock_pill.h"
#include "core/countdown.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "theme/theme.h"

#include <string.h>

#define CLOCK_KEY "lookin-clock-pill"
#define MINUTE (60 * COUNTDOWN_SECOND)
#define TIMER_COUNT 3
#define DEFAULT_MINUTES 5 /* duration offered by a timer never started */

typedef struct ClockPill ClockPill;

typedef struct {
    ClockPill* pill;
    guint number;      /* 1, 2, 3 */
    GtkWidget* button; /* in the top bar */
    GtkWidget* label;
    Countdown countdown;
    gboolean enabled;  /* shown and counting */
    CountdownExpiry expiry; /* expiry action, kept across starts */
    gint64 tooltip_minute; /* minute (and state) of the tooltip shown */
    /* Widgets of the open popover, else NULL */
    GtkWidget* status;
    GtkWidget* spin;
    GtkWidget* pause;
    GtkWidget* reset;
    GtkWidget* off;
    GtkWidget* restart_choice;
    GtkWidget* stop_choice;
    GtkWidget* overtime_choice;
} Timer;

struct ClockPill {
    GtkWidget* clock; /* GtkButton: opens the popover too, when no timer shows */
    GtkWidget* clock_label;
    GtkWidget* clock_icon; /* instead of the time when it is hidden */
    gboolean shown;     /* the time shows */
    gboolean twelve_hour;
    gboolean seconds;
    Timer timers[TIMER_COUNT];
    GtkWidget* popover; /* open, else NULL */
    gboolean frozen;    /* LOOKIN_CLOCK_FROZEN: nothing moves */
    gchar* clock_text;  /* shown, to skip unchanged updates */
    guint source;
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".clock-pill label { font-feature-settings: \"tnum\"; }"
                  "button.status-pill.clock-clock { padding: 2px 4px; }"
                  "label.clock-time { color: @fg_muted; font-size: @size_bar; }"
                  "button.status-pill.clock-timer-paused > label { color: @fg_faint; }"
                  "button.status-pill.clock-timer-inverse,"
                  "button.status-pill.clock-timer-inverse:hover { background-color: @fg_bright; }"
                  "button.status-pill.clock-timer-inverse > label,"
                  "button.status-pill.clock-timer-inverse:hover > label { color: @bg_bar; }"
                  ".clock-popover label.clock-status { font-feature-settings: \"tnum\"; }");
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
    const gchar* format = p->twelve_hour ? (p->seconds ? "%-I:%M:%S %p" : "%-I:%M %p")
                                         : (p->seconds ? "%H:%M:%S" : "%H:%M");
    gchar* text = g_strstrip(g_date_time_format(dt, format)); /* %p is empty in some languages */
    if (g_strcmp0(text, p->clock_text) != 0) {
        gtk_label_set_text(GTK_LABEL(p->clock_label), text);
        g_free(p->clock_text);
        p->clock_text = text;
        text = NULL;
        /* TRANSLATORS: strftime format of the date in the clock tooltip */
        gchar* date = g_date_time_format(dt, _("%A, %B %-d, %Y"));
        gchar* tip = g_strdup_printf("%s\n%s", date, _("Click: Timers"));
        gtk_widget_set_tooltip_text(p->clock, tip);
        g_free(tip);
        g_free(date);
    }
    g_free(text);
    g_date_time_unref(dt);
}

static void set_button_label(GtkWidget* button, const gchar* label) {
    if (g_strcmp0(gtk_button_get_label(GTK_BUTTON(button)), label) != 0) {
        gtk_button_set_label(GTK_BUTTON(button), label);
    }
}

/* The page of t in the open popover */
static void update_popover_page(Timer* t, const gchar* text, gboolean expired, gboolean overtime) {
    if (t->status == NULL) return;
    gchar* status;
    if (!t->enabled) {
        status = g_strdup(_("Off"));
    } else if (expired) {
        status = g_strdup_printf(_("%s — expired"), text);
    } else if (t->countdown.paused) {
        status = g_strdup_printf(_("%s — paused"), text);
    } else if (overtime) {
        status = g_strdup_printf(_("%s — overtime"), text);
    } else {
        status = g_strdup_printf(_("%s — running"), text);
    }
    if (g_strcmp0(gtk_label_get_text(GTK_LABEL(t->status)), status) != 0) {
        gtk_label_set_text(GTK_LABEL(t->status), status);
    }
    g_free(status);
    set_button_label(t->pause, (t->enabled && t->countdown.paused) ? _("_Resume") : _("_Pause"));
    gtk_widget_set_sensitive(t->pause, t->enabled && !expired);
    gtk_widget_set_sensitive(t->reset, t->enabled);
    gtk_widget_set_sensitive(t->off, t->enabled);
}

static void update_tooltip(Timer* t, gint64 left, const gchar* text, gboolean expired, gboolean overtime) {
    Countdown* c = &t->countdown;
    /* The tooltip changes once a minute (and on a click) */
    gint64 minutes = (left > 0) ? (left + MINUTE - 1) / MINUTE : -(-left / MINUTE);
    gint64 minute = ((minutes * 2 + (c->paused ? 1 : 0)) * 2 + (overtime ? 1 : 0)) * 3 + (gint64)t->expiry;
    if (minute == t->tooltip_minute) return;
    t->tooltip_minute = minute;
    gchar* total = countdown_format(c->duration, c->duration);
    gchar* state;
    if (expired) {
        state = g_strdup_printf(_("Timer %1$u expired after %2$s"), t->number, total);
    } else if (overtime) {
        const gchar* over = (text[0] == '-') ? text + 1 : text;
        state = c->paused ? g_strdup_printf(_("Timer %1$u paused: %2$s over %3$s"), t->number, over, total)
                          : g_strdup_printf(_("Timer %1$u: %2$s over %3$s"), t->number, over, total);
    } else if (c->paused) {
        state = g_strdup_printf(_("Timer %1$u paused: %2$s left of %3$s"), t->number, text, total);
    } else {
        state = g_strdup_printf(_("Timer %1$u: %2$s left of %3$s"), t->number, text, total);
    }
    gchar* cycle = (t->expiry == COUNTDOWN_STOP)       ? g_strdup(_("Stops at 0:00 when it expires"))
                   : (t->expiry == COUNTDOWN_OVERTIME) ? g_strdup(_("Counts on below 0:00 when it expires"))
                                                       : g_strdup_printf(_("Cycle %u (restarts when it expires)"), c->cycle);
    gchar* tip = g_strdup_printf("%s\n%s\n%s", state, cycle,
                                 _("Click: Timers, Start, Pause, Reset — Right-Click: Pause / Resume"));
    gtk_widget_set_tooltip_text(t->button, tip);
    g_free(tip);
    g_free(cycle);
    g_free(state);
    g_free(total);
}

static void update_timer(Timer* t, gint64 now, gboolean blink) {
    if (!t->enabled) {
        update_popover_page(t, NULL, FALSE, FALSE);
        return;
    }
    Countdown* c = &t->countdown;
    gint64 left = countdown_left(c, now);
    gboolean overtime = countdown_overtime(c, now);
    gboolean expired = (left <= 0 && !overtime);
    gchar* text = countdown_format(left, c->duration);
    gtk_label_set_text(GTK_LABEL(t->label), text);
    set_class(t->button, "clock-timer-inverse", expired || (!c->paused && countdown_inverted(left, blink)));
    set_class(t->button, "clock-timer-paused", c->paused && !expired && !t->pill->frozen);
    update_tooltip(t, left, text, expired, overtime);
    update_popover_page(t, text, expired, overtime);
    g_free(text);
}

static gboolean on_tick(gpointer data);

/* The time, or the icon while no timer shows, or nothing */
static void update_clock_visibility(ClockPill* p) {
    gboolean timer_shown = FALSE;
    for (guint i = 0; i < TIMER_COUNT; i++) timer_shown = timer_shown || p->timers[i].enabled;
    gtk_widget_set_visible(p->clock_label, p->shown);
    gtk_widget_set_visible(p->clock_icon, !p->shown);
    gtk_widget_set_visible(p->clock, p->shown || !timer_shown);
}

/* One wake-up at the next change: the second of a running timer (half
 * second while it blinks), else the clock's second or minute */
static void schedule(ClockPill* p) {
    if (p->source != 0) g_source_remove(p->source);
    p->source = 0;
    if (p->frozen) return;
    gint64 now = g_get_real_time();
    gint64 unit = (p->shown && p->seconds) ? COUNTDOWN_SECOND : MINUTE;
    gint64 delay = unit - now % unit;
    gboolean blink = animations_on(p->clock);
    for (guint i = 0; i < TIMER_COUNT; i++) {
        Timer* t = &p->timers[i];
        if (!t->enabled || t->countdown.paused) continue;
        gint64 left = countdown_left(&t->countdown, now);
        if (left > 0 || t->countdown.expiry == COUNTDOWN_OVERTIME) {
            delay = MIN(delay, countdown_next_change(left, blink));
        }
    }
    p->source = g_timeout_add((guint)(delay / 1000) + 1, on_tick, p);
}

static void refresh(ClockPill* p) {
    gint64 now = g_get_real_time();
    gboolean blink = animations_on(p->clock);
    update_clock(p, now);
    update_clock_visibility(p);
    for (guint i = 0; i < TIMER_COUNT; i++) update_timer(&p->timers[i], now, blink);
    schedule(p);
}

static gboolean on_tick(gpointer data) {
    ClockPill* p = data;
    p->source = 0;
    refresh(p);
    return G_SOURCE_REMOVE;
}

static void toggle_pause(Timer* t) {
    gint64 now = g_get_real_time();
    if (!t->enabled || countdown_expired(&t->countdown, now)) return;
    if (t->countdown.paused) {
        countdown_resume(&t->countdown, now);
    } else {
        countdown_pause(&t->countdown, now);
    }
    refresh(t->pill);
}

/* Room for the full duration (and the minus sign of overtime): the bar
 * does not move */
static void set_label_width(Timer* t) {
    gchar* full = countdown_format(t->countdown.duration, t->countdown.duration);
    gint width = (gint)strlen(full) + ((t->expiry == COUNTDOWN_OVERTIME) ? 1 : 0);
    gtk_label_set_width_chars(GTK_LABEL(t->label), width);
    g_free(full);
}

/* A new cycle of minutes, starting now (0: the timer is hidden, its
 * duration kept for the popover) */
static void start_minutes(Timer* t, gint minutes) {
    gint64 now = g_get_real_time();
    t->enabled = (minutes > 0);
    gtk_widget_set_visible(t->button, t->enabled);
    if (minutes <= 0) minutes = (t->countdown.duration > 0) ? (gint)(t->countdown.duration / MINUTE) : DEFAULT_MINUTES;
    countdown_start(&t->countdown, MAX(minutes, 1) * MINUTE, now);
    countdown_set_expiry(&t->countdown, t->expiry, now);
    if (t->pill->frozen) countdown_pause(&t->countdown, now);
    set_label_width(t);
    set_class(t->button, "clock-timer-inverse", FALSE);
    t->tooltip_minute = -1;
    refresh(t->pill);
}

static void set_expiry(Timer* t, CountdownExpiry expiry) {
    if (t->expiry == expiry) return;
    t->expiry = expiry;
    countdown_set_expiry(&t->countdown, expiry, g_get_real_time());
    set_label_width(t);
    t->tooltip_minute = -1;
    refresh(t->pill);
}

/* ---- The popover of a click on a timer or the clock ---- */

static const gint PRESETS[] = {5, 15, 25, 60};

static void popdown(GtkWidget* widget) {
    gtk_popover_popdown(GTK_POPOVER(gtk_widget_get_ancestor(widget, GTK_TYPE_POPOVER)));
}

static void on_preset_clicked(GtkButton* button, gpointer data) {
    Timer* t = g_object_get_data(G_OBJECT(button), "lookin-timer");
    start_minutes(t, GPOINTER_TO_INT(data));
    popdown(GTK_WIDGET(button));
}

static void start_spin(Timer* t) {
    GtkWidget* spin = t->spin;
    gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
    start_minutes(t, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)));
    popdown(spin);
}

static void on_start_clicked(GtkButton* button, gpointer data) {
    (void)button;
    start_spin(data);
}

static void on_spin_activate(GtkSpinButton* spin, gpointer data) {
    (void)spin;
    start_spin(data);
}

static void on_pause_clicked(GtkButton* button, gpointer data) {
    toggle_pause(data);
    popdown(GTK_WIDGET(button));
}

static void on_reset_clicked(GtkButton* button, gpointer data) {
    Timer* t = data;
    start_minutes(t, (gint)(t->countdown.duration / MINUTE));
    popdown(GTK_WIDGET(button));
}

static void on_off_clicked(GtkButton* button, gpointer data) {
    start_minutes(data, 0);
    popdown(GTK_WIDGET(button));
}

static void on_expiry_toggled(GtkToggleButton* button, gpointer data) {
    if (!gtk_toggle_button_get_active(button)) return;
    gpointer expiry = g_object_get_data(G_OBJECT(button), "lookin-expiry");
    set_expiry(data, (CountdownExpiry)GPOINTER_TO_INT(expiry));
}

static gboolean unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void on_popover_closed(GtkPopover* popover, gpointer data) {
    ClockPill* p = data;
    if (p->popover == GTK_WIDGET(popover)) {
        p->popover = NULL;
        for (guint i = 0; i < TIMER_COUNT; i++) {
            Timer* t = &p->timers[i];
            t->status = t->spin = t->pause = t->reset = t->off = NULL;
            t->restart_choice = t->stop_choice = t->overtime_choice = NULL;
        }
    }
    g_idle_add(unparent_idle, g_object_ref(popover));
}

static GtkWidget* popover_button(const gchar* label, GCallback callback, gpointer data) {
    GtkWidget* button = gtk_button_new_with_mnemonic(label);
    g_signal_connect(button, "clicked", callback, data);
    return button;
}

/* A toggle of the expiry action (in the group of first, unless NULL) */
static GtkWidget* expiry_choice(Timer* t, GtkWidget* box, GtkWidget* first, const gchar* label, const gchar* tooltip,
                                CountdownExpiry expiry) {
    GtkWidget* choice = gtk_toggle_button_new_with_label(label);
    gtk_widget_set_tooltip_text(choice, tooltip);
    if (first != NULL) gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(choice), GTK_TOGGLE_BUTTON(first));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(choice), t->expiry == expiry);
    g_object_set_data(G_OBJECT(choice), "lookin-expiry", GINT_TO_POINTER(expiry));
    g_signal_connect(choice, "toggled", G_CALLBACK(on_expiry_toggled), t);
    gtk_box_append(GTK_BOX(box), choice);
    return choice;
}

/* The page of a timer: its state, durations, expiry action and actions */
static GtkWidget* timer_page(Timer* t) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    t->status = gtk_label_new(NULL);
    gtk_widget_add_css_class(t->status, "clock-status");
    gtk_label_set_xalign(GTK_LABEL(t->status), 0.0f);
    gtk_box_append(GTK_BOX(box), t->status);

    GtkWidget* presets = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(presets), TRUE);
    for (gsize i = 0; i < G_N_ELEMENTS(PRESETS); i++) {
        gchar* label = g_strdup_printf(_("%d min"), PRESETS[i]);
        GtkWidget* preset = gtk_button_new_with_label(label);
        g_free(label);
        gtk_widget_set_tooltip_text(preset, _("Start the timer with this duration"));
        g_object_set_data(G_OBJECT(preset), "lookin-timer", t);
        g_signal_connect(preset, "clicked", G_CALLBACK(on_preset_clicked), GINT_TO_POINTER(PRESETS[i]));
        gtk_box_append(GTK_BOX(presets), preset);
    }
    gtk_box_append(GTK_BOX(box), presets);

    GtkWidget* custom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    t->spin = gtk_spin_button_new_with_range(1, 1440, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(t->spin), (gdouble)(t->countdown.duration / MINUTE));
    gtk_widget_set_tooltip_text(t->spin, _("Duration in minutes (Enter starts the timer)"));
    g_signal_connect(t->spin, "activate", G_CALLBACK(on_spin_activate), t);
    GtkWidget* unit = gtk_label_new(_("minutes"));
    gtk_widget_set_hexpand(unit, TRUE);
    gtk_label_set_xalign(GTK_LABEL(unit), 0.0f);
    gtk_box_append(GTK_BOX(custom), t->spin);
    gtk_box_append(GTK_BOX(custom), unit);
    gtk_box_append(GTK_BOX(custom), popover_button(_("_Start"), G_CALLBACK(on_start_clicked), t));
    gtk_box_append(GTK_BOX(box), custom);

    GtkWidget* expiry = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* expiry_label = gtk_label_new(_("When it expires"));
    gtk_widget_set_hexpand(expiry_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(expiry_label), 0.0f);
    GtkWidget* choices = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(choices, "linked");
    t->restart_choice = expiry_choice(t, choices, NULL, _("Restart"), _("Start again at once with the same duration"),
                                      COUNTDOWN_RESTART);
    t->stop_choice = expiry_choice(t, choices, t->restart_choice, _("Stop"),
                                   _("Stay at 0:00 in reverse colours until Reset or Start"), COUNTDOWN_STOP);
    t->overtime_choice = expiry_choice(t, choices, t->restart_choice, _("Count On"),
                                       _("Count on below 0:00 (-0:01, -0:02…) in reverse colours until Reset or Start"),
                                       COUNTDOWN_OVERTIME);
    gtk_box_append(GTK_BOX(expiry), expiry_label);
    gtk_box_append(GTK_BOX(expiry), choices);
    gtk_box_append(GTK_BOX(box), expiry);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(actions), TRUE);
    t->pause = popover_button(_("_Pause"), G_CALLBACK(on_pause_clicked), t);
    gtk_widget_set_tooltip_text(t->pause, _("Right-click on the timer does the same"));
    t->reset = popover_button(_("_Reset"), G_CALLBACK(on_reset_clicked), t);
    gtk_widget_set_tooltip_text(t->reset, _("Start the current duration again from the beginning"));
    t->off = popover_button(_("_Off"), G_CALLBACK(on_off_clicked), t);
    gtk_widget_set_tooltip_text(t->off, _("Hide this timer from the top bar"));
    gtk_box_append(GTK_BOX(actions), t->pause);
    gtk_box_append(GTK_BOX(actions), t->reset);
    gtk_box_append(GTK_BOX(actions), t->off);
    gtk_box_append(GTK_BOX(box), actions);
    return box;
}

/* Opens under parent, on the page of shown */
static void open_popover(ClockPill* p, GtkWidget* parent, Timer* shown) {
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, parent); /* the pill styles only reach the button's own label */
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(box, "clock-popover");
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);

    GtkWidget* stack = gtk_stack_new();
    for (guint i = 0; i < TIMER_COUNT; i++) {
        gchar* name = g_strdup_printf("timer%u", i + 1);
        gchar* title = g_strdup_printf(_("Timer %u"), i + 1);
        gtk_stack_add_titled(GTK_STACK(stack), timer_page(&p->timers[i]), name, title);
        g_free(title);
        g_free(name);
    }
    GtkWidget* switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_widget_set_halign(switcher, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), switcher);
    gtk_box_append(GTK_BOX(box), stack);
    gchar* page = g_strdup_printf("timer%u", shown->number);
    gtk_stack_set_visible_child_name(GTK_STACK(stack), page);
    g_free(page);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    p->popover = popover;
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), p);
    refresh(p); /* fills the pages */
    gtk_popover_popup(GTK_POPOVER(popover));
    gtk_widget_grab_focus(shown->spin);
}

static void on_timer_clicked(GtkButton* button, gpointer data) {
    Timer* t = data;
    open_popover(t->pill, GTK_WIDGET(button), t);
}

static void on_clock_clicked(GtkButton* button, gpointer data) {
    ClockPill* p = data;
    open_popover(p, GTK_WIDGET(button), &p->timers[0]);
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

static ClockPill* pill_get(GtkWidget* pill) {
    return (pill != NULL) ? g_object_get_data(G_OBJECT(pill), CLOCK_KEY) : NULL;
}

void clock_pill_set_timer(GtkWidget* pill, gint minutes) {
    ClockPill* p = pill_get(pill);
    if (p != NULL) start_minutes(&p->timers[0], minutes);
}

static CountdownExpiry countdown_expiry_of(SettingsExpiry expiry) {
    switch (expiry) {
    case SETTINGS_EXPIRY_STOP: return COUNTDOWN_STOP;
    case SETTINGS_EXPIRY_COUNT_ON: return COUNTDOWN_OVERTIME;
    default: return COUNTDOWN_RESTART;
    }
}

void clock_pill_set_expiry(GtkWidget* pill, SettingsExpiry expiry) {
    ClockPill* p = pill_get(pill);
    if (p != NULL) set_expiry(&p->timers[0], countdown_expiry_of(expiry));
}

void clock_pill_set_display(GtkWidget* pill, const Settings* settings) {
    ClockPill* p = pill_get(pill);
    if (p == NULL) return;
    p->shown = settings->clock_shown;
    p->twelve_hour = settings->clock_12h;
    p->seconds = settings->clock_seconds;
    g_clear_pointer(&p->clock_text, g_free); /* the label is rewritten */
    refresh(p);
}

static GtkWidget* pill_button(GtkWidget* child) {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(button), child);
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_set_focusable(button, FALSE);
    gtk_widget_add_css_class(button, "status-pill");
    return button;
}

static void timer_init(ClockPill* p, Timer* t, guint number) {
    t->pill = p;
    t->number = number;
    t->label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(t->label), 1.0f);
    t->button = pill_button(t->label);
    gtk_widget_add_css_class(t->button, "clock-timer");
    gtk_widget_set_visible(t->button, FALSE);
    g_signal_connect(t->button, "clicked", G_CALLBACK(on_timer_clicked), t);
    GtkGesture* right = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
    g_signal_connect(right, "pressed", G_CALLBACK(on_timer_right_clicked), t);
    gtk_widget_add_controller(t->button, GTK_EVENT_CONTROLLER(right));
    countdown_start(&t->countdown, DEFAULT_MINUTES * MINUTE, g_get_real_time());
    t->tooltip_minute = -1;
}

GtkWidget* clock_pill_new(const Settings* settings) {
    ensure_css();
    ClockPill* p = g_new0(ClockPill, 1);
    p->frozen = (g_getenv("LOOKIN_CLOCK_FROZEN") != NULL);

    p->clock_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(p->clock_label, "clock-time");
    p->clock_icon = icon_image_new("alarm");
    GtkWidget* clock_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(clock_box), p->clock_icon);
    gtk_box_append(GTK_BOX(clock_box), p->clock_label);
    p->clock = pill_button(clock_box);
    gtk_widget_add_css_class(p->clock, "clock-clock");
    gtk_widget_set_valign(p->clock, GTK_ALIGN_CENTER);
    g_signal_connect(p->clock, "clicked", G_CALLBACK(on_clock_clicked), p);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(box, "clock-pill");
    gtk_box_append(GTK_BOX(box), p->clock);
    for (guint i = 0; i < TIMER_COUNT; i++) {
        timer_init(p, &p->timers[i], i + 1);
        gtk_box_append(GTK_BOX(box), p->timers[i].button);
    }
    p->shown = settings->clock_shown;
    p->twelve_hour = settings->clock_12h;
    p->seconds = settings->clock_seconds;
    p->timers[0].expiry = countdown_expiry_of(settings->timer_expiry);
    g_object_set_data_full(G_OBJECT(box), CLOCK_KEY, p, clock_pill_free);
    clock_pill_set_timer(box, settings->timer_minutes);
    return box;
}
