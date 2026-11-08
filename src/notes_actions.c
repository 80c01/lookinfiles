/* Notes view actions: create, edit, convert, export, move, reorder, sort,
 * trash and restore notes and notebooks (see notes.h) */
#include "notes.h"
#include "notes_private.h"
#include "preview_state.h"
#include "core/i18n.h"
#include "note_types.h"
#include "notes_store.h"
#include "notebook_icons.h"
#include "canvas/canvas_doc.h"
#include "canvas/canvas_view.h"
#include "tree_nav.h"
#include "core/file_types.h"
#include "editor.h"
#include "viewer.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* User Actions                                                              */
/* ------------------------------------------------------------------------- */

/* Initial content of a new note */
static gchar* note_starter(NoteType type, const gchar* title) {
    gchar* starter = NULL;
    if (type == NOTE_TYPE_MARKDOWN) {
        starter = g_strdup_printf(
            "# %s\n\n"
            "Markdown note supporting **bold**, *italic*, lists, tables, and images.\n\n"
            "- [ ] Task 1\n"
            "- [ ] Task 2\n",
            title);
    } else if (type == NOTE_TYPE_ORG) {
        GDateTime* dt = g_date_time_new_now_local();
        gchar* cur_date = g_date_time_format(dt, "%Y-%m-%d");
        g_date_time_unref(dt);

        starter = g_strdup_printf(
            "#+TITLE: %s\n"
            "#+DATE: <%s>\n\n"
            "* TODO Welcome to Org-mode!\n"
            "  SCHEDULED: <%s>\n"
            "  - [ ] First task\n"
            "  - [ ] Second task\n\n"
            "* Notes\n"
            "  Org-mode supports *bold*, /italic/, =code=, tables, and folding.\n",
            title, cur_date ? cur_date : "2026-10-28", cur_date ? cur_date : "2026-10-28");
        g_free(cur_date);
    } else {
        starter = g_strdup("");
    }

    return starter;
}

/* Write a new note: a file, or the directory of a Canvas */
static gboolean note_create_on_disk(NoteType type, const gchar* path, const gchar* title) {
    if (type == NOTE_TYPE_CANVAS) {
        return canvas_doc_create(path, title, NULL);
    }
    gchar* starter = note_starter(type, title);
    gboolean ok = g_file_set_contents(path, starter, -1, NULL);
    g_free(starter);
    return ok;
}

/* Delete a note for good (from the trash) */
static void note_delete_on_disk(AppData* app, NoteType type, const gchar* path) {
    if (type == NOTE_TYPE_CANVAS) {
        canvas_view_close_path(app, path);
        canvas_doc_delete(path);
    } else {
        g_unlink(path);
    }
}

/* Edit a note: an editor tab, or the Canvas page */
static void open_note(AppData* app, NoteType type, const gchar* name, const gchar* title, const gchar* path) {
    if (type == NOTE_TYPE_CANVAS) {
        canvas_view_open(app, path, title);
    } else {
        editor_open_file(app, name, path);
    }
}

void notes_new_note(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    NotebookItem* target_nb = NULL;
    NotesSelectionType sel = notes_selected_info(app, &target_nb, NULL, NULL);
    if (sel == SEL_TRASH || sel == SEL_TRASH_NOTE || target_nb == NULL) {
        if (mgr->notebooks != NULL) {
            target_nb = (NotebookItem*)mgr->notebooks->data;
        }
    }
    if (target_nb == NULL) return;

    gchar* default_title = g_strdup_printf(_("Untitled-%d"), mgr->next_note_id++);
    gchar* title = NULL;
    NoteType type = NOTE_TYPE_MARKDOWN;
    if (!notes_prompt_note(app->window, _("New Note"), _("_Create"), default_title, NOTE_TYPE_MARKDOWN, FALSE,
                            NULL, NULL, FALSE, &title, &type)) {
        g_free(default_title);
        return;
    }
    g_free(default_title);
    if (title == NULL) return;

    const gchar* ext = note_type_ext(type);
    gchar* filename = g_strdup_printf("%s%s", title, ext);
    gchar* file_path = g_build_filename(target_nb->path, filename, NULL);

    /* If file exists, find an available unique filename */
    if (g_file_test(file_path, G_FILE_TEST_EXISTS)) {
        gint counter = 2;
        do {
            g_free(filename);
            g_free(file_path);
            filename = g_strdup_printf("%s (%d)%s", title, counter++, ext);
            file_path = g_build_filename(target_nb->path, filename, NULL);
        } while (g_file_test(file_path, G_FILE_TEST_EXISTS));
    }

    note_create_on_disk(type, file_path, title);

    NoteItem* note = note_item_new(filename, title, file_path, type);
    target_nb->notes = g_list_append(target_nb->notes, note);
    target_nb->expanded = TRUE;

    notes_save(app);

    /* Open the new note to edit it */
    open_note(app, type, title, title, file_path);

    notes_refresh(app);

    notes_select_path(app, file_path);

    g_free(title);
    g_free(filename);
    g_free(file_path);
}

