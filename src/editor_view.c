/* The text view of an editor tab (see editor_view.h) */
#include "editor_view.h"
#include "core/i18n.h"
#include <math.h>
#include "editor_modes.h"
#include "core/app_events.h"
#include "widgets/panels.h"
#include "theme/text_style.h"
#include "widgets/minimap.h"
#include "widgets/color_column.h"
#include "widgets/diff_gutter.h"
#include "code_help.h"
#include "editor_tagbar.h"
#include "core/file_types.h"

/* The tab whose view has the buffer's cursor (the views of a split file share
 * the buffer, which has one cursor; the others keep theirs in view_insert /
 * view_bound) */
#define ACTIVE_VIEW_KEY "editor-active-view"

static EditorTab* active_of(GtkTextBuffer* buffer) {
    return g_object_get_data(G_OBJECT(buffer), ACTIVE_VIEW_KEY);
}

EditorTab* editor_view_active(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return tab;
    EditorTab* active = active_of(tab->buffer);
    return active != NULL ? active : tab;
}

static void save_cursor(EditorTab* tab) {
    GtkTextBuffer* buf = tab->buffer;
    GtkTextIter ins, bound;
    gtk_text_buffer_get_iter_at_mark(buf, &ins, gtk_text_buffer_get_insert(buf));
    gtk_text_buffer_get_iter_at_mark(buf, &bound, gtk_text_buffer_get_selection_bound(buf));
    gtk_text_buffer_move_mark(buf, tab->view_insert, &ins);
    gtk_text_buffer_move_mark(buf, tab->view_bound, &bound);
}

static void restore_cursor(EditorTab* tab) {
    GtkTextBuffer* buf = tab->buffer;
    GtkTextIter ins, bound;
    gtk_text_buffer_get_iter_at_mark(buf, &ins, tab->view_insert);
    gtk_text_buffer_get_iter_at_mark(buf, &bound, tab->view_bound);
    gtk_text_buffer_select_range(buf, &ins, &bound);
}

void editor_view_activate(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL || tab->view_insert == NULL) return;
    EditorTab* active = active_of(tab->buffer);
    if (active == tab) return;
    if (active != NULL) {
        save_cursor(active);
        active->view_passive = TRUE;
    }
    tab->view_passive = FALSE;
    g_object_set_data(G_OBJECT(tab->buffer), ACTIVE_VIEW_KEY, tab);
    if (active != NULL) restore_cursor(tab);
}

/* Another view of the buffer moved the cursor: this view stays where it is */
static gboolean view_is_passive(GtkTextBuffer* buffer, GtkWidget* view) {
    EditorTab* active = active_of(buffer);
    return active != NULL && active->view != view;
}

static void on_editor_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->app != NULL) {
        AppData* app = tab->app;
        editor_view_activate(tab);
        app->last_focused_editor = tab;
        update_active_panel_highlight(app);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
        editor_modes_update_status(tab);
    }
}

#define DRAG_GESTURES_KEY "editor-drag-gestures"
#define POINTER_DOWN_KEY "editor-pointer-down"
#define BUTTONS_MASK (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)

/* The drag gestures of the text view (its selection drag among them), found
 * once: observing the controllers of a widget is slow */
static void remember_drag_gestures(GtkWidget* view) {
    GPtrArray* drags = g_ptr_array_new();
    GListModel* ctrls = gtk_widget_observe_controllers(view);
    for (guint i = 0; i < g_list_model_get_n_items(ctrls); i++) {
        GObject* c = g_list_model_get_item(ctrls, i);
        if (GTK_IS_GESTURE_DRAG(c)) g_ptr_array_add(drags, c); /* owned by the view */
        g_object_unref(c);
    }
    g_object_unref(ctrls);
    g_object_set_data_full(G_OBJECT(view), DRAG_GESTURES_KEY, drags, (GDestroyNotify)g_ptr_array_unref);
}

