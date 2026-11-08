#include "notes.h"
#include "notes_private.h"
#include "core/i18n.h"
#include "note_types.h"
#include "notes_store.h"
#include "canvas/canvas_view.h"
#include "palette.h"
#include "core/app_paths.h"
#include "core/app_events.h"
#include "tree_nav.h"
#include "editor.h"

#include <string.h>

enum {
    NOTE_ACTION_OPEN = 1,
    NOTE_ACTION_NEW_NOTE,
    NOTE_ACTION_NEW_NOTEBOOK,
    NOTE_ACTION_RENAME,
    NOTE_ACTION_MOVE,
    NOTE_ACTION_DELETE,
    NOTE_ACTION_RESTORE,
    NOTE_ACTION_EMPTY_TRASH,
    NOTE_ACTION_MOVE_UP,
    NOTE_ACTION_MOVE_DOWN,
    NOTE_ACTION_SORT,
    NOTE_ACTION_COLLAPSE,
    NOTE_ACTION_CONVERT_TEXT,
    NOTE_ACTION_CONVERT_MARKDOWN,
    NOTE_ACTION_CONVERT_ORG,
    NOTE_ACTION_EXPORT_MARKDOWN
};

NotesManager* notes_mgr_of(AppData* app) {
    return (app != NULL) ? app->notes_mgr : NULL;
}