/* Notebook named `name`, created when missing */
static NotebookItem* notes_ensure_notebook(NotesManager* mgr, const gchar* name) {
    for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
        NotebookItem* nb = (NotebookItem*)l->data;
        if (g_strcmp0(nb->name, name) == 0) return nb;
    }
    gchar* root = notes_get_root_dir();
    gchar* dir_path = g_build_filename(root, name, NULL);
    g_free(root);
    g_mkdir_with_parents(dir_path, 0755);
    NotebookItem* nb = notebook_item_new(name, NOTEBOOK_ICON_DEFAULT, dir_path);
    mgr->notebooks = g_list_append(mgr->notebooks, nb);
    g_free(dir_path);
    return nb;
}

/* A notebook or note name: not empty, no '/', not hidden */
static gboolean notes_valid_name(const gchar* name) {
    return name != NULL && name[0] != '\0' && name[0] != '.' && strchr(name, '/') == NULL;
}

gchar* notes_find_or_create(AppData* app, const gchar* spec) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL || spec == NULL) return NULL;

    gchar* nb_name = NULL;
    const gchar* name = spec;
    const gchar* slash = strchr(spec, '/');
    if (slash != NULL) {
        nb_name = g_strndup(spec, slash - spec);
        name = slash + 1;
    }
    if ((nb_name != NULL && !notes_valid_name(nb_name)) || !notes_valid_name(name)) {
        g_free(nb_name);
        return NULL;
    }

    /* A note extension (.org, .md, .txt...) chooses the file; otherwise the title does */
    const gchar* ext = file_extension(name);
    gboolean by_filename = note_type_has_note_ext(name);

    for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
        NotebookItem* nb = (NotebookItem*)l->data;
        if (nb_name != NULL && g_strcmp0(nb->name, nb_name) != 0) continue;
        for (GList* n = nb->notes; n != NULL; n = n->next) {
            NoteItem* note = (NoteItem*)n->data;
            if (g_strcmp0(by_filename ? note->filename : note->title, name) == 0) {
                nb->expanded = TRUE;
                g_free(nb_name);
                return g_strdup(note->path);
            }
        }
    }

    /* Not found: create it, in the named notebook or the first one */
    NotebookItem* nb = (nb_name != NULL) ? notes_ensure_notebook(mgr, nb_name)
                     : (mgr->notebooks != NULL) ? (NotebookItem*)mgr->notebooks->data
                     : notes_ensure_notebook(mgr, "General");
    g_free(nb_name);

    gchar* title = NULL;
    gchar* filename = NULL;
    NoteType type;
    if (by_filename) {
        title = g_strndup(name, strlen(name) - strlen(ext) - 1);
        filename = g_strdup(name);
        type = note_type_from_filename(name);
    } else {
        title = g_strdup(name);
        type = NOTE_TYPE_MARKDOWN;
        filename = g_strdup_printf("%s%s", title, note_type_ext(type));
    }
    gchar* file_path = g_build_filename(nb->path, filename, NULL);
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) {
        if (!note_create_on_disk(type, file_path, title)) {
            g_free(title);
            g_free(filename);
            g_free(file_path);
            return NULL;
        }
    }
    nb->notes = g_list_append(nb->notes, note_item_new(filename, title, file_path, type));
    nb->expanded = TRUE;
    notes_save(app);
    g_free(title);
    g_free(filename);
    return file_path;
}