/* A button release that never reached the view (taken by a popup, a focus
 * change...) leaves its selection drag running: GtkTextView then ignores the
 * cursor keys until the mouse moves the cursor. A key pressed with no button
 * down ends such a drag. */
static void end_stuck_drag(GtkWidget* view, GdkModifierType state) {
    if ((state & BUTTONS_MASK) != 0) return;
    g_object_set_data(G_OBJECT(view), POINTER_DOWN_KEY, NULL);
    GPtrArray* drags = g_object_get_data(G_OBJECT(view), DRAG_GESTURES_KEY);
    for (guint i = 0; drags != NULL && i < drags->len; i++) {
        GtkGesture* g = g_ptr_array_index(drags, i);
        if (gtk_gesture_is_active(g)) gtk_event_controller_reset(GTK_EVENT_CONTROLLER(g));
    }
}

static gboolean on_editor_view_key_press(GtkEventControllerKey* controller, guint keyval,
                                         guint keycode, GdkModifierType state, gpointer data) {
    EditorTab* tab = (EditorTab*)data;
    end_stuck_drag(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)), state);
    if (tab != NULL && tab->app != NULL) {
        if (editor_modes_handle_key(tab->app, tab, keyval, keycode, state)) {
            return TRUE;
        }
    }
    return FALSE;
}


/* GTK scrolls only until the cursor is visible: back at the start of a long
 * line the left padding stays scrolled away, and the cursor rides under the
 * overlay scrollbars at the right and bottom edges. After GTK has scrolled,
 * keep the padding (plus TEXT_SCROLLBAR_CLEARANCE right and bottom) around
 * the cursor, and show the line start with its padding when it fits. */
static void clamp_adjustment(GtkAdjustment* adj, gdouble value) {
    gdouble lower = gtk_adjustment_get_lower(adj);
    gdouble max = MAX(lower, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
    value = CLAMP(value, lower, max);
    if (value != gtk_adjustment_get_value(adj)) {
        gtk_adjustment_set_value(adj, value);
    }
}

/* Tracks whether a mouse button (or touch) is down in the view. Scrolling the
 * text under a held button turns the click into a drag selection, which then
 * moves the cursor and scrolls again: the view runs away and typing replaces
 * the accidental selection. A click places the cursor in view anyway. */
static gboolean on_editor_pointer_event(GtkEventControllerLegacy* ctrl, GdkEvent* event,
                                        gpointer data) {
    GtkWidget* view = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    switch (gdk_event_get_event_type(event)) {
    case GDK_BUTTON_PRESS:
    case GDK_TOUCH_BEGIN:
        /* Before the view places the cursor (and takes the focus) */
        editor_view_activate((EditorTab*)data);
        g_object_set_data(G_OBJECT(view), POINTER_DOWN_KEY, GINT_TO_POINTER(1));
        break;
    case GDK_BUTTON_RELEASE:
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL:
        g_object_set_data(G_OBJECT(view), POINTER_DOWN_KEY, NULL);
        break;
    default:
        break;
    }
    return FALSE; /* observe only */
}

static gboolean keep_cursor_clear(gpointer data) {
    GtkTextView* view = GTK_TEXT_VIEW(data);
    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(view));
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    if (g_object_get_data(G_OBJECT(view), POINTER_DOWN_KEY) != NULL) {
        return G_SOURCE_REMOVE; /* cursor placed with the mouse: leave the view */
    }
    if (hadj == NULL || vadj == NULL || !gtk_widget_get_mapped(GTK_WIDGET(view))) {
        return G_SOURCE_REMOVE;
    }
    GdkRectangle c; /* buffer coordinates, like the adjustments */
    gtk_text_view_get_cursor_locations(view, NULL, &c, NULL);
    const gint pad_left = PREVIEW_TEXT_PADDING_X;
    const gint pad_right = PREVIEW_TEXT_PADDING_X + TEXT_SCROLLBAR_CLEARANCE;
    const gint pad_top = PREVIEW_TEXT_PADDING_Y;
    const gint pad_bottom = PREVIEW_TEXT_PADDING_Y + TEXT_SCROLLBAR_CLEARANCE;
    gint cw = MAX(c.width, 2);

    gdouble hv = gtk_adjustment_get_value(hadj);
    gdouble hp = gtk_adjustment_get_page_size(hadj);
    if (c.x + cw + pad_right <= hp) {
        clamp_adjustment(hadj, gtk_adjustment_get_lower(hadj)); /* line start fits: show it */
    } else if (c.x + cw + pad_right > hv + hp) {
        clamp_adjustment(hadj, c.x + cw + pad_right - hp);
    } else if (c.x - pad_left < hv) {
        clamp_adjustment(hadj, c.x - pad_left);
    }

    gdouble vv = gtk_adjustment_get_value(vadj);
    gdouble vp = gtk_adjustment_get_page_size(vadj);
    if (c.y + c.height + pad_bottom > vv + vp) {
        clamp_adjustment(vadj, c.y + c.height + pad_bottom - vp);
    } else if (c.y - pad_top < vv) {
        clamp_adjustment(vadj, c.y - pad_top);
    }
    return G_SOURCE_REMOVE;
}

