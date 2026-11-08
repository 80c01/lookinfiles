/* Panel helpers shared by features and window chrome: fullscreen, focused-panel
 * highlight, workspace width, split-handle drag feedback and split fractions. */
#include "panels.h"
#include "app_events.h"

/* Focused panel and split-handle drag (app->panels), created on first use */
typedef struct Panels Panels;
struct Panels {
    GtkWidget* focused; /* panel holding the keyboard focus ("panel-focused"), or NULL */
    GtkWidget* popup;   /* size badge over the workspace, shown while dragging; NULL before
                         * panels_size_popup_attach() */
    GtkWidget* label;   /* "w × h  |  w × h" of the two sides */
    GtkWidget* paned;   /* paned whose handle is being dragged, or NULL */
    gboolean active;    /* a split handle is being dragged */
    gboolean following_main; /* the right pane follows a main pane drag: not a drag of its own */
};

static Panels* panels_get(AppData* app) {
    if (app->panels == NULL) {
        app->panels = g_new0(Panels, 1);
    }
    return app->panels;
}

NavStyle app_nav_style(AppData* app) {
    if (app == NULL) return NAV_STYLE_NOTEPAD;
    switch (app->default_editor_mode) {
    case EDITOR_MODE_VIM: return NAV_STYLE_VIM;
    case EDITOR_MODE_EMACS:
    case EDITOR_MODE_ORGMODE: return NAV_STYLE_EMACS;
    default: return NAV_STYLE_NOTEPAD;
    }
}

gboolean window_is_fullscreen(GtkWindow* win) {
    return gtk_window_is_fullscreen(win);
}

void toggle_fullscreen(GtkWindow* window) {
    if (window_is_fullscreen(window)) {
        gtk_window_unfullscreen(window);
    } else {
        gtk_window_fullscreen(window);
    }
}


gint get_workspace_total_width(AppData* app) {
    if (app == NULL) {
        return 0;
    }
    if (app->workspace_hbox != NULL) {
        gint w = gtk_widget_get_width(app->workspace_hbox);
        if (w > 60) {
            return w;
        }
    }
    if (app->window != NULL) {
        gint w = gtk_widget_get_width(GTK_WIDGET(app->window));
        if (w > 60) {
            return w;
        }
    }
    if (app->main_paned != NULL) {
        gint mw = gtk_widget_get_width(app->main_paned);
        gboolean help_vis = (app->help_window != NULL) && gtk_widget_get_visible(app->help_window);
        gint help_w = (help_vis && app->help_window != NULL) ? gtk_widget_get_width(app->help_window) : 0;
        if (help_vis && help_w <= 0) {
            help_w = 400;
        }
        return mw + help_w;
    }
    return 0;
}


/* Update the active-panel CSS highlight based on which panel owns keyboard focus */
gboolean update_active_panel_highlight(AppData* app) {
    if (app == NULL || app->window == NULL) {
        return FALSE;
    }

    GtkWidget* focused = gtk_window_get_focus(app->window);
    GtkWidget* target_panel = NULL;

    GtkWidget* w = focused;
    while (w != NULL) {
        if (app->left_vbox != NULL && w == app->left_vbox) {
            target_panel = app->left_vbox;
            break;
        }
        if (app->viewer_box != NULL && w == app->viewer_box) {
            target_panel = app->viewer_box;
            break;
        }
        if (app->editor_box != NULL && w == app->editor_box) {
            target_panel = app->editor_box;
            break;
        }
        if (app->term_scroll != NULL && w == app->term_scroll) {
            target_panel = app->term_scroll;
            break;
        }
        if (app->help_window != NULL && w == app->help_window) {
            target_panel = app->help_window;
            break;
        }
        w = gtk_widget_get_parent(w);
    }

    Panels* p = panels_get(app);
    if (p->focused == target_panel) {
        return FALSE;
    }
    if (p->focused != NULL) {
        gtk_widget_remove_css_class(p->focused, "panel-focused");
    }
    p->focused = target_panel;
    if (p->focused != NULL) {
        gtk_widget_add_css_class(p->focused, "panel-focused");
    }
    return TRUE;
}

/* Show or refresh small overlay badge listing w x h of panes */
static void update_pane_dims(GtkWidget* paned_widget, AppData* app) {
    Panels* d = app->panels;
    GtkPaned* paned = GTK_PANED(paned_widget);
    GtkWidget* c1 = gtk_paned_get_start_child(paned);
    GtkWidget* c2 = gtk_paned_get_end_child(paned);
    if (c1 == NULL || c2 == NULL ||
        !gtk_widget_get_visible(c1) || !gtk_widget_get_visible(c2)) {
        return; /* nothing to drag on that side (panel hidden) */
    }
    gint w1 = gtk_widget_get_width(c1);
    gint h1 = gtk_widget_get_height(c1);
    gint w2 = gtk_widget_get_width(c2);
    gint h2 = gtk_widget_get_height(c2);
    if (w1 <= 0 || w2 <= 0) {
        return; /* not laid out yet */
    }

    gchar* text = g_strdup_printf("%d \xc3\x97 %d  |  %d \xc3\x97 %d",
                                  w1, h1, w2, h2);
    gtk_label_set_text(GTK_LABEL(d->label), text);
    g_free(text);

    gtk_widget_set_visible(d->popup, TRUE);
}