void notes_new_notebook(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    gchar* name = NULL;
    gchar* icon = NULL;
    if (!notes_prompt_notebook(app->window, _("New Notebook"), _("_Create"), _("Notebook"), NOTEBOOK_ICON_DEFAULT, &name, &icon)) {
        return;
    }

    gchar* root = notes_get_root_dir();
    gchar* dir_path = g_build_filename(root, name, NULL);
    g_free(root);

    if (g_file_test(dir_path, G_FILE_TEST_EXISTS)) {
        notes_notice(app, _("Notebook Exists"), _("A notebook with that name already exists. Please choose a different name."));
        g_free(name);
        g_free(icon);
        g_free(dir_path);
        return;
    }

    g_mkdir_with_parents(dir_path, 0755);

    NotebookItem* nb = notebook_item_new(name, icon, dir_path);
    mgr->notebooks = g_list_append(mgr->notebooks, nb);

    notes_save(app);
    notes_refresh(app);

    notes_select_path(app, dir_path);

    g_free(name);
    g_free(icon);
    g_free(dir_path);
}

void notes_convert_selected_type(AppData* app, NoteType target_type) {
    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    NotesSelectionType sel = notes_selected_info(app, &nb, &note, NULL);
    if (sel != SEL_NOTE || note == NULL || nb == NULL) return;
    if (note->type == target_type) return;
    /* A Canvas has its own contents: exported to Markdown, never renamed to it */
    if (note->type == NOTE_TYPE_CANVAS || target_type == NOTE_TYPE_CANVAS) return;

    const gchar* ext = note_type_ext(target_type);
    gchar* new_filename = g_strdup_printf("%s%s", note->title, ext);
    gchar* new_path = g_build_filename(nb->path, new_filename, NULL);

    if (g_file_test(new_path, G_FILE_TEST_EXISTS) && g_strcmp0(new_path, note->path) != 0) {
        notes_notice(app, _("Note Exists"), _("A note with this name and format already exists in this notebook."));
        g_free(new_filename);
        g_free(new_path);
        return;
    }

    if (g_rename(note->path, new_path) == 0) {
        notes_update_open_editors(app, note->path, new_path, note->title);
        g_free(note->filename);
        g_free(note->path);
        note->filename = new_filename;
        note->path = new_path;
        note->type = target_type;

        notes_save(app);
        notes_refresh(app);

        if (preview_state_path(app) != NULL) {
            load_file_content(app, note->filename, note->path);
        }
    } else {
        g_free(new_filename);
        g_free(new_path);
    }
}

void notes_export_selected_markdown(AppData* app) {
    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    if (notes_selected_info(app, &nb, &note, NULL) != SEL_NOTE || note == NULL || note->type != NOTE_TYPE_CANVAS) {
        return;
    }
    canvas_view_save(app); /* the last changes too */
    g_autoptr(GError) error = NULL;
    gchar* path = canvas_doc_export_markdown(note->path, &error);
    if (path == NULL) {
        notes_notice(app, _("Export Failed"), error != NULL ? error->message : _("The canvas cannot be read."));
        return;
    }
    gchar* filename = g_path_get_basename(path);
    gchar* title = g_strndup(filename, strlen(filename) - strlen(".md"));
    NoteItem* md = note_item_new(filename, title, path, NOTE_TYPE_MARKDOWN);
    nb->notes = g_list_insert(nb->notes, md, g_list_index(nb->notes, note) + 1);
    notes_save(app);
    notes_refresh(app);
    notes_select_path(app, path);
    g_free(title);
    g_free(filename);
    g_free(path);
}

