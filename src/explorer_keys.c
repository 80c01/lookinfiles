/* Keys of the explorer panel. Navigation follows the global mode (nav_keys.h);
 * the view actions (a, d, r, o...) are the same in every mode. */
#include "explorer_keys.h"
#include "file_list.h"
#include "tree_nav.h"
#include "panels.h"
#include "app_events.h"
#include "search.h"
#include "help.h"
#include "image_preview.h"
#include "video.h"
#include "audio.h"
#include "bookmarks.h"
#include "notes.h"
#include "remote.h"
#include "editor.h"
#include "focus_route.h"

/* Rows with children the arrow keys and space expand: directories,
 * bookmark folders, notebooks and the notes trash */
static gboolean is_container_row(ExplorerRow* row) {
    return explorer_row_is(row, "dir") || explorer_row_is(row, "bm_folder") ||
           explorer_row_is(row, "notebook") || explorer_row_is(row, "trash");
}

/* Files view (not bookmarks / notes / remote) */
static gboolean is_files_view(AppData* app) {
    return app->sidebar_view == SIDEBAR_VIEW_FILES && !app->show_bookmarks;
}

/* Right / l / C-f: open a bookmark, expand a folder or step into its first child */
static void step_forward(AppData* app) {
    ExplorerRow* row = tree_nav_selected(app);
    if (explorer_row_is(row, "bookmark")) {
        bookmarks_activate_selected(app);
    } else if (is_container_row(row)) {
        if (!explorer_tree_is_expanded(app->tree, row)) {
            explorer_tree_set_expanded(app->tree, row, TRUE);
        } else if (explorer_row_n_children(row) > 0) {
            select_row(app, explorer_row_child(row, 0));
        }
    }
}

/* Left / h / C-b: collapse a folder or jump to the parent row */
static void step_backward(AppData* app) {
    ExplorerRow* row = tree_nav_selected(app);
    if (is_container_row(row) && explorer_tree_is_expanded(app->tree, row)) {
        explorer_tree_set_expanded(app->tree, row, FALSE);
    } else if (row != NULL && explorer_row_get_parent(row) != NULL) {
        select_row(app, explorer_row_get_parent(row));
    }
}

/* Vim t / Emacs C-o: edit the selected file in the dedicated editor panel */
static void open_selected_dedicated(AppData* app, gboolean focus_editor) {
    ExplorerRow* row = tree_nav_selected(app);
    if (!explorer_row_is(row, "file") || row->path == NULL || row->path[0] == '\0') {
        return;
    }
    gchar* name = g_strdup(row->name); /* the row may go away while opening */
    gchar* full = g_strdup(row->path);
    editor_open_dedicated(app, name, full, focus_editor ? NULL : app->tree_view);
    g_free(name);
    g_free(full);
}

static void run_nav(AppData* app, NavAction action) {
    switch (action) {
    case NAV_DOWN: select_next_row(app); break;
    case NAV_UP: select_prev_row(app); break;
    case NAV_FORWARD: step_forward(app); break;
    case NAV_BACKWARD: step_backward(app); break;
    case NAV_FIRST: select_first_row(app); break;
    case NAV_LAST: select_last_row(app); break;
    case NAV_PAGE_DOWN: page_down_rows(app); break;
    case NAV_PAGE_UP: page_up_rows(app); break;
    case NAV_CENTER: center_selected_row(app); break;
    case NAV_TOP: top_selected_row(app); break;
    case NAV_BOTTOM: bottom_selected_row(app); break;
    case NAV_RECENTER:
        /* Emacs C-l: center, top, bottom in turn */
        if (app->nav_recenter_step == 1) top_selected_row(app);
        else if (app->nav_recenter_step == 2) bottom_selected_row(app);
        else center_selected_row(app);
        app->nav_recenter_step = (app->nav_recenter_step + 1) % 3;
        break;
    case NAV_SEARCH: show_search(app, FALSE, FALSE); break;
    case NAV_SEARCH_BACKWARD: show_search(app, FALSE, TRUE); break;
    case NAV_SEARCH_NEXT: do_search(app, +1); break;
    case NAV_SEARCH_PREV: do_search(app, -1); break;
    case NAV_PANEL_NEXT: app_emit(app, APP_EVENT_FOCUS_NEXT_PANEL); break;
    case NAV_PANEL_PREV: app_emit(app, APP_EVENT_FOCUS_PREV_PANEL); break;
    case NAV_OPEN_DEDICATED: open_selected_dedicated(app, TRUE); break;
    case NAV_DISPLAY_DEDICATED: open_selected_dedicated(app, FALSE); break;
    /* The tabs of the preview notebook; the explorer keeps the focus */
    case NAV_TAB_NEXT: focus_route_step_tab(app, GTK_NOTEBOOK(app->notebook), 1, FALSE); break;
    case NAV_TAB_PREV: focus_route_step_tab(app, GTK_NOTEBOOK(app->notebook), -1, FALSE); break;
    default: break; /* NAV_PENDING, NAV_CANCEL */
    }
    if (action != NAV_RECENTER) {
        app->nav_recenter_step = 0;
    }
}

/* Video or audio preview shown: play / pause it */
static gboolean toggle_media(AppData* app) {
    if (app->viewed_name == NULL || app->render_raw) return FALSE;
    if (app->viewed_kind == FILE_KIND_VIDEO) {
        video_preview_toggle_play(app);
        return TRUE;
    }
    if (app->viewed_kind == FILE_KIND_AUDIO) {
        audio_preview_toggle_play(app);
        return TRUE;
    }
    return FALSE;
}

