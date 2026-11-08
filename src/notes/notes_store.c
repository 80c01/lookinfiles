/* The notes on disk and their manifest (see notes_store.h) */
#include "notes/notes_store.h"
#include "notes/notebook_icons.h"
#include <string.h>

NoteItem* note_item_new(const gchar* filename, const gchar* title, const gchar* path, NoteType type) {
    NoteItem* item = g_new0(NoteItem, 1);
    item->filename = g_strdup(filename);
    item->title = g_strdup(title);
    item->path = g_strdup(path);
    item->type = type;
    return item;
}

void note_item_free(NoteItem* item) {
    if (item == NULL) return;
    g_free(item->filename);
    g_free(item->title);
    g_free(item->path);
    g_free(item);
}

NotebookItem* notebook_item_new(const gchar* name, const gchar* icon, const gchar* path) {
    NotebookItem* nb = g_new0(NotebookItem, 1);
    nb->name = g_strdup(name);
    nb->icon = g_strdup(notebook_icon_normalize(icon));
    nb->path = g_strdup(path);
    nb->expanded = TRUE;
    nb->notes = NULL;
    return nb;
}

void notebook_item_free(NotebookItem* nb) {
    if (nb == NULL) return;
    g_free(nb->name);
    g_free(nb->icon);
    g_free(nb->path);
    g_list_free_full(nb->notes, (GDestroyNotify)note_item_free);
    g_free(nb);
}

TrashNoteItem* trash_item_new(const gchar* filename, const gchar* title, const gchar* path,
                              const gchar* original_nb, gint64 trashed_at, NoteType type) {
    TrashNoteItem* item = g_new0(TrashNoteItem, 1);
    item->filename = g_strdup(filename);
    item->title = g_strdup(title);
    item->path = g_strdup(path);
    item->original_nb = g_strdup(original_nb);
    item->trashed_at = trashed_at;
    item->type = type;
    return item;
}

void trash_item_free(TrashNoteItem* item) {
    if (item == NULL) return;
    g_free(item->filename);
    g_free(item->title);
    g_free(item->path);
    g_free(item->original_nb);
    g_free(item);
}

gboolean notes_store_save(const NotesManager* mgr, GError** error) {
    g_return_val_if_fail(mgr != NULL, FALSE);

    GString* out = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    g_string_append_printf(out, "<notes version=\"1\" sort=\"%d\" next_id=\"%d\" trash_expanded=\"%s\">\n",
                           mgr->sort_mode, mgr->next_note_id, mgr->trash_expanded ? "true" : "false");

    for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
        NotebookItem* nb = (NotebookItem*)l->data;
        gchar* nb_line = g_markup_printf_escaped("  <notebook name=\"%s\" icon=\"%s\" expanded=\"%s\">\n",
                                                nb->name, nb->icon, nb->expanded ? "true" : "false");
        g_string_append(out, nb_line);
        g_free(nb_line);

        for (GList* nl = nb->notes; nl != NULL; nl = nl->next) {
            NoteItem* note = (NoteItem*)nl->data;
            gchar* n_line = g_markup_printf_escaped("    <note filename=\"%s\" title=\"%s\" type=\"%s\"/>\n",
                                                   note->filename, note->title, note_type_to_str(note->type));
            g_string_append(out, n_line);
            g_free(n_line);
        }
        g_string_append(out, "  </notebook>\n");
    }

    g_string_append(out, "  <trash>\n");
    for (GList* tl = mgr->trash; tl != NULL; tl = tl->next) {
        TrashNoteItem* t = (TrashNoteItem*)tl->data;
        gchar* t_line = g_markup_printf_escaped("    <trashed_note filename=\"%s\" title=\"%s\" original_nb=\"%s\" trashed_at=\"%" G_GINT64_FORMAT "\" type=\"%s\"/>\n",
                                               t->filename, t->title, t->original_nb ? t->original_nb : "General", t->trashed_at, note_type_to_str(t->type));
        g_string_append(out, t_line);
        g_free(t_line);
    }
    g_string_append(out, "  </trash>\n");
    g_string_append(out, "</notes>\n");

    gchar* path = g_build_filename(mgr->root, NOTES_STORE_MANIFEST, NULL);
    gboolean ok = g_file_set_contents(path, out->str, (gssize)out->len, error);
    g_free(path);
    g_string_free(out, TRUE);
    return ok;
}

typedef struct {
    NotesManager* mgr;
    NotebookItem* current_nb;
    gboolean in_trash;
} NotesParseState;