/* Typing moves the cursor without "mark-set" */
static void on_editor_text_changed(GtkTextBuffer* buffer, gpointer data) {
    if (view_is_passive(buffer, data)) return;
    g_idle_add_full(G_PRIORITY_LOW, keep_cursor_clear, g_object_ref(data), g_object_unref);
}

static void on_editor_cursor_moved(GtkTextBuffer* buffer, const GtkTextIter* location,
                                   GtkTextMark* mark, gpointer data) {
    (void)location;
    if (mark != gtk_text_buffer_get_insert(buffer) || view_is_passive(buffer, data)) {
        return;
    }
    /* Low priority: after the redraw in which GTK scrolls to the cursor */
    g_idle_add_full(G_PRIORITY_LOW, keep_cursor_clear, g_object_ref(data), g_object_unref);
}

/* While a new view settles, its layout can change under its scroll position:
 * GtkTextView then keeps an internal offset that differs from its vertical
 * adjustment (the adjustment says 0, the view is scrolled to the last line:
 * only that line is drawn, and moving the cursor does nothing as the
 * adjustment already holds the requested value). During the first moments the
 * view is shown, check it and put it back on its cursor. */
#define SCROLL_WATCH_KEY "editor-scroll-watch"
#define SCROLL_WATCH_FRAMES 60

typedef struct {
    guint frames;
    gboolean follow_cursor; /* also bring an off-screen cursor back */
} ScrollWatch;

static void resync_nudge(GtkAdjustment* adj) {
    gdouble lo = gtk_adjustment_get_lower(adj);
    gdouble up = gtk_adjustment_get_upper(adj);
    gdouble page = gtk_adjustment_get_page_size(adj);
    gdouble step = gtk_adjustment_get_step_increment(adj);
    gdouble pinc = gtk_adjustment_get_page_increment(adj);
    /* Two real value changes: the view takes the adjustment's value again */
    gtk_adjustment_configure(adj, lo + 1, lo, MAX(up, page + 2), step, pinc, page);
    gtk_adjustment_configure(adj, lo, lo, up, step, pinc, page);
}