gchar* notes_get_root_dir(void) {
    gchar* cfg_dir = app_config_dir();
    gchar* dir = g_build_filename(cfg_dir, "notes", NULL);
    g_free(cfg_dir);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

gchar* notes_get_trash_dir(void) {
    gchar* root = notes_get_root_dir();
    gchar* trash = g_build_filename(root, NOTES_STORE_TRASH, NULL);
    g_mkdir_with_parents(trash, 0755);
    g_free(root);
    return trash;
}

/* ------------------------------------------------------------------------- */
/* Saving                                                                    */
/* ------------------------------------------------------------------------- */

void notes_save(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;
    GError* error = NULL;
    if (!notes_store_save(mgr, &error)) {
        g_printerr("[ Notes manifest not saved: %s ]\n", error->message);
        g_clear_error(&error);
    }
}

/* ------------------------------------------------------------------------- */
/* Initialization and legacy migration                                       */
/* ------------------------------------------------------------------------- */

static void notes_migrate_legacy_buffers(void) {
    gchar* cfg_dir = app_config_dir();
    gchar* old_buf_dir = g_build_filename(cfg_dir, "buffers", NULL);
    g_free(cfg_dir);
    if (!g_file_test(old_buf_dir, G_FILE_TEST_IS_DIR)) {
        g_free(old_buf_dir);
        return;
    }

    gchar* notes_root = notes_get_root_dir();
    gchar* default_nb_dir = g_build_filename(notes_root, "General", NULL);
    g_mkdir_with_parents(default_nb_dir, 0755);

    GDir* d = g_dir_open(old_buf_dir, 0, NULL);
    if (d != NULL) {
        const gchar* name = NULL;
        while ((name = g_dir_read_name(d)) != NULL) {
            if (name[0] == '.') continue;
            gchar* old_item = g_build_filename(old_buf_dir, name, NULL);
            if (g_file_test(old_item, G_FILE_TEST_IS_DIR)) {
                /* Directory in buffers becomes a notebook */
                gchar* target_nb = g_build_filename(notes_root, name, NULL);
                g_rename(old_item, target_nb);
                g_free(target_nb);
            } else {
                /* File moves to General */
                gchar* target_file = g_build_filename(default_nb_dir, name, NULL);
                g_rename(old_item, target_file);
                g_free(target_file);
            }
            g_free(old_item);
        }
        g_dir_close(d);
    }
    g_rmdir(old_buf_dir);
    g_free(default_nb_dir);
    g_free(notes_root);
    g_free(old_buf_dir);
}

void notes_init(AppData* app) {
    if (app == NULL) return;
    notes_migrate_legacy_buffers();
    gchar* root = notes_get_root_dir();
    app->notes_mgr = notes_store_load(root);
    g_free(root);
    notes_save(app);
}

void notes_free(AppData* app) {
    if (app == NULL) return;
    g_clear_pointer(&app->notes_mgr, notes_store_free);
}

/* ------------------------------------------------------------------------- */
/* Explorer Rows                                                             */
/* ------------------------------------------------------------------------- */

/* A notebook row shows the icon its user chose */
static void apply_notebook_icon(ExplorerRow* row, const NotebookItem* nb) {
    explorer_row_set_glyph(row, nb->icon);
    explorer_row_set_label(row, nb->name);
}

/* A note shows the VS Code icon of its type's extension (.md, .org...) */
static void apply_note_icon(ExplorerRow* row, NoteType type) {
    gchar* name = g_strconcat("note", note_type_ext(type), NULL);
    explorer_row_set_icon_name(row, name);
    g_free(name);
}

void notes_append_rows(ExplorerRow* root, AppData* app) {
    if (root == NULL || app == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        return;
    }
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    /* 1. Notebooks */
    for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
        NotebookItem* nb = (NotebookItem*)l->data;
        ExplorerRow* nb_row = explorer_row_append_container(root, "notebook", nb->name, nb->name, nb->path);
        apply_notebook_icon(nb_row, nb);
        guint n_notes = g_list_length(nb->notes);
        if (n_notes > 0) {
            gchar* badge = g_strdup_printf("<span foreground=\"%s\">%u</span>", palette_color(PAL_FG_FAINT), n_notes);
            explorer_row_set_badge(nb_row, badge);
            g_free(badge);
        }

        if (nb->notes == NULL) {
            explorer_row_append(nb_row, "-", "", _("  (empty)"), "");
        } else {
            for (GList* nl = nb->notes; nl != NULL; nl = nl->next) {
                NoteItem* note = (NoteItem*)nl->data;
                ExplorerRow* row = explorer_row_append(nb_row, "note", note->title, note->title, note->path);
                apply_note_icon(row, note->type);
                const gchar* badge = note_type_badge(note->type);
                PaletteRole badge_role = (note->type == NOTE_TYPE_ORG)      ? PAL_SYN_CONTROL
                                         : (note->type == NOTE_TYPE_CANVAS) ? PAL_SYN_FUNCTION
                                                                            : PAL_SYN_TYPE;
                gchar* type_badge = (badge[0] != '\0')
                    ? g_markup_printf_escaped("<span foreground=\"%s\" size=\"small\">%s</span>",
                                              palette_color(badge_role), badge)
                    : g_strdup("");
                explorer_row_set_badge(row, type_badge);
                g_free(type_badge);
            }
        }
    }

    /* 2. Trash Bin */
    gchar* trash_dir = notes_get_trash_dir();
    guint n_trash = g_list_length(mgr->trash);
    gchar* t_label = g_strdup(n_trash == 0 ? _("Trash (empty)") : _("Trash"));
    ExplorerRow* trash_row = explorer_row_append_container(root, "trash", "Trash", t_label, trash_dir);
    explorer_row_set_glyph(trash_row, "trash");
    g_free(trash_dir);
    g_free(t_label);
    if (n_trash > 0) {
        gchar* t_badge = g_strdup_printf("<span foreground=\"%s\">%u</span>", palette_color(PAL_FG_FAINT), n_trash);
        explorer_row_set_badge(trash_row, t_badge);
        g_free(t_badge);
    }

    if (mgr->trash == NULL) {
        explorer_row_append(trash_row, "-", "", _("  (trash is empty)"), "");
    } else {
        for (GList* tl = mgr->trash; tl != NULL; tl = tl->next) {
            TrashNoteItem* tnote = (TrashNoteItem*)tl->data;
            gchar* origin_badge = g_markup_printf_escaped(_("<span foreground=\"%s\" size=\"small\">from %s</span>"),
                                                          palette_color(PAL_FG_FAINT),
                                                          tnote->original_nb ? tnote->original_nb : "General");
            ExplorerRow* row = explorer_row_append(trash_row, "trash_note", tnote->title, tnote->title, tnote->path);
            apply_note_icon(row, tnote->type);
            explorer_row_set_badge(row, origin_badge);
            g_free(origin_badge);
        }
    }
}