static void notes_parse_start(GMarkupParseContext* ctx, const gchar* element,
                              const gchar** attr_names, const gchar** attr_values,
                              gpointer user_data, GError** error) {
    (void)ctx;
    (void)error;
    NotesParseState* st = (NotesParseState*)user_data;
    const gchar* name = NULL;
    const gchar* icon = NULL;
    const gchar* filename = NULL;
    const gchar* title = NULL;
    const gchar* original_nb = NULL;
    const gchar* expanded_str = NULL;
    const gchar* sort_str = NULL;
    const gchar* next_id_str = NULL;
    const gchar* trashed_at_str = NULL;
    const gchar* type_str = NULL;

    for (gint i = 0; attr_names[i] != NULL; i++) {
        if (g_strcmp0(attr_names[i], "name") == 0) name = attr_values[i];
        else if (g_strcmp0(attr_names[i], "icon") == 0) icon = attr_values[i];
        else if (g_strcmp0(attr_names[i], "filename") == 0) filename = attr_values[i];
        else if (g_strcmp0(attr_names[i], "title") == 0) title = attr_values[i];
        else if (g_strcmp0(attr_names[i], "original_nb") == 0) original_nb = attr_values[i];
        else if (g_strcmp0(attr_names[i], "expanded") == 0) expanded_str = attr_values[i];
        else if (g_strcmp0(attr_names[i], "sort") == 0) sort_str = attr_values[i];
        else if (g_strcmp0(attr_names[i], "next_id") == 0) next_id_str = attr_values[i];
        else if (g_strcmp0(attr_names[i], "trashed_at") == 0) trashed_at_str = attr_values[i];
        else if (g_strcmp0(attr_names[i], "type") == 0) type_str = attr_values[i];
        else if (g_strcmp0(attr_names[i], "trash_expanded") == 0) {
            st->mgr->trash_expanded = (g_strcmp0(attr_values[i], "true") == 0);
        }
    }

    if (g_strcmp0(element, "notes") == 0) {
        if (sort_str != NULL) st->mgr->sort_mode = (gint)g_ascii_strtoll(sort_str, NULL, 10);
        if (next_id_str != NULL) st->mgr->next_note_id = (gint)g_ascii_strtoll(next_id_str, NULL, 10);
    } else if (g_strcmp0(element, "notebook") == 0 && name != NULL && name[0] != '\0') {
        gchar* nb_dir = g_build_filename(st->mgr->root, name, NULL);
        g_mkdir_with_parents(nb_dir, 0755);

        NotebookItem* nb = notebook_item_new(name, icon, nb_dir);
        if (expanded_str != NULL) nb->expanded = (g_strcmp0(expanded_str, "true") == 0);
        st->mgr->notebooks = g_list_append(st->mgr->notebooks, nb);
        st->current_nb = nb;
        g_free(nb_dir);
    } else if (g_strcmp0(element, "note") == 0 && filename != NULL && st->current_nb != NULL) {
        gchar* note_path = g_build_filename(st->current_nb->path, filename, NULL);
        if (g_file_test(note_path, G_FILE_TEST_EXISTS)) {
            const gchar* t = (title != NULL && title[0] != '\0') ? title : filename;
            NoteType ntype = note_type_from_str(type_str, filename);
            NoteItem* note = note_item_new(filename, t, note_path, ntype);
            st->current_nb->notes = g_list_append(st->current_nb->notes, note);
        }
        g_free(note_path);
    } else if (g_strcmp0(element, "trash") == 0) {
        st->in_trash = TRUE;
    } else if (g_strcmp0(element, "trashed_note") == 0 && filename != NULL) {
        gchar* trash_dir = notes_store_trash_dir(st->mgr);
        gchar* t_path = g_build_filename(trash_dir, filename, NULL);
        g_free(trash_dir);
        if (g_file_test(t_path, G_FILE_TEST_EXISTS)) {
            gint64 tat = trashed_at_str ? g_ascii_strtoll(trashed_at_str, NULL, 10) : g_get_real_time() / G_USEC_PER_SEC;
            const gchar* t = (title != NULL && title[0] != '\0') ? title : filename;
            NoteType ntype = note_type_from_str(type_str, filename);
            TrashNoteItem* item = trash_item_new(filename, t, t_path, original_nb ? original_nb : "General", tat, ntype);
            st->mgr->trash = g_list_append(st->mgr->trash, item);
        }
        g_free(t_path);
    }
}

static void notes_parse_end(GMarkupParseContext* ctx, const gchar* element,
                            gpointer user_data, GError** error) {
    (void)ctx;
    (void)error;
    NotesParseState* st = (NotesParseState*)user_data;
    if (g_strcmp0(element, "notebook") == 0) {
        st->current_nb = NULL;
    } else if (g_strcmp0(element, "trash") == 0) {
        st->in_trash = FALSE;
    }
}