/* Edit dialog of a note: title, format and notebook (a new notebook moves it) */
static void notes_edit_note(AppData* app, NotebookItem* nb, NoteItem* note, gboolean focus_notebook) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    gchar* new_title = NULL;
    NoteType new_type = note->type;
    NotebookItem* dest_nb = nb;
    if (!notes_prompt_note(app->window, _("Edit Note"), _("_Save"), note->title, note->type, TRUE,
                            mgr->notebooks, &dest_nb, focus_notebook, &new_title, &new_type)) {
        return;
    }

    gboolean title_changed = (g_strcmp0(new_title, note->title) != 0);
    gboolean type_changed = (new_type != note->type);
    gboolean nb_changed = (dest_nb != nb);

    if (!title_changed && !type_changed && !nb_changed) {
        g_free(new_title);
        return;
    }

    const gchar* ext = note_type_ext(new_type);
    gchar* new_filename = g_strdup_printf("%s%s", new_title, ext);
    gchar* new_path = g_build_filename(dest_nb->path, new_filename, NULL);

    if (g_file_test(new_path, G_FILE_TEST_EXISTS) && g_strcmp0(new_path, note->path) != 0) {
        notes_notice(app, _("Note Exists"), nb_changed
                     ? _("A note with that title and format already exists in the target notebook.")
                     : _("A note with that title and format already exists in this notebook."));
        g_free(new_title);
        g_free(new_filename);
        g_free(new_path);
        return;
    }

    if (g_rename(note->path, new_path) != 0) {
        g_free(new_title);
        g_free(new_filename);
        g_free(new_path);
        return;
    }

    notes_update_open_editors(app, note->path, new_path, new_title);
    g_free(note->filename);
    g_free(note->title);
    g_free(note->path);
    note->filename = new_filename;
    note->title = new_title;
    note->path = new_path;
    note->type = new_type;

    if (nb_changed) {
        nb->notes = g_list_remove(nb->notes, note);
        dest_nb->notes = g_list_append(dest_nb->notes, note);
        dest_nb->expanded = TRUE;
    }

    notes_save(app);
    notes_refresh(app);
    if (nb_changed) notes_select_path(app, note->path);

    if (preview_state_path(app) != NULL) {
        load_file_content(app, note->filename, note->path);
    }
}

void notes_edit_selected(AppData* app) {
    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    TrashNoteItem* tnote = NULL;
    NotesSelectionType sel = notes_selected_info(app, &nb, &note, &tnote);

    if (sel == SEL_NOTE && note != NULL && nb != NULL) {
        notes_edit_note(app, nb, note, FALSE);
    } else if (sel == SEL_NOTEBOOK && nb != NULL) {
        gchar* new_name = NULL;
        gchar* new_icon = NULL;
        if (!notes_prompt_notebook(app->window, _("Edit Notebook"), _("_Save"), nb->name, nb->icon, &new_name, &new_icon)) {
            return;
        }

        gboolean name_changed = (g_strcmp0(new_name, nb->name) != 0);
        gboolean icon_changed = (g_strcmp0(new_icon, nb->icon) != 0);

        if (!name_changed && !icon_changed) {
            g_free(new_name);
            g_free(new_icon);
            return;
        }

        if (name_changed) {
            gchar* root = notes_get_root_dir();
            gchar* new_dir = g_build_filename(root, new_name, NULL);
            g_free(root);

            if (g_file_test(new_dir, G_FILE_TEST_EXISTS)) {
                notes_notice(app, _("Notebook Exists"), _("A notebook with that name already exists."));
                g_free(new_name);
                g_free(new_icon);
                g_free(new_dir);
                return;
            }

            if (g_rename(nb->path, new_dir) != 0) {
                g_free(new_name);
                g_free(new_icon);
                g_free(new_dir);
                return;
            }

            notes_update_open_editors(app, nb->path, new_dir, NULL);
            g_free(nb->name);
            g_free(nb->path);
            nb->name = new_name;
            nb->path = new_dir;

            /* Update paths of all notes inside */
            for (GList* nl = nb->notes; nl != NULL; nl = nl->next) {
                NoteItem* n = (NoteItem*)nl->data;
                g_free(n->path);
                n->path = g_build_filename(nb->path, n->filename, NULL);
            }
        } else {
            g_free(new_name);
        }

        if (icon_changed) {
            g_free(nb->icon);
            nb->icon = new_icon;
        } else {
            g_free(new_icon);
        }

        notes_save(app);
        notes_refresh(app);
    }
}