void notes_reexpand(AppData* app) {
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        return;
    }
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    ExplorerRow* root = explorer_tree_get_root(app->tree);
    for (guint i = 0; i < explorer_row_n_children(root); i++) {
        ExplorerRow* row = explorer_row_child(root, i);
        if (explorer_row_is(row, "notebook")) {
            for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
                NotebookItem* nb = (NotebookItem*)l->data;
                if (g_strcmp0(nb->name, row->name) == 0 && nb->expanded) {
                    explorer_tree_set_expanded(app->tree, row, TRUE);
                    break;
                }
            }
        } else if (explorer_row_is(row, "trash") && mgr->trash_expanded) {
            explorer_tree_set_expanded(app->tree, row, TRUE);
        }
    }
}

gboolean notes_handle_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded) {
    if (app == NULL || row == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        return FALSE;
    }
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return FALSE;

    if (explorer_row_is(row, "notebook")) {
        for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
            NotebookItem* nb = (NotebookItem*)l->data;
            if (g_strcmp0(nb->name, row->name) == 0) {
                nb->expanded = expanded;
                notes_save(app);
                apply_notebook_icon(row, nb);
                break;
            }
        }
        return TRUE;
    } else if (explorer_row_is(row, "trash")) {
        mgr->trash_expanded = expanded;
        notes_save(app);
        return TRUE;
    }
    return FALSE;
}

/* Select the row of a note or notebook by path, showing it if needed */
void notes_select_path(AppData* app, const gchar* path) {
    ExplorerRow* row = explorer_row_find_by_path(explorer_tree_get_root(app->tree), path);
    reveal_row(app, row);
}

void notes_refresh(AppData* app) {
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        return;
    }

    ExplorerRow* selected = tree_nav_selected(app);
    gchar* sel_path = (selected != NULL && selected->path[0] != '\0') ? g_strdup(selected->path) : NULL;

    tree_nav_block_selection_handler(app);
    explorer_tree_clear(app->tree);
    notes_append_rows(explorer_tree_get_root(app->tree), app);
    notes_reexpand(app);
    tree_nav_unblock_selection_handler(app);

    ExplorerRow* row = explorer_row_find_by_path(explorer_tree_get_root(app->tree), sel_path);
    if (row != NULL) {
        /* The same row again (e.g. a note saved): its preview is re-rendered
         * without bringing the preview tab in front of the editor */
        if (reveal_row_quietly(app, row)) {
            app_emit(app, APP_EVENT_PREVIEW_STALE);
        } else {
            select_first_row(app);
        }
    } else {
        select_first_row(app);
    }
    g_free(sel_path);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

/* ------------------------------------------------------------------------- */
/* Selection Helpers                                                         */
/* ------------------------------------------------------------------------- */

NotesSelectionType notes_selected_info(AppData* app, NotebookItem** out_nb, NoteItem** out_note, TrashNoteItem** out_trash) {
    if (out_nb) *out_nb = NULL;
    if (out_note) *out_note = NULL;
    if (out_trash) *out_trash = NULL;
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) return SEL_NONE;

    ExplorerRow* row = tree_nav_selected(app);
    if (row == NULL) return SEL_NONE;

    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return SEL_NONE;

    const gchar* type = row->type;
    const gchar* path = row->path;
    const gchar* name = row->name;

    NotesSelectionType res = SEL_NONE;
    if (g_strcmp0(type, "notebook") == 0) {
        for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
            NotebookItem* nb = (NotebookItem*)l->data;
            if (g_strcmp0(nb->name, name) == 0 || (path != NULL && g_strcmp0(nb->path, path) == 0)) {
                if (out_nb) *out_nb = nb;
                res = SEL_NOTEBOOK;
                break;
            }
        }
    } else if (g_strcmp0(type, "note") == 0 && path != NULL) {
        for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
            NotebookItem* nb = (NotebookItem*)l->data;
            for (GList* nl = nb->notes; nl != NULL; nl = nl->next) {
                NoteItem* note = (NoteItem*)nl->data;
                if (g_strcmp0(note->path, path) == 0) {
                    if (out_nb) *out_nb = nb;
                    if (out_note) *out_note = note;
                    res = SEL_NOTE;
                    break;
                }
            }
            if (res == SEL_NOTE) break;
        }
    } else if (g_strcmp0(type, "trash") == 0) {
        res = SEL_TRASH;
    } else if (g_strcmp0(type, "trash_note") == 0 && path != NULL) {
        for (GList* tl = mgr->trash; tl != NULL; tl = tl->next) {
            TrashNoteItem* t = (TrashNoteItem*)tl->data;
            if (g_strcmp0(t->path, path) == 0) {
                if (out_trash) *out_trash = t;
                res = SEL_TRASH_NOTE;
                break;
            }
        }
    }
    return res;
}