static void reconcile_with_disk(NotesManager* mgr) {
    const gchar* root = mgr->root;
    GDir* d = g_dir_open(root, 0, NULL);
    if (d != NULL) {
        const gchar* name = NULL;
        while ((name = g_dir_read_name(d)) != NULL) {
            if (name[0] == '.' || g_strcmp0(name, NOTES_STORE_MANIFEST) == 0) continue;
            gchar* full_dir = g_build_filename(root, name, NULL);
            if (!g_file_test(full_dir, G_FILE_TEST_IS_DIR)) {
                g_free(full_dir);
                continue;
            }

            /* Find if notebook already registered */
            NotebookItem* target_nb = NULL;
            for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
                NotebookItem* nb = (NotebookItem*)l->data;
                if (g_strcmp0(nb->name, name) == 0) {
                    target_nb = nb;
                    break;
                }
            }
            if (target_nb == NULL) {
                target_nb = notebook_item_new(name, NOTEBOOK_ICON_DEFAULT, full_dir);
                mgr->notebooks = g_list_append(mgr->notebooks, target_nb);
            }

            /* Scan files inside notebook directory */
            GDir* nd = g_dir_open(full_dir, 0, NULL);
            if (nd != NULL) {
                const gchar* fname = NULL;
                while ((fname = g_dir_read_name(nd)) != NULL) {
                    if (fname[0] == '.') continue;
                    gchar* fpath = g_build_filename(full_dir, fname, NULL);
                    /* Notes are files, and Canvas directories */
                    if (!g_file_test(fpath, G_FILE_TEST_IS_DIR) ||
                        note_type_from_filename(fname) == NOTE_TYPE_CANVAS) {
                        gboolean found = FALSE;
                        for (GList* nl = target_nb->notes; nl != NULL; nl = nl->next) {
                            NoteItem* note = (NoteItem*)nl->data;
                            if (g_strcmp0(note->filename, fname) == 0) {
                                found = TRUE;
                                break;
                            }
                        }
                        if (!found) {
                            /* Strip extension for display title if present */
                            gchar* title = g_strdup(fname);
                            if (note_type_has_note_ext(title)) {
                                gchar* dot = strrchr(title, '.');
                                if (dot != NULL) *dot = '\0';
                            }
                            NoteType ntype = note_type_from_filename(fname);
                            NoteItem* item = note_item_new(fname, title, fpath, ntype);
                            target_nb->notes = g_list_append(target_nb->notes, item);
                            g_free(title);
                        }
                    }
                    g_free(fpath);
                }
                g_dir_close(nd);
            }
            g_free(full_dir);
        }
        g_dir_close(d);
    }

    /* If no notebooks exist, create default General notebook with a welcome note */
    if (mgr->notebooks == NULL) {
        gchar* gen_path = g_build_filename(root, "General", NULL);
        g_mkdir_with_parents(gen_path, 0755);

        NotebookItem* gen = notebook_item_new("General", NOTEBOOK_ICON_DEFAULT, gen_path);
        mgr->notebooks = g_list_append(mgr->notebooks, gen);

        gchar* note_path = g_build_filename(gen_path, "Welcome Note.txt", NULL);
        const gchar* welcome_text =
            "# Welcome to Notes!\n\n"
            "This notebook manager lets you organize and order your notes:\n"
            "- 4 Note Types: Plain Text, Markdown (with tables and images), Org-mode,\n"
            "  and Canvas (text boxes and pasted pictures placed freely)\n"
            "- Create notebooks (carnets) and give each one an icon\n"
            "- Move notes between notebooks easily\n"
            "- Reorder notes and notebooks using Shift+J / Shift+K\n"
            "- Delete notes to the Trash bin (corbeille) and restore them anytime\n"
            "- Right-click any note or notebook for full action menus\n";
        g_file_set_contents(note_path, welcome_text, -1, NULL);

        NoteItem* item = note_item_new("Welcome Note.txt", "Welcome Note", note_path, NOTE_TYPE_TEXT);
        gen->notes = g_list_append(gen->notes, item);

        g_free(note_path);
        g_free(gen_path);
    }
}

gchar* notes_store_trash_dir(const NotesManager* mgr) {
    gchar* trash = g_build_filename(mgr->root, NOTES_STORE_TRASH, NULL);
    g_mkdir_with_parents(trash, 0755);
    return trash;
}

NotesManager* notes_store_load(const gchar* root) {
    g_return_val_if_fail(root != NULL, NULL);
    NotesManager* mgr = g_new0(NotesManager, 1);
    mgr->root = g_strdup(root);
    mgr->next_note_id = 1;
    g_mkdir_with_parents(root, 0755);
    g_free(notes_store_trash_dir(mgr));

    gchar* xml_path = g_build_filename(root, NOTES_STORE_MANIFEST, NULL);
    gchar* content = NULL;
    gsize length = 0;
    if (g_file_get_contents(xml_path, &content, &length, NULL)) {
        static const GMarkupParser parser = { notes_parse_start, notes_parse_end, NULL, NULL, NULL };
        NotesParseState st = { mgr, NULL, FALSE };
        GMarkupParseContext* ctx = g_markup_parse_context_new(&parser, 0, &st, NULL);
        if (!g_markup_parse_context_parse(ctx, content, (gssize)length, NULL) ||
            !g_markup_parse_context_end_parse(ctx, NULL)) {
            g_warning("Notes manifest %s was malformed; recovering from disk", xml_path);
        }
        g_markup_parse_context_free(ctx);
        g_free(content);
    }
    g_free(xml_path);

    reconcile_with_disk(mgr);
    return mgr;
}

void notes_store_free(NotesManager* mgr) {
    if (mgr == NULL) return;
    g_list_free_full(mgr->notebooks, (GDestroyNotify)notebook_item_free);
    g_list_free_full(mgr->trash, (GDestroyNotify)trash_item_free);
    g_free(mgr->root);
    g_free(mgr);
}