void notes_move_selected(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    NotebookItem* src_nb = NULL;
    NoteItem* note = NULL;
    NotesSelectionType sel = notes_selected_info(app, &src_nb, &note, NULL);

    if (sel != SEL_NOTE || note == NULL || src_nb == NULL) return;
    if (g_list_length(mgr->notebooks) < 2) {
        notes_notice(app, _("Move Note"), _("There are no other notebooks to move this note to. Create another notebook first."));
        return;
    }

    /* Moving is part of the edit dialog: open it on the notebook choice */
    notes_edit_note(app, src_nb, note, TRUE);
}

void notes_delete_selected(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    TrashNoteItem* tnote = NULL;
    NotesSelectionType sel = notes_selected_info(app, &nb, &note, &tnote);

    if (sel == SEL_NOTE && note != NULL && nb != NULL) {
        /* Move note to Trash */
        gchar* trash_dir = notes_get_trash_dir();
        gchar* trash_filename = g_strdup_printf("%" G_GINT64_FORMAT "_%s", (gint64)time(NULL), note->filename);
        gchar* trash_path = g_build_filename(trash_dir, trash_filename, NULL);
        g_free(trash_dir);

        canvas_view_close_path(app, note->path);
        if (g_rename(note->path, trash_path) == 0) {
            TrashNoteItem* item = trash_item_new(trash_filename, note->title, trash_path, nb->name, (gint64)time(NULL), note->type);
            mgr->trash = g_list_append(mgr->trash, item);

            nb->notes = g_list_remove(nb->notes, note);
            note_item_free(note);

            notes_save(app);
            notes_refresh(app);
        }
        g_free(trash_filename);
        g_free(trash_path);
    } else if (sel == SEL_NOTEBOOK && nb != NULL) {
        guint count = g_list_length(nb->notes);
        gchar* msg = g_strdup_printf(ngettext("Move notebook '%s' and its %u note to Trash?",
                                              "Move notebook '%s' and its %u notes to Trash?", count),
                                     nb->name, count);
        if (!notes_confirm(app, _("Delete Notebook"), msg, _("_Move to Trash"), TRUE)) {
            g_free(msg);
            return;
        }
        g_free(msg);

        canvas_view_close_path(app, nb->path);
        gchar* trash_dir = notes_get_trash_dir();
        for (GList* nl = nb->notes; nl != NULL; nl = nl->next) {
            NoteItem* n = (NoteItem*)nl->data;
            gchar* t_filename = g_strdup_printf("%" G_GINT64_FORMAT "_%s", (gint64)time(NULL), n->filename);
            gchar* t_path = g_build_filename(trash_dir, t_filename, NULL);
            if (g_rename(n->path, t_path) == 0) {
                TrashNoteItem* titem = trash_item_new(t_filename, n->title, t_path, nb->name, (gint64)time(NULL), n->type);
                mgr->trash = g_list_append(mgr->trash, titem);
            }
            g_free(t_filename);
            g_free(t_path);
        }
        g_free(trash_dir);

        g_rmdir(nb->path);
        mgr->notebooks = g_list_remove(mgr->notebooks, nb);
        notebook_item_free(nb);

        notes_save(app);
        notes_refresh(app);
    } else if (sel == SEL_TRASH_NOTE && tnote != NULL) {
        /* Permanently delete from trash */
        gchar* msg = g_strdup_printf(_("Permanently delete '%s'? This action cannot be undone."), tnote->title);
        if (notes_confirm(app, _("Delete Permanently"), msg, _("_Delete"), TRUE)) {
            note_delete_on_disk(app, tnote->type, tnote->path);
            mgr->trash = g_list_remove(mgr->trash, tnote);
            trash_item_free(tnote);

            notes_save(app);
            notes_refresh(app);
        }
        g_free(msg);
    } else if (sel == SEL_TRASH) {
        notes_empty_trash(app);
    }
}