void notes_update_open_editors(AppData* app, const gchar* old_path, const gchar* new_path, const gchar* new_name) {
    if (app == NULL || old_path == NULL || new_path == NULL) return;
    for (gint w = 0; w < app->num_workspaces; w++) {
        for (GList* l = app->workspaces[w].editors; l != NULL; l = l->next) {
            EditorTab* tab = (EditorTab*)l->data;
            if (tab != NULL && tab->path != NULL) {
                if (g_strcmp0(tab->path, old_path) == 0) {
                    g_free(tab->path);
                    tab->path = g_strdup(new_path);
                    if (new_name != NULL) {
                        g_free(tab->name);
                        tab->name = g_strdup(new_name);
                    }
                    editor_tab_label_update(tab);
                    if (new_name != NULL) {
                        editor_apply_language(tab->buffer, new_name);
                    }
                } else if (g_str_has_prefix(tab->path, old_path) &&
                           (tab->path[strlen(old_path)] == '/' || tab->path[strlen(old_path)] == '\0')) {
                    gchar* sub = g_strdup_printf("%s%s", new_path, tab->path + strlen(old_path));
                    g_free(tab->path);
                    tab->path = sub;
                }
            }
        }
    }
    canvas_view_path_moved(app, old_path, new_path, new_name);
}

/* ------------------------------------------------------------------------- */
/* Keyboard Handler                                                          */
/* ------------------------------------------------------------------------- */

gboolean notes_handle_key(AppData* app, guint keyval, GdkModifierType state) {
    if (app == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        return FALSE;
    }
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    if ((state & GDK_ALT_MASK) != 0) {
        return FALSE;
    }

    switch (keyval) {
    case GDK_KEY_a:
        if (ctrl) return FALSE; /* Ctrl+A: toggle bookmarks */
        notes_new_note(app);
        break;
    case GDK_KEY_A:
        if (ctrl) return FALSE;
        notes_new_notebook(app);
        break;
    case GDK_KEY_r:
    case GDK_KEY_R:
        if (ctrl) {
            notes_edit_selected(app);
        } else if (keyval == GDK_KEY_R) {
            notes_restore_selected(app);
        } else {
            notes_edit_selected(app);
        }
        break;
    case GDK_KEY_F2:
    case GDK_KEY_e:
        notes_edit_selected(app);
        break;
    case GDK_KEY_m:
        notes_move_selected(app);
        break;
    case GDK_KEY_d:
    case GDK_KEY_D:
        if (ctrl) return FALSE;
        notes_delete_selected(app);
        break;
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
        notes_delete_selected(app);
        break;
    case GDK_KEY_J:
        if (ctrl) return FALSE;
        notes_move_reorder(app, +1);
        break;
    case GDK_KEY_K:
        if (ctrl) return FALSE;
        notes_move_reorder(app, -1);
        break;
    default:
        return FALSE;
    }

    app->nav_prefix = 0; /* NAV_PREFIX_NONE */
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Context Menu                                                              */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    gint action;
} NoteMenuAction;