static gboolean pane_dims_idle(gpointer data) {
    AppData* app = (AppData*)data;
    Panels* d = app->panels;
    if (d != NULL && d->active && d->paned != NULL) {
        update_pane_dims(d->paned, app);
    }
    return FALSE; /* one-shot */
}

static void on_paned_pressed(GtkGestureClick* gesture, gint n_press,
                             gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    AppData* app = (AppData*)data;
    Panels* d = app->panels;
    if (d == NULL || d->popup == NULL) {
        return;
    }
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    if (!GTK_IS_PANED(widget)) {
        return;
    }
    GtkWidget* c1 = gtk_paned_get_start_child(GTK_PANED(widget));
    GtkWidget* c2 = gtk_paned_get_end_child(GTK_PANED(widget));
    if (c1 == NULL || c2 == NULL ||
        !gtk_widget_get_visible(c1) || !gtk_widget_get_visible(c2)) {
        return;
    }
    GtkWidget* picked = gtk_widget_pick(widget, x, y, GTK_PICK_DEFAULT);
    gboolean on_own_sep = (picked != NULL &&
                           g_strcmp0(gtk_widget_get_css_name(picked), "separator") == 0 &&
                           gtk_widget_get_parent(picked) == widget);
    if (!on_own_sep) {
        /* Also check grab tolerance around this paned's divider position,
         * unless the click landed on a nested child paned's separator. */
        if (picked != NULL &&
            g_strcmp0(gtk_widget_get_css_name(picked), "separator") == 0 &&
            gtk_widget_get_parent(picked) != widget) {
            return;
        }
        GtkOrientation o = gtk_orientable_get_orientation(GTK_ORIENTABLE(widget));
        gint sep_pos = gtk_paned_get_position(GTK_PANED(widget));
        gdouble coord = (o == GTK_ORIENTATION_VERTICAL) ? y : x;
        if (sep_pos <= 0 || ABS((gint)coord - sep_pos) > 16) {
            return;
        }
    }
    d->active = TRUE;
    d->paned = widget;
    update_pane_dims(widget, app);
}

static void record_paned_fraction(AppData* app, GtkWidget* widget) {
    if (app == NULL || widget == NULL || !GTK_IS_PANED(widget) ||
        app->switching_ws || app_layout_held(app) ||
        (app->panels != NULL && app->panels->following_main) ||
        app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return;
    }
    VirtualLayout* ws = &app->workspaces[app->current_ws];

    if (widget == app->main_paned) {
        if (ws->files_vis && (ws->preview_vis || ws->editor_dedicated || ws->term_vis)) {
            gint mw = gtk_widget_get_width(app->main_paned);
            gint pos = gtk_paned_get_position(GTK_PANED(app->main_paned));
            gint total_w = get_workspace_total_width(app);
            if (total_w <= 0) {
                total_w = mw;
            }
            if (total_w > 60 && pos > 0 && pos < total_w) {
                ws->main_frac = CLAMP((gdouble)pos / total_w, 0.05, 0.95);
                if (app->right_paned != NULL && (ws->preview_vis || ws->editor_dedicated) && ws->term_vis &&
                    ws->right_orient == GTK_ORIENTATION_HORIZONTAL) {
                    gint rw = mw - pos;
                    if (rw > 40) {
                        panels_get(app)->following_main = TRUE;
                        gtk_paned_set_position(GTK_PANED(app->right_paned),
                                               (gint)(ws->right_frac * rw + 0.5));
                        panels_get(app)->following_main = FALSE;
                    }
                }
            }
        }
    } else if (widget == app->editor_paned) {
        if (ws->preview_vis && ws->editor_dedicated) {
            gint len = gtk_widget_get_width(app->editor_paned);
            gint pos = gtk_paned_get_position(GTK_PANED(app->editor_paned));
            if (len > 40 && pos > 0 && pos < len) {
                ws->editor_frac = CLAMP((gdouble)pos / len, 0.05, 0.95);
            }
        }
    } else if (widget == app->right_paned) {
        if ((ws->preview_vis || ws->editor_dedicated) && ws->term_vis) {
            gint len = (ws->right_orient == GTK_ORIENTATION_VERTICAL)
                           ? gtk_widget_get_height(app->right_paned)
                           : gtk_widget_get_width(app->right_paned);
            gint pos = gtk_paned_get_position(GTK_PANED(app->right_paned));
            if (len > 40 && pos > 0 && pos < len) {
                ws->right_frac = CLAMP((gdouble)pos / len, 0.05, 0.95);
            }
        }
    } else {
        GtkWidget* c1 = gtk_paned_get_start_child(GTK_PANED(widget));
        GtkWidget* c2 = gtk_paned_get_end_child(GTK_PANED(widget));
        if (c1 != NULL && c2 != NULL &&
            gtk_widget_get_width(c1) > 0 && gtk_widget_get_width(c2) > 0) {
            GtkOrientation o = gtk_orientable_get_orientation(GTK_ORIENTABLE(widget));
            gint len = (o == GTK_ORIENTATION_VERTICAL)
                           ? gtk_widget_get_height(widget)
                           : gtk_widget_get_width(widget);
            gint pos = gtk_paned_get_position(GTK_PANED(widget));
            if (len > 40 && pos > 0 && pos < len) {
                gdouble* frac = g_new(gdouble, 1);
                *frac = CLAMP((gdouble)pos / len, 0.05, 0.95);
                g_object_set_data_full(G_OBJECT(widget), "term-paned-frac", frac, g_free);
            }
        }
    }
}