void notes_restore_selected(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    TrashNoteItem* tnote = NULL;
    NotesSelectionType sel = notes_selected_info(app, NULL, NULL, &tnote);
    if (sel != SEL_TRASH_NOTE || tnote == NULL) return;

    /* Find target notebook to restore into */
    NotebookItem* dest_nb = NULL;
    if (tnote->original_nb != NULL) {
        for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
            NotebookItem* nb = (NotebookItem*)l->data;
            if (g_strcmp0(nb->name, tnote->original_nb) == 0) {
                dest_nb = nb;
                break;
            }
        }
    }
    if (dest_nb == NULL && mgr->notebooks != NULL) {
        dest_nb = (NotebookItem*)mgr->notebooks->data;
    }
    if (dest_nb == NULL) {
        /* Recreate original notebook if none exist */
        gchar* root = notes_get_root_dir();
        const gchar* nb_name = tnote->original_nb ? tnote->original_nb : "General";
        gchar* nb_path = g_build_filename(root, nb_name, NULL);
        g_free(root);
        g_mkdir_with_parents(nb_path, 0755);
        dest_nb = notebook_item_new(nb_name, NOTEBOOK_ICON_DEFAULT, nb_path);
        mgr->notebooks = g_list_append(mgr->notebooks, dest_nb);
        g_free(nb_path);
    }

    const gchar* ext = note_type_ext(tnote->type);
    gchar* note_filename = g_strdup_printf("%s%s", tnote->title, ext);
    gchar* target_path = g_build_filename(dest_nb->path, note_filename, NULL);

    if (g_file_test(target_path, G_FILE_TEST_EXISTS)) {
        gint counter = 2;
        do {
            g_free(note_filename);
            g_free(target_path);
            note_filename = g_strdup_printf("%s (%d)%s", tnote->title, counter++, ext);
            target_path = g_build_filename(dest_nb->path, note_filename, NULL);
        } while (g_file_test(target_path, G_FILE_TEST_EXISTS));
    }

    if (g_rename(tnote->path, target_path) == 0) {
        NoteItem* item = note_item_new(note_filename, tnote->title, target_path, tnote->type);
        dest_nb->notes = g_list_append(dest_nb->notes, item);
        dest_nb->expanded = TRUE;

        mgr->trash = g_list_remove(mgr->trash, tnote);
        trash_item_free(tnote);

        notes_save(app);
        notes_refresh(app);

        notes_select_path(app, target_path);
    }

    g_free(note_filename);
    g_free(target_path);
}

void notes_empty_trash(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    guint count = g_list_length(mgr->trash);
    if (count == 0) {
        notes_notice(app, _("Empty Trash"), _("The Trash bin is already empty."));
        return;
    }

    gchar* msg = g_strdup_printf(ngettext("Permanently delete the %u item in the Trash? This cannot be undone.",
                                          "Permanently delete all %u items in the Trash? This cannot be undone.",
                                          count),
                                 count);
    if (!notes_confirm(app, _("Empty Trash"), msg, _("_Empty Trash"), TRUE)) {
        g_free(msg);
        return;
    }
    g_free(msg);

    for (GList* l = mgr->trash; l != NULL; l = l->next) {
        TrashNoteItem* t = (TrashNoteItem*)l->data;
        note_delete_on_disk(app, t->type, t->path);
    }
    g_list_free_full(mgr->trash, (GDestroyNotify)trash_item_free);
    mgr->trash = NULL;

    notes_save(app);
    notes_refresh(app);
}