static gboolean notes_run_menu_action(gpointer data) {
    NoteMenuAction* a = (NoteMenuAction*)data;
    AppData* app = a->app;
    switch (a->action) {
    case NOTE_ACTION_OPEN:           notes_activate_selected(app); break;
    case NOTE_ACTION_NEW_NOTE:       notes_new_note(app); break;
    case NOTE_ACTION_NEW_NOTEBOOK:   notes_new_notebook(app); break;
    case NOTE_ACTION_RENAME:         notes_edit_selected(app); break;
    case NOTE_ACTION_MOVE:           notes_move_selected(app); break;
    case NOTE_ACTION_DELETE:         notes_delete_selected(app); break;
    case NOTE_ACTION_RESTORE:        notes_restore_selected(app); break;
    case NOTE_ACTION_EMPTY_TRASH:    notes_empty_trash(app); break;
    case NOTE_ACTION_MOVE_UP:        notes_move_reorder(app, -1); break;
    case NOTE_ACTION_MOVE_DOWN:      notes_move_reorder(app, +1); break;
    case NOTE_ACTION_SORT:           notes_toggle_sort(app); break;
    case NOTE_ACTION_COLLAPSE:       notes_toggle_collapse_expand(app); break;
    case NOTE_ACTION_CONVERT_TEXT:   notes_convert_selected_type(app, NOTE_TYPE_TEXT); break;
    case NOTE_ACTION_CONVERT_MARKDOWN: notes_convert_selected_type(app, NOTE_TYPE_MARKDOWN); break;
    case NOTE_ACTION_CONVERT_ORG:    notes_convert_selected_type(app, NOTE_TYPE_ORG); break;
    case NOTE_ACTION_EXPORT_MARKDOWN: notes_export_selected_markdown(app); break;
    default: break;
    }
    g_free(a);
    return G_SOURCE_REMOVE;
}

static void on_note_menu_item_clicked(GtkButton* btn, gpointer data) {
    GtkWidget* popover = GTK_WIDGET(data);
    NoteMenuAction* a = g_new0(NoteMenuAction, 1);
    a->app = (AppData*)g_object_get_data(G_OBJECT(btn), "app");
    a->action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "action"));
    gtk_popover_popdown(GTK_POPOVER(popover));
    g_idle_add(notes_run_menu_action, a);
}

static void note_menu_item(GtkWidget* box, GtkWidget* popover, AppData* app,
                           const gchar* text, const gchar* hint, gint action, gboolean sensitive) {
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    if (hint != NULL) {
        GtkWidget* key = gtk_label_new(hint);
        gtk_widget_add_css_class(key, "dim-label");
        gtk_box_append(GTK_BOX(row), key);
    }
    gtk_button_set_child(GTK_BUTTON(btn), row);
    gtk_widget_set_sensitive(btn, sensitive);
    g_object_set_data(G_OBJECT(btn), "app", app);
    g_object_set_data(G_OBJECT(btn), "action", GINT_TO_POINTER(action));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_note_menu_item_clicked), popover);
    gtk_box_append(GTK_BOX(box), btn);
}

static void note_menu_separator(GtkWidget* box) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 3);
    gtk_widget_set_margin_bottom(sep, 3);
    gtk_box_append(GTK_BOX(box), sep);
}

static gboolean notes_unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void notes_on_popover_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    g_idle_add(notes_unparent_idle, g_object_ref(popover));
}

