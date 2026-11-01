/* The text view of an editor tab (see editor_view.h) */
#include "editor_view.h"
#include "editor_modes.h"
#include "app_events.h"
#include "panels.h"
#include "text_style.h"
#include "minimap.h"
#include "diff_gutter.h"
#include "code_help.h"
#include "file_types.h"

static void on_editor_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->app != NULL) {
        AppData* app = (AppData*)tab->app;
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
        if (editor_modes_handle_key((AppData*)tab->app, tab, keyval, keycode, state)) {
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
    (void)data;
    GtkWidget* view = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl));
    switch (gdk_event_get_event_type(event)) {
    case GDK_BUTTON_PRESS:
    case GDK_TOUCH_BEGIN:
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
    (void)buffer;
    g_idle_add_full(G_PRIORITY_LOW, keep_cursor_clear, g_object_ref(data), g_object_unref);
}

static void on_editor_cursor_moved(GtkTextBuffer* buffer, const GtkTextIter* location,
                                   GtkTextMark* mark, gpointer data) {
    (void)location;
    if (mark != gtk_text_buffer_get_insert(buffer)) {
        return;
    }
    /* Low priority: after the redraw in which GTK scrolls to the cursor */
    g_idle_add_full(G_PRIORITY_LOW, keep_cursor_clear, g_object_ref(data), g_object_unref);
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
    g_signal_connect(pointer_ctrl, "event", G_CALLBACK(on_editor_pointer_event), NULL);
    gtk_widget_add_controller(view, pointer_ctrl);

    diff_gutter_attach(view, TRUE);
    diff_gutter_set_file(view, tab->path);
    /* Preferences of new tabs */
    if (app->editor_line_numbers) {
        diff_gutter_set_line_numbers(view, 0);
    }
    if (app->editor_wrap) {
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    }

    editor_minimap_init(tab);

    GtkWidget* container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(container, TRUE);
    gtk_widget_set_vexpand(container, TRUE);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(hbox, TRUE);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_box_append(GTK_BOX(hbox), scroll);
    if (tab->minimap != NULL) {
        gtk_box_append(GTK_BOX(hbox), tab->minimap);
    }
    gtk_box_append(GTK_BOX(container), hbox);

    /* Status line for this editor */
    /* Status line for this editor */
    tab->status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(tab->status_bar, "editor-statusbar");
    GtkWidget* stat_lbl = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(stat_lbl), 0.0f);
    gtk_widget_set_hexpand(stat_lbl, TRUE);
    if (is_org_file(tab->name) || tab->mode == EDITOR_MODE_ORGMODE) {
        gtk_label_set_text(GTK_LABEL(stat_lbl), "Org-mode (Emacs keys) | Tab/S-Tab: fold | M-RET: heading | M-arrows: structure | S-arrows: TODO | C-c C-c");
    } else if (tab->mode == EDITOR_MODE_VIM) {
        gtk_label_set_text(GTK_LABEL(stat_lbl), "VIM -- NORMAL --");
    } else if (tab->mode == EDITOR_MODE_EMACS) {
        gtk_label_set_text(GTK_LABEL(stat_lbl), "Emacs mode");
    } else {
        gtk_label_set_text(GTK_LABEL(stat_lbl), "Notepad mode");
    }
    g_object_set_data(G_OBJECT(tab->status_bar), "label", stat_lbl);
    gtk_box_append(GTK_BOX(tab->status_bar), stat_lbl);
    gtk_box_append(GTK_BOX(container), tab->status_bar);
    tab->container = container;
    editor_modes_init_tab(app, tab);

    g_signal_connect(buffer, "mark-set", G_CALLBACK(on_editor_cursor_moved), view);
    g_signal_connect_after(buffer, "changed", G_CALLBACK(on_editor_text_changed), view);
}

void editor_view_release(EditorTab* tab) {
    if (tab == NULL || tab->buffer == NULL) return;
    /* The buffer stays alive while another view shows it: no handler of this
     * tab or its view may run afterwards */
    code_help_detach(tab);
    g_signal_handlers_disconnect_by_data(tab->buffer, tab);
    if (tab->view != NULL) {
        g_signal_handlers_disconnect_by_data(tab->buffer, tab->view);
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