/* Keys that are not navigation: view actions, the same in every mode */
static gboolean handle_action_key(AppData* app, guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    switch (keyval) {
    case GDK_KEY_space: {
        ExplorerRow* row = tree_nav_selected(app);
        if (is_container_row(row)) {
            explorer_tree_toggle_expanded(app->tree, row);
        } else if (row != NULL) {
            toggle_media(app);
        }
        return TRUE;
    }
    case GDK_KEY_p:
    case GDK_KEY_P:
        return toggle_media(app);
    case GDK_KEY_Return: /* Enter selected directory or open editor tab */
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
        if (ctrl) {
            /* Ctrl+Enter: enter directory as root */
            ExplorerRow* row = tree_nav_selected(app);
            if (explorer_row_is(row, "dir")) {
                gchar* full = g_strdup(row->path); /* the reload clears the tree */
                reload_directory(app, full);
                g_free(full);
            }
        } else {
            activate_selected_entry(app);
        }
        return TRUE;
    case GDK_KEY_BackSpace:
        go_to_parent_directory(app);
        return TRUE;
    case GDK_KEY_question: /* ?: toggle internal keybindings help panel */
        if (ctrl) return FALSE;
        toggle_help_panel(app);
        return TRUE;
    case GDK_KEY_I: /* Toggle hidden files (files view only) */
    case GDK_KEY_i:
        if (is_files_view(app)) {
            toggle_hidden_files(app);
        }
        return TRUE;
    case GDK_KEY_o: /* Cycle the sort of the current view */
        if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->show_bookmarks) {
            bookmarks_toggle_sort(app);
        } else if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            notes_toggle_sort(app);
        } else if (is_files_view(app)) {
            cycle_sort_mode(app); /* name -> date -> size -> ext */
        }
        return TRUE;
    case GDK_KEY_d: /* Ctrl+D: bookmark selected entry; d: move selected file to trash */
    case GDK_KEY_D:
        if (ctrl) {
            bookmarks_add_from_context(app);
            return TRUE;
        }
        if (keyval == GDK_KEY_d && is_files_view(app)) {
            trash_selected_in_tree(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Delete: /* Delete: move selected file or folder to trash */
    case GDK_KEY_KP_Delete:
        if (is_files_view(app)) {
            trash_selected_in_tree(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_a: /* Ctrl+A: switch files / bookmarks views; a / Shift+A: new file / folder */
    case GDK_KEY_A:
        if (ctrl) {
            toggle_bookmarks_view(app);
            return TRUE;
        }
        if (is_files_view(app)) {
            if (keyval == GDK_KEY_A) {
                create_new_folder_in_tree(app);
            } else {
                create_new_file_in_tree(app);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_e: /* e: rename selected file or folder (like F2 / r) */
        if (is_files_view(app) && !ctrl) {
            rename_selected_in_tree(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_u: /* Ctrl+U: toggle notes view in explorer panel */
    case GDK_KEY_U:
        if (ctrl) {
            toggle_notes_view(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_r: /* r in notes view / Ctrl+R / F2: rename selected item */
    case GDK_KEY_R:
        if (ctrl || app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
                notes_edit_selected(app);
            }
            return TRUE;
        }
        if (keyval == GDK_KEY_r && is_files_view(app)) {
            rename_selected_in_tree(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_F2:
        if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            notes_edit_selected(app);
        } else if (is_files_view(app)) {
            rename_selected_in_tree(app);
        }
        return TRUE;
    case GDK_KEY_f: /* f alone: fullscreen (Ctrl+F is a navigation key) */
        if (ctrl) return FALSE;
        toggle_fullscreen(app->window);
        return TRUE;
    case GDK_KEY_m:
    case GDK_KEY_M:
        if (app->viewed_name != NULL && file_kind_is_renderable(app->viewed_kind)) {
            app->render_raw = !app->render_raw;
            app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            if (app->viewed_path != NULL) {
                app_emit(app, APP_EVENT_PREVIEW_STALE);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Escape:
        if (app->help_window != NULL && gtk_widget_get_visible(app->help_window)) {
            toggle_help_panel(app);
            return TRUE;
        }
        return FALSE;
    default:
        return FALSE;
    }
}

gboolean on_tree_key_press(GtkEventControllerKey* controller, guint keyval,
                           guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    AppData* app = (AppData*)data;
    NavPrefix prefix = (NavPrefix)app->nav_prefix;
    NavPrefix next = NAV_PREFIX_NONE;
    NavAction action = nav_keys_lookup(app_nav_style(app), prefix, keyval, state, &next);

    /* The key after C-x / Ctrl+W belongs to the prefix, whatever it is */
    if (prefix == NAV_PREFIX_CTRL_X || prefix == NAV_PREFIX_CTRL_W) {
        app->nav_prefix = next;
        run_nav(app, action);
        return TRUE;
    }

    /* Bookmark manager keys (add, new folder, edit, delete, reorder) */
    if (bookmarks_handle_key(app, keyval, state)) {
        return TRUE;
    }

    /* Notes manager keys (new note, new notebook, edit, move, delete, restore, reorder) */
    if (notes_handle_key(app, keyval, state)) {
        return TRUE;
    }

    /* Remote manager keys (connect, terminal, add, edit, delete) */
    if (remote_handle_key(app, keyval, state)) {
        return TRUE;
    }

    /* Zoom of the previewed image or PDF (+ / - / 0) */
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0 && image_preview_zoom_key(app, keyval)) {
        app->nav_prefix = NAV_PREFIX_NONE;
        return TRUE;
    }

    app->nav_prefix = next;
    if (action != NAV_NONE) {
        run_nav(app, action);
        return TRUE;
    }
    app->nav_recenter_step = 0;
    return handle_action_key(app, keyval, state);
}