static gboolean scroll_watch_tick(GtkWidget* widget, GdkFrameClock* clock, gpointer data) {
    (void)clock;
    ScrollWatch* w = data;
    if (!gtk_widget_get_mapped(widget) || gtk_widget_get_height(widget) <= 1) {
        return G_SOURCE_CONTINUE; /* a background tab: wait until it is shown */
    }
    GtkTextView* view = GTK_TEXT_VIEW(widget);
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    if (++w->frames > SCROLL_WATCH_FRAMES || vadj == NULL) {
        g_object_set_data(G_OBJECT(widget), SCROLL_WATCH_KEY, NULL);
        return G_SOURCE_REMOVE;
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);
    GtkTextMark* insert = gtk_text_buffer_get_insert(buffer);
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(view, &visible);
    gdouble offset = visible.y + gtk_text_view_get_top_margin(view);
    if (fabs(offset - gtk_adjustment_get_value(vadj)) > 2.0) {
        resync_nudge(vadj);
        /* The offset was wrong: whatever was shown, show the cursor */
        gtk_text_view_scroll_to_mark(view, insert, 0.0, TRUE, 0.0, w->follow_cursor ? 0.3 : 0.5);
        return G_SOURCE_CONTINUE;
    }
    if (w->follow_cursor) {
        GtkTextIter ins;
        gtk_text_buffer_get_iter_at_mark(buffer, &ins, insert);
        gint y = 0, h = 0;
        gtk_text_view_get_line_yrange(view, &ins, &y, &h);
        if (y < visible.y || y + h > visible.y + visible.height) {
            gtk_text_view_scroll_to_mark(view, insert, 0.0, TRUE, 0.0, 0.3);
        }
    }
    return G_SOURCE_CONTINUE; /* the layout may still move for a few frames */
}

void editor_view_watch_scroll(EditorTab* tab, gboolean follow_cursor) {
    if (tab == NULL || tab->view == NULL) return;
    ScrollWatch* w = g_object_get_data(G_OBJECT(tab->view), SCROLL_WATCH_KEY);
    if (w == NULL) {
        w = g_new0(ScrollWatch, 1);
        /* The tick owns w: the key only finds it again */
        g_object_set_data(G_OBJECT(tab->view), SCROLL_WATCH_KEY, w);
        gtk_widget_add_tick_callback(tab->view, scroll_watch_tick, w, g_free);
    }
    w->frames = 0;
    w->follow_cursor = w->follow_cursor || follow_cursor;
}

void editor_view_build(AppData* app, EditorTab* tab, GtkTextBuffer* buffer) {
#ifdef HAVE_GSV
    GtkWidget* view =
        gtk_source_view_new_with_buffer(GTK_SOURCE_BUFFER(buffer));
#else
    GtkWidget* view = gtk_text_view_new_with_buffer(buffer);
#endif
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_X);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_X + TEXT_SCROLLBAR_CLEARANCE);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_Y);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_Y + TEXT_SCROLLBAR_CLEARANCE);
    /* File contents in the code font, in every editor mode (as in the preview) */
    set_view_monospace(view, TRUE);
#ifdef HAVE_GSV
    apply_vscode_scheme(buffer, view);