static void notes_on_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    AppData* app = (AppData*)data;
    if (app == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) return;

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    ExplorerRow* row = explorer_tree_row_at(app->tree, x, y);
    if (row != NULL) {
        select_row(app, row);
    } else {
        explorer_tree_unselect_all(app->tree);
    }

    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    TrashNoteItem* tnote = NULL;
    NotesSelectionType sel = notes_selected_info(app, &nb, &note, &tnote);

    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(app->tree_view));
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    GdkRectangle rect = { (gint)x, (gint)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    if (sel == SEL_NOTE) {
        note_menu_item(box, popover, app, _("Open Note"), "Enter", NOTE_ACTION_OPEN, TRUE);
        note_menu_item(box, popover, app, _("Edit / Rename Note…"), "F2", NOTE_ACTION_RENAME, TRUE);
        note_menu_item(box, popover, app, _("Move to Notebook…"), "m", NOTE_ACTION_MOVE, TRUE);
        note_menu_separator(box);
        gboolean convertible = note != NULL && note->type != NOTE_TYPE_CANVAS;
        if (convertible && note->type != NOTE_TYPE_TEXT) {
            note_menu_item(box, popover, app, _("Convert to Plain Text (.txt)"), NULL, NOTE_ACTION_CONVERT_TEXT, TRUE);
        }
        if (convertible && note->type != NOTE_TYPE_MARKDOWN) {
            note_menu_item(box, popover, app, _("Convert to Markdown (.md)"), NULL, NOTE_ACTION_CONVERT_MARKDOWN, TRUE);
        }
        if (note != NULL && note->type == NOTE_TYPE_CANVAS) {
            note_menu_item(box, popover, app, _("Export to Markdown (.md)"), NULL, NOTE_ACTION_EXPORT_MARKDOWN, TRUE);
        }
        if (convertible && note->type != NOTE_TYPE_ORG) {
            note_menu_item(box, popover, app, _("Convert to Org-mode (.org)"), NULL, NOTE_ACTION_CONVERT_ORG, TRUE);
        }
        note_menu_separator(box);
        note_menu_item(box, popover, app, _("Move Up"), "Shift+K", NOTE_ACTION_MOVE_UP, TRUE);
        note_menu_item(box, popover, app, _("Move Down"), "Shift+J", NOTE_ACTION_MOVE_DOWN, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, _("Move to Trash"), "Delete", NOTE_ACTION_DELETE, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, _("New Note Here"), "a", NOTE_ACTION_NEW_NOTE, TRUE);
        note_menu_item(box, popover, app, _("New Notebook…"), "Shift+A", NOTE_ACTION_NEW_NOTEBOOK, TRUE);
    } else if (sel == SEL_NOTEBOOK) {
        note_menu_item(box, popover, app, _("New Note Here"), "a", NOTE_ACTION_NEW_NOTE, TRUE);
        note_menu_item(box, popover, app, _("Edit Notebook…"), "F2", NOTE_ACTION_RENAME, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, _("Move Up"), "Shift+K", NOTE_ACTION_MOVE_UP, TRUE);
        note_menu_item(box, popover, app, _("Move Down"), "Shift+J", NOTE_ACTION_MOVE_DOWN, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, _("Move Notebook to Trash"), "Delete", NOTE_ACTION_DELETE, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, _("New Notebook…"), "Shift+A", NOTE_ACTION_NEW_NOTEBOOK, TRUE);
    } else if (sel == SEL_TRASH_NOTE) {
        note_menu_item(box, popover, app, _("Restore Note"), "Shift+R", NOTE_ACTION_RESTORE, TRUE);
        note_menu_item(box, popover, app, _("Delete Permanently"), "Delete", NOTE_ACTION_DELETE, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, _("Empty Trash…"), NULL, NOTE_ACTION_EMPTY_TRASH, TRUE);
    } else if (sel == SEL_TRASH) {
        note_menu_item(box, popover, app, _("Empty Trash…"), NULL, NOTE_ACTION_EMPTY_TRASH, TRUE);
    } else {
        note_menu_item(box, popover, app, _("New Note"), "a", NOTE_ACTION_NEW_NOTE, TRUE);
        note_menu_item(box, popover, app, _("New Notebook…"), "Shift+A", NOTE_ACTION_NEW_NOTEBOOK, TRUE);
    }

    note_menu_separator(box);
    note_menu_item(box, popover, app, _("Sort / Order Mode"), "o", NOTE_ACTION_SORT, TRUE);
    note_menu_item(box, popover, app, _("Collapse / Expand All"), NULL, NOTE_ACTION_COLLAPSE, TRUE);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(notes_on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void notes_attach_context_menu(AppData* app) {
    if (app == NULL || app->tree_view == NULL) return;
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(notes_on_right_click), app);
    gtk_widget_add_controller(GTK_WIDGET(app->tree_view), GTK_EVENT_CONTROLLER(click));
}