static gboolean on_paned_legacy_event(GtkEventControllerLegacy* ctrl,
                                      GdkEvent* event, gpointer data) {
    (void)ctrl;
    AppData* app = (AppData*)data;
    Panels* d = app->panels;
    if (d != NULL && d->active && gdk_event_get_event_type(event) == GDK_BUTTON_RELEASE) {
        if (d->paned != NULL) {
            record_paned_fraction(app, d->paned);
        }
        d->active = FALSE;
        d->paned = NULL;
        gtk_widget_set_visible(d->popup, FALSE);
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    }
    return FALSE;
}

static void on_paned_notify_position(GObject* object, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    AppData* app = (AppData*)data;
    Panels* d = app != NULL ? app->panels : NULL;
    if (d == NULL || app->switching_ws || app_layout_held(app) ||
        d->following_main || !d->active) {
        return;
    }
    GtkWidget* widget = GTK_WIDGET(object);
    if (d->paned == NULL || d->paned == widget) {
        d->paned = widget;
        record_paned_fraction(app, widget);
        g_idle_add(pane_dims_idle, app);
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    }
}

static void on_paned_released(GtkGestureClick* gesture, gint n_press,
                              gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    AppData* app = (AppData*)data;
    Panels* d = app->panels;
    if (d == NULL) {
        return;
    }
    if (d->active && d->paned != NULL) {
        record_paned_fraction(app, d->paned);
    }
    d->active = FALSE;
    d->paned = NULL;
    gtk_widget_set_visible(d->popup, FALSE);
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
}

/* A non-wide GtkPaned handle grabs the pointer ~15 px on each side of its
 * 1 px line, over the panel contents, which prevents selecting text near the
 * edge. A wide handle only reacts to its own box: the theme gives it a 1 px
 * visible line inside 2 px transparent borders (5 px grab zone). */
void paned_use_thin_handle(GtkWidget* paned) {
    if (GTK_IS_PANED(paned)) {
        gtk_paned_set_wide_handle(GTK_PANED(paned), TRUE);
    }
}

void wire_paned_controllers(GtkWidget* paned, AppData* app) {
    paned_use_thin_handle(paned);
    GtkGesture* click = gtk_gesture_click_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
                                               GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_paned_pressed), app);
    g_signal_connect(click, "released", G_CALLBACK(on_paned_released), app);
    gtk_widget_add_controller(paned, GTK_EVENT_CONTROLLER(click));

    GtkEventController* legacy = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(legacy, GTK_PHASE_CAPTURE);
    g_signal_connect(legacy, "event", G_CALLBACK(on_paned_legacy_event), app);
    gtk_widget_add_controller(paned, legacy);

    g_signal_connect(paned, "notify::position",
                     G_CALLBACK(on_paned_notify_position), app);
}

void panels_size_popup_attach(AppData* app, GtkOverlay* overlay) {
    GtkWidget* popup = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(popup, "dim-window");
    gtk_widget_set_halign(popup, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(popup, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(popup, FALSE);
    GtkWidget* label = gtk_label_new("");
    gtk_box_append(GTK_BOX(popup), label);
    gtk_overlay_add_overlay(overlay, popup);

    Panels* d = panels_get(app);
    d->popup = popup;
    d->label = label;
}

void panels_free(AppData* app) {
    if (app != NULL) {
        g_clear_pointer(&app->panels, g_free);
    }
}