void notes_move_reorder(AppData* app, gint delta) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    NotesSelectionType sel = notes_selected_info(app, &nb, &note, NULL);

    if (sel == SEL_NOTE && note != NULL && nb != NULL) {
        GList* link = g_list_find(nb->notes, note);
        if (link == NULL) return;

        if (delta < 0 && link->prev != NULL) {
            gpointer tmp = link->data;
            link->data = link->prev->data;
            link->prev->data = tmp;
        } else if (delta > 0 && link->next != NULL) {
            gpointer tmp = link->data;
            link->data = link->next->data;
            link->next->data = tmp;
        } else {
            return;
        }
        mgr->sort_mode = 0;
        notes_save(app);
        notes_refresh(app);
    } else if (sel == SEL_NOTEBOOK && nb != NULL) {
        GList* link = g_list_find(mgr->notebooks, nb);
        if (link == NULL) return;

        if (delta < 0 && link->prev != NULL) {
            gpointer tmp = link->data;
            link->data = link->prev->data;
            link->prev->data = tmp;
        } else if (delta > 0 && link->next != NULL) {
            gpointer tmp = link->data;
            link->data = link->next->data;
            link->next->data = tmp;
        } else {
            return;
        }
        mgr->sort_mode = 0;
        notes_save(app);
        notes_refresh(app);
    }
}

static gint compare_notes_asc(gconstpointer a, gconstpointer b) {
    return g_utf8_collate(((const NoteItem*)a)->title, ((const NoteItem*)b)->title);
}

static gint compare_notes_desc(gconstpointer a, gconstpointer b) {
    return -compare_notes_asc(a, b);
}

static gint compare_notebooks_asc(gconstpointer a, gconstpointer b) {
    return g_utf8_collate(((const NotebookItem*)a)->name, ((const NotebookItem*)b)->name);
}

static gint compare_notebooks_desc(gconstpointer a, gconstpointer b) {
    return -compare_notebooks_asc(a, b);
}

void notes_toggle_sort(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    /* Cycle: 0 (Manual) -> 1 (A-Z) -> 2 (Z-A) -> 0 (Manual) */
    mgr->sort_mode = (mgr->sort_mode + 1) % 3;

    if (mgr->sort_mode == 1) {
        mgr->notebooks = g_list_sort(mgr->notebooks, compare_notebooks_asc);
        for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
            NotebookItem* nb = (NotebookItem*)l->data;
            nb->notes = g_list_sort(nb->notes, compare_notes_asc);
        }
    } else if (mgr->sort_mode == 2) {
        mgr->notebooks = g_list_sort(mgr->notebooks, compare_notebooks_desc);
        for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
            NotebookItem* nb = (NotebookItem*)l->data;
            nb->notes = g_list_sort(nb->notes, compare_notes_desc);
        }
    }

    notes_save(app);
    notes_refresh(app);
}

gboolean notes_any_expanded(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    for (GList* l = mgr != NULL ? mgr->notebooks : NULL; l != NULL; l = l->next) {
        if (((NotebookItem*)l->data)->expanded) {
            return TRUE;
        }
    }
    return FALSE;
}

void notes_toggle_collapse_expand(AppData* app) {
    NotesManager* mgr = notes_mgr_of(app);
    if (mgr == NULL) return;

    gboolean target = !notes_any_expanded(app);
    for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
        NotebookItem* nb = (NotebookItem*)l->data;
        nb->expanded = target;
    }
    mgr->trash_expanded = target;

    notes_save(app);
    notes_refresh(app);
}

void notes_activate_selected(AppData* app) {
    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    TrashNoteItem* tnote = NULL;
    NotesSelectionType sel = notes_selected_info(app, &nb, &note, &tnote);

    if (sel == SEL_NOTE && note != NULL) {
        open_note(app, note->type, note->filename ? note->filename : note->title, note->title, note->path);
    } else if (sel == SEL_TRASH_NOTE && tnote != NULL) {
        gchar* msg = g_strdup_printf(_("Note '%s' is in the Trash bin.\nDo you want to restore it so you can edit it?"), tnote->title);
        if (notes_confirm(app, _("Restore Note"), msg, _("_Restore"), FALSE)) {
            notes_restore_selected(app);
        }
        g_free(msg);
    } else if (sel == SEL_NOTEBOOK || sel == SEL_TRASH) {
        /* Toggle expand / collapse */
        explorer_tree_toggle_expanded(app->tree, tree_nav_selected(app));
    }
}