#endif

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);

    tab->buffer = buffer;
    tab->scroll = scroll;
    tab->view = view;
    GtkTextIter ins, bound;
    gtk_text_buffer_get_iter_at_mark(buffer, &ins, gtk_text_buffer_get_insert(buffer));
    gtk_text_buffer_get_iter_at_mark(buffer, &bound, gtk_text_buffer_get_selection_bound(buffer));
    tab->view_insert = gtk_text_buffer_create_mark(buffer, NULL, &ins, FALSE);
    tab->view_bound = gtk_text_buffer_create_mark(buffer, NULL, &bound, FALSE);
    if (active_of(buffer) == NULL) {
        g_object_set_data(G_OBJECT(buffer), ACTIVE_VIEW_KEY, tab);
    } else {
        tab->view_passive = TRUE; /* a split view: until it is focused */
    }

    GtkEventController* focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "enter", G_CALLBACK(on_editor_focus_enter), tab);
    gtk_widget_add_controller(view, focus_ctrl);

    remember_drag_gestures(view);
    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_editor_view_key_press), tab);
    gtk_widget_add_controller(view, key_ctrl);

    GtkEventController* pointer_ctrl = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(pointer_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(pointer_ctrl, "event", G_CALLBACK(on_editor_pointer_event), tab);
    gtk_widget_add_controller(view, pointer_ctrl);

    diff_gutter_attach(view, TRUE);
    diff_gutter_set_file(view, tab->path);
    /* Preferences of new tabs */
    if (app->settings.line_numbers) {
        diff_gutter_set_line_numbers(view, 0);
    }
    if (app->settings.wrap) {
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    }

    tab->minimap = minimap_new_for_view(view, "editor");
    diff_gutter_add_companion(view, tab->minimap);

    GtkWidget* container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(container, TRUE);
    gtk_widget_set_vexpand(container, TRUE);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(hbox, TRUE);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_box_append(GTK_BOX(hbox), color_column_wrap(scroll, view));
    color_column_set(view, app->settings.color_column);
    if (tab->minimap != NULL) {
        gtk_box_append(GTK_BOX(hbox), tab->minimap);
    }
    gtk_box_append(GTK_BOX(container), editor_tagbar_wrap(app, tab, hbox));

    /* Status line for this editor */
    /* Status line for this editor */
    tab->status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(tab->status_bar, "editor-statusbar");
    GtkWidget* stat_lbl = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(stat_lbl), 0.0f);
    gtk_widget_set_hexpand(stat_lbl, TRUE);
    if (is_org_file(tab->name) || tab->mode == EDITOR_MODE_ORGMODE) {
        gtk_label_set_text(GTK_LABEL(stat_lbl), _("Org-mode (Emacs keys) | Tab/S-Tab: fold | M-RET: heading | M-arrows: structure | S-arrows: TODO | C-c C-c"));
    } else if (tab->mode == EDITOR_MODE_VIM) {
        gtk_label_set_text(GTK_LABEL(stat_lbl), "VIM -- NORMAL --");
    } else if (tab->mode == EDITOR_MODE_EMACS) {
        gtk_label_set_text(GTK_LABEL(stat_lbl), _("Emacs mode"));
    } else {
        gtk_label_set_text(GTK_LABEL(stat_lbl), _("Notepad mode"));
    }
    g_object_set_data(G_OBJECT(tab->status_bar), "label", stat_lbl);
    gtk_box_append(GTK_BOX(tab->status_bar), stat_lbl);
    gtk_box_append(GTK_BOX(container), tab->status_bar);
    tab->container = container;
    editor_modes_init_tab(app, tab);

    g_signal_connect(buffer, "mark-set", G_CALLBACK(on_editor_cursor_moved), view);
    g_signal_connect_after(buffer, "changed", G_CALLBACK(on_editor_text_changed), view);
    editor_view_watch_scroll(tab, FALSE);
}

void editor_view_release(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return;
    /* The buffer stays alive while another view shows it: no handler of this
     * tab or its view may run afterwards */
    code_help_detach(tab);
    editor_tagbar_release(tab);
    g_signal_handlers_disconnect_by_data(tab->buffer, tab);
    if (tab->view != NULL) {
        g_signal_handlers_disconnect_by_data(tab->buffer, tab->view);
    }
    if (active_of(tab->buffer) == tab) {
        /* The view left takes its own cursor back */
        EditorTab* twin = editor_view_twin(tab->app, tab);
        g_object_set_data(G_OBJECT(tab->buffer), ACTIVE_VIEW_KEY, twin);
        if (twin != NULL) {
            twin->view_passive = FALSE;
            restore_cursor(twin);
        }
    }
    if (tab->view_insert != NULL) {
        gtk_text_buffer_delete_mark(tab->buffer, tab->view_insert);
        gtk_text_buffer_delete_mark(tab->buffer, tab->view_bound);
        tab->view_insert = tab->view_bound = NULL;
    }
}

EditorTab* editor_view_twin(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL || tab->buffer == NULL) return NULL;
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t != NULL && t != tab && t->buffer == tab->buffer) return t;
    }
    return NULL;
}
