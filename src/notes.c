#include "notes.h"
#include "topbar.h"
#include "tree_nav.h"
#include "file_types.h"
#include "editor.h"
#include "viewer.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* Data structures for notes manager */
typedef struct {
    gchar* filename;     /* e.g. "Meeting.txt", "Spec.md", "Tasks.org" */
    gchar* title;        /* e.g. "Meeting" */
    gchar* path;         /* full path on disk */
    NoteType type;       /* NOTE_TYPE_TEXT, NOTE_TYPE_RICH, NOTE_TYPE_ORG */
} NoteItem;

typedef struct {
    gchar* name;         /* e.g. "General", "Work" */
    gchar* icon;         /* e.g. "📓", "💼" */
    gchar* path;         /* full directory path on disk */
    gboolean expanded;
    GList* notes;        /* list of NoteItem* in custom user order */
} NotebookItem;

typedef struct {
    gchar* filename;     /* filename in .trash */
    gchar* title;        /* original title */
    gchar* path;         /* path in .trash */
    gchar* original_nb;  /* name of notebook it came from */
    gint64 trashed_at;   /* timestamp */
    NoteType type;       /* note type */
} TrashNoteItem;

typedef struct {
    GList* notebooks;    /* list of NotebookItem* in custom user order */
    GList* trash;        /* list of TrashNoteItem* */
    gboolean trash_expanded;
    gint sort_mode;      /* 0 = manual, 1 = A-Z, 2 = Z-A */
    gint next_note_id;   /* for Untitled-N */
} NotesManager;

enum {
    NOTE_ACTION_OPEN = 1,
    NOTE_ACTION_NEW_NOTE,
    NOTE_ACTION_NEW_NOTEBOOK,
    NOTE_ACTION_RENAME,
    NOTE_ACTION_CHANGE_ICON,
    NOTE_ACTION_MOVE,
    NOTE_ACTION_DELETE,
    NOTE_ACTION_RESTORE,
    NOTE_ACTION_EMPTY_TRASH,
    NOTE_ACTION_MOVE_UP,
    NOTE_ACTION_MOVE_DOWN,
    NOTE_ACTION_SORT,
    NOTE_ACTION_COLLAPSE,
    NOTE_ACTION_CONVERT_TEXT,
    NOTE_ACTION_CONVERT_RICH,
    NOTE_ACTION_CONVERT_ORG
};

/* ------------------------------------------------------------------------- */
/* Note Type Helpers                                                         */
/* ------------------------------------------------------------------------- */

static NoteType note_type_from_filename(const gchar* filename) {
    if (filename == NULL) return NOTE_TYPE_TEXT;
    const gchar* ext = file_extension(filename);
    if (g_ascii_strcasecmp(ext, "org") == 0 || g_ascii_strcasecmp(ext, "org_archive") == 0) {
        return NOTE_TYPE_ORG;
    }
    if (g_ascii_strcasecmp(ext, "md") == 0 || g_ascii_strcasecmp(ext, "markdown") == 0 ||
        g_ascii_strcasecmp(ext, "mdown") == 0 || g_ascii_strcasecmp(ext, "mkd") == 0) {
        return NOTE_TYPE_RICH;
    }
    return NOTE_TYPE_TEXT;
}

static const gchar* note_type_to_str(NoteType type) {
    switch (type) {
        case NOTE_TYPE_ORG: return "org";
        case NOTE_TYPE_RICH: return "rich";
        default: return "text";
    }
}

static NoteType note_type_from_str(const gchar* str, const gchar* filename) {
    if (str != NULL) {
        if (g_ascii_strcasecmp(str, "org") == 0) return NOTE_TYPE_ORG;
        if (g_ascii_strcasecmp(str, "rich") == 0) return NOTE_TYPE_RICH;
        if (g_ascii_strcasecmp(str, "text") == 0) return NOTE_TYPE_TEXT;
    }
    return note_type_from_filename(filename);
}

static const gchar* note_type_to_ext(NoteType type) {
    switch (type) {
        case NOTE_TYPE_ORG: return ".org";
        case NOTE_TYPE_RICH: return ".md";
        default: return ".txt";
    }
}

static const gchar* note_type_to_icon(NoteType type) {
    switch (type) {
        case NOTE_TYPE_ORG: return "🦄";
        case NOTE_TYPE_RICH: return "✨";
        default: return "📄";
    }
}

static const gchar* note_type_to_name(NoteType type) {
    switch (type) {
        case NOTE_TYPE_ORG: return "Orgmode";
        case NOTE_TYPE_RICH: return "Rich Format";
        default: return "Plain Text";
    }
}

/* ------------------------------------------------------------------------- */
/* Memory helpers                                                            */
/* ------------------------------------------------------------------------- */

static NoteItem* note_item_new(const gchar* filename, const gchar* title, const gchar* path, NoteType type) {
    NoteItem* item = g_new0(NoteItem, 1);
    item->filename = g_strdup(filename);
    item->title = g_strdup(title);
    item->path = g_strdup(path);
    item->type = type;
    return item;
}

static void note_item_free(NoteItem* item) {
    if (item == NULL) return;
    g_free(item->filename);
    g_free(item->title);
    g_free(item->path);
    g_free(item);
}

static NotebookItem* notebook_item_new(const gchar* name, const gchar* icon, const gchar* path) {
    NotebookItem* nb = g_new0(NotebookItem, 1);
    nb->name = g_strdup(name);
    nb->icon = g_strdup(icon != NULL && icon[0] != '\0' ? icon : "📓");
    nb->path = g_strdup(path);
    nb->expanded = TRUE;
    nb->notes = NULL;
    return nb;
}

static void notebook_item_free(NotebookItem* nb) {
    if (nb == NULL) return;
    g_free(nb->name);
    g_free(nb->icon);
    g_free(nb->path);
    g_list_free_full(nb->notes, (GDestroyNotify)note_item_free);
    g_free(nb);
}

static TrashNoteItem* trash_item_new(const gchar* filename, const gchar* title,
                                     const gchar* path, const gchar* original_nb,
                                     gint64 trashed_at, NoteType type) {
    TrashNoteItem* item = g_new0(TrashNoteItem, 1);
    item->filename = g_strdup(filename);
    item->title = g_strdup(title);
    item->path = g_strdup(path);
    item->original_nb = g_strdup(original_nb);
    item->trashed_at = trashed_at;
    item->type = type;
    return item;
}

static void trash_item_free(TrashNoteItem* item) {
    if (item == NULL) return;
    g_free(item->filename);
    g_free(item->title);
    g_free(item->path);
    g_free(item->original_nb);
    g_free(item);
}

static NotesManager* get_notes_mgr(AppData* app) {
    return (app != NULL) ? (NotesManager*)app->notes_mgr : NULL;
}

gchar* notes_get_root_dir(void) {
    const gchar* cfg_dir = g_get_user_config_dir();
    gchar* dir = g_build_filename(cfg_dir, "explorateur_fichiers", "notes", NULL);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static gchar* notes_get_trash_dir(void) {
    gchar* root = notes_get_root_dir();
    gchar* trash = g_build_filename(root, ".trash", NULL);
    g_mkdir_with_parents(trash, 0755);
    g_free(root);
    return trash;
}

static gchar* notes_get_manifest_path(void) {
    gchar* root = notes_get_root_dir();
    gchar* path = g_build_filename(root, "notes.xml", NULL);
    g_free(root);
    return path;
}

/* ------------------------------------------------------------------------- */
/* XML Persistence                                                           */
/* ------------------------------------------------------------------------- */

void notes_save(AppData* app) {
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

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

    gchar* path = notes_get_manifest_path();
    g_file_set_contents(path, out->str, (gssize)out->len, NULL);
    g_free(path);
    g_string_free(out, TRUE);
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
        if (sort_str != NULL) st->mgr->sort_mode = atoi(sort_str);
        if (next_id_str != NULL) st->mgr->next_note_id = atoi(next_id_str);
    } else if (g_strcmp0(element, "notebook") == 0 && name != NULL && name[0] != '\0') {
        gchar* root = notes_get_root_dir();
        gchar* nb_dir = g_build_filename(root, name, NULL);
        g_free(root);
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
        gchar* trash_dir = notes_get_trash_dir();
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

/* ------------------------------------------------------------------------- */
/* Initialization, Legacy Migration, & Disk Reconciliation                   */
/* ------------------------------------------------------------------------- */

static void notes_migrate_legacy_buffers(void) {
    const gchar* cfg_dir = g_get_user_config_dir();
    gchar* old_buf_dir = g_build_filename(cfg_dir, "explorateur_fichiers", "buffers", NULL);
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

static void notes_reconcile_with_disk(NotesManager* mgr) {
    gchar* root = notes_get_root_dir();
    GDir* d = g_dir_open(root, 0, NULL);
    if (d != NULL) {
        const gchar* name = NULL;
        while ((name = g_dir_read_name(d)) != NULL) {
            if (name[0] == '.' || g_strcmp0(name, "notes.xml") == 0) continue;
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
                target_nb = notebook_item_new(name, "📓", full_dir);
                mgr->notebooks = g_list_append(mgr->notebooks, target_nb);
            }

            /* Scan files inside notebook directory */
            GDir* nd = g_dir_open(full_dir, 0, NULL);
            if (nd != NULL) {
                const gchar* fname = NULL;
                while ((fname = g_dir_read_name(nd)) != NULL) {
                    if (fname[0] == '.') continue;
                    gchar* fpath = g_build_filename(full_dir, fname, NULL);
                    if (!g_file_test(fpath, G_FILE_TEST_IS_DIR)) {
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
                            const gchar* ext = file_extension(title);
                            if (ext[0] != '\0' && (g_ascii_strcasecmp(ext, "txt") == 0 ||
                                                   g_ascii_strcasecmp(ext, "md") == 0 ||
                                                   g_ascii_strcasecmp(ext, "org") == 0)) {
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
    g_free(root);

    /* If no notebooks exist, create default General notebook with a welcome note */
    if (mgr->notebooks == NULL) {
        gchar* root_dir = notes_get_root_dir();
        gchar* gen_path = g_build_filename(root_dir, "General", NULL);
        g_mkdir_with_parents(gen_path, 0755);

        NotebookItem* gen = notebook_item_new("General", "📓", gen_path);
        mgr->notebooks = g_list_append(mgr->notebooks, gen);

        gchar* note_path = g_build_filename(gen_path, "Welcome Note.txt", NULL);
        const gchar* welcome_text =
            "# Welcome to Notes!\n\n"
            "This notebook manager lets you organize and order your notes:\n"
            "- 3 Note Types: Plain Text (📄), Rich Format (✨ with Markdown, tables, images), and Org-mode (🦄)\n"
            "- Create notebooks (carnets) with custom icons (📓, 💼, 💡, 📚, etc.)\n"
            "- Move notes between notebooks easily\n"
            "- Reorder notes and notebooks using Shift+J / Shift+K\n"
            "- Delete notes to the Trash bin (corbeille) and restore them anytime\n"
            "- Right-click any note or notebook for full action menus\n";
        g_file_set_contents(note_path, welcome_text, -1, NULL);

        NoteItem* item = note_item_new("Welcome Note.txt", "Welcome Note", note_path, NOTE_TYPE_TEXT);
        gen->notes = g_list_append(gen->notes, item);

        g_free(note_path);
        g_free(gen_path);
        g_free(root_dir);
    }
}

void notes_init(AppData* app) {
    if (app == NULL) return;

    notes_migrate_legacy_buffers();

    NotesManager* mgr = g_new0(NotesManager, 1);
    mgr->next_note_id = 1;
    mgr->sort_mode = 0;
    mgr->trash_expanded = FALSE;
    app->notes_mgr = mgr;

    /* Ensure directories exist */
    gchar* trash_dir = notes_get_trash_dir();
    g_free(trash_dir);

    /* Load XML manifest if it exists */
    gchar* xml_path = notes_get_manifest_path();
    gchar* content = NULL;
    gsize length = 0;
    if (g_file_get_contents(xml_path, &content, &length, NULL)) {
        static const GMarkupParser parser = { notes_parse_start, notes_parse_end, NULL, NULL, NULL };
        NotesParseState st = { mgr, NULL, FALSE };
        GMarkupParseContext* ctx = g_markup_parse_context_new(&parser, 0, &st, NULL);
        if (!g_markup_parse_context_parse(ctx, content, (gssize)length, NULL)) {
            g_warning("Notes manifest %s was malformed; recovering from disk", xml_path);
        }
        g_markup_parse_context_free(ctx);
        g_free(content);
    }
    g_free(xml_path);

    /* Reconcile with filesystem */
    notes_reconcile_with_disk(mgr);
    notes_save(app);
}

void notes_free(AppData* app) {
    if (app == NULL || app->notes_mgr == NULL) return;
    NotesManager* mgr = (NotesManager*)app->notes_mgr;
    g_list_free_full(mgr->notebooks, (GDestroyNotify)notebook_item_free);
    g_list_free_full(mgr->trash, (GDestroyNotify)trash_item_free);
    g_free(mgr);
    app->notes_mgr = NULL;
}

/* ------------------------------------------------------------------------- */
/* Modal Dialog Helpers                                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    GMainLoop* loop;
    gint response;
    GtkWidget* entry;
} SimpleDialogData;

static void on_simple_dlg_response(GtkDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    SimpleDialogData* d = (SimpleDialogData*)user_data;
    d->response = response_id;
    if (g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

static GtkWidget* notes_dialog_body(GtkWidget* dlg) {
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_box_append(GTK_BOX(content), box);
    return box;
}

typedef struct {
    GMainLoop* loop;
    gint response;
    GtkWidget* entry;
    GtkWidget* rb_rich;
    GtkWidget* rb_org;
    GtkWidget* rb_text;
    GtkWidget* preview_label;
} NoteDialogData;

static void update_note_dialog_preview(NoteDialogData* d) {
    if (d->preview_label == NULL || d->entry == NULL) return;
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(d->entry));
    gchar* title = (text != NULL) ? g_strstrip(g_strdup(text)) : NULL;
    const gchar* disp_title = (title != NULL && title[0] != '\0') ? title : "Untitled";

    NoteType type = NOTE_TYPE_RICH;
    if (d->rb_org && gtk_check_button_get_active(GTK_CHECK_BUTTON(d->rb_org))) {
        type = NOTE_TYPE_ORG;
    } else if (d->rb_text && gtk_check_button_get_active(GTK_CHECK_BUTTON(d->rb_text))) {
        type = NOTE_TYPE_TEXT;
    }

    const gchar* icon = note_type_to_icon(type);
    const gchar* ext = note_type_to_ext(type);

    gchar* markup = g_markup_printf_escaped("File Preview: <span size=\"large\">%s</span> <b>%s%s</b>  <span foreground=\"#808080\">(%s)</span>",
                                           icon, disp_title, ext, note_type_to_name(type));
    gtk_label_set_markup(GTK_LABEL(d->preview_label), markup);
    g_free(markup);
    g_free(title);
}

static void on_note_dlg_field_changed(GtkWidget* widget, gpointer user_data) {
    (void)widget;
    update_note_dialog_preview((NoteDialogData*)user_data);
}

static void on_note_dlg_response(GtkDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    NoteDialogData* d = (NoteDialogData*)user_data;
    d->response = response_id;
    if (g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

static gboolean prompt_note_dialog(GtkWindow* parent_win, const gchar* title,
                                   const gchar* ok_btn_text,
                                   const gchar* init_title, NoteType init_type,
                                   gchar** out_title, NoteType* out_type) {
    *out_title = NULL;
    *out_type = init_type;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(title, parent_win,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "Cancel", GTK_RESPONSE_CANCEL,
                                                 ok_btn_text ? ok_btn_text : "OK", GTK_RESPONSE_OK,
                                                 NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    GtkWidget* box = notes_dialog_body(dlg);

    GtkWidget* lbl = gtk_label_new("Note Title:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl);

    GtkWidget* entry = gtk_entry_new();
    if (init_title != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(entry), init_title);
    }
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 6);
    gtk_widget_set_margin_bottom(sep, 6);
    gtk_box_append(GTK_BOX(box), sep);

    GtkWidget* tlabel = gtk_label_new("Select Note Format & Type:");
    gtk_widget_set_halign(tlabel, GTK_ALIGN_START);
    gtk_widget_add_css_class(tlabel, "dim-label");
    gtk_box_append(GTK_BOX(box), tlabel);

    GtkWidget* type_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(type_vbox, 8);

    GtkWidget* rb_rich = gtk_check_button_new_with_label("✨ Rich Format (.md - headings, lists, tables, images)");
    GtkWidget* rb_org = gtk_check_button_new_with_label("🦄 Org-mode (.org - Emacs tasks, TODOs, checkboxes, folding)");
    GtkWidget* rb_text = gtk_check_button_new_with_label("📄 Plain Text (.txt - unformatted plain text)");

    gtk_check_button_set_group(GTK_CHECK_BUTTON(rb_org), GTK_CHECK_BUTTON(rb_rich));
    gtk_check_button_set_group(GTK_CHECK_BUTTON(rb_text), GTK_CHECK_BUTTON(rb_rich));

    if (init_type == NOTE_TYPE_ORG) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(rb_org), TRUE);
    } else if (init_type == NOTE_TYPE_TEXT) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(rb_text), TRUE);
    } else {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(rb_rich), TRUE);
    }

    gtk_box_append(GTK_BOX(type_vbox), rb_rich);
    gtk_box_append(GTK_BOX(type_vbox), rb_org);
    gtk_box_append(GTK_BOX(type_vbox), rb_text);
    gtk_box_append(GTK_BOX(box), type_vbox);

    GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep2, 6);
    gtk_widget_set_margin_bottom(sep2, 6);
    gtk_box_append(GTK_BOX(box), sep2);

    GtkWidget* preview_label = gtk_label_new(NULL);
    gtk_widget_set_halign(preview_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), preview_label);

    NoteDialogData d = {
        g_main_loop_new(NULL, FALSE),
        GTK_RESPONSE_CANCEL,
        entry,
        rb_rich,
        rb_org,
        rb_text,
        preview_label
    };

    update_note_dialog_preview(&d);

    g_signal_connect(entry, "changed", G_CALLBACK(on_note_dlg_field_changed), &d);
    g_signal_connect(rb_rich, "toggled", G_CALLBACK(on_note_dlg_field_changed), &d);
    g_signal_connect(rb_org, "toggled", G_CALLBACK(on_note_dlg_field_changed), &d);
    g_signal_connect(rb_text, "toggled", G_CALLBACK(on_note_dlg_field_changed), &d);
    g_signal_connect(dlg, "response", G_CALLBACK(on_note_dlg_response), &d);

    gtk_window_present(GTK_WINDOW(dlg));
    gtk_widget_grab_focus(entry);
    if (init_title != NULL) {
        gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    }

    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);

    gboolean ok = FALSE;
    if (d.response == GTK_RESPONSE_OK) {
        const gchar* raw = gtk_editable_get_text(GTK_EDITABLE(entry));
        if (raw != NULL) {
            gchar* trimmed = g_strstrip(g_strdup(raw));
            if (trimmed[0] != '\0' && strchr(trimmed, '/') == NULL && strchr(trimmed, '\\') == NULL) {
                *out_title = trimmed;
                if (gtk_check_button_get_active(GTK_CHECK_BUTTON(rb_org))) {
                    *out_type = NOTE_TYPE_ORG;
                } else if (gtk_check_button_get_active(GTK_CHECK_BUTTON(rb_text))) {
                    *out_type = NOTE_TYPE_TEXT;
                } else {
                    *out_type = NOTE_TYPE_RICH;
                }
                ok = TRUE;
            } else {
                g_free(trimmed);
            }
        }
    }

    gtk_window_destroy(GTK_WINDOW(dlg));
    return ok;
}

typedef struct {
    GMainLoop* loop;
    gint response;
    GtkWidget* name_entry;
    GtkWidget* icon_entry;
    GtkWidget* preview_label;
} NotebookDialogData;

static void update_notebook_preview(NotebookDialogData* d) {
    if (d->preview_label == NULL || d->name_entry == NULL || d->icon_entry == NULL) return;
    const gchar* icon_text = gtk_editable_get_text(GTK_EDITABLE(d->icon_entry));
    const gchar* name_text = gtk_editable_get_text(GTK_EDITABLE(d->name_entry));
    gchar* icon_trimmed = (icon_text != NULL) ? g_strstrip(g_strdup(icon_text)) : NULL;
    gchar* name_trimmed = (name_text != NULL) ? g_strstrip(g_strdup(name_text)) : NULL;

    const gchar* display_icon = (icon_trimmed != NULL && icon_trimmed[0] != '\0') ? icon_trimmed : "📓";
    const gchar* display_name = (name_trimmed != NULL && name_trimmed[0] != '\0') ? name_trimmed : "Notebook";

    gchar* markup = g_markup_printf_escaped("Preview: <span size=\"large\">%s</span>  <b>%s</b>", display_icon, display_name);
    gtk_label_set_markup(GTK_LABEL(d->preview_label), markup);
    g_free(markup);
    g_free(icon_trimmed);
    g_free(name_trimmed);
}

static void on_notebook_field_changed(GtkEditable* editable, gpointer user_data) {
    (void)editable;
    update_notebook_preview((NotebookDialogData*)user_data);
}

static void on_preset_icon_clicked(GtkButton* btn, gpointer user_data) {
    NotebookDialogData* d = (NotebookDialogData*)user_data;
    const gchar* icon = gtk_button_get_label(btn);
    if (icon != NULL && d->icon_entry != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(d->icon_entry), icon);
        update_notebook_preview(d);
    }
}

static void on_notebook_dlg_response(GtkDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    NotebookDialogData* d = (NotebookDialogData*)user_data;
    d->response = response_id;
    if (g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

static gboolean prompt_notebook_dialog(GtkWindow* parent_win, const gchar* title,
                                       const gchar* ok_btn_text,
                                       const gchar* init_name, const gchar* init_icon,
                                       gboolean focus_icon,
                                       gchar** out_name, gchar** out_icon) {
    *out_name = NULL;
    *out_icon = NULL;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(title, parent_win,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "Cancel", GTK_RESPONSE_CANCEL,
                                                 ok_btn_text ? ok_btn_text : "OK", GTK_RESPONSE_OK,
                                                 NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    GtkWidget* box = notes_dialog_body(dlg);

    NotebookDialogData d;
    memset(&d, 0, sizeof(d));
    d.loop = g_main_loop_new(NULL, FALSE);
    d.response = GTK_RESPONSE_CANCEL;

    /* 1. Notebook Name */
    GtkWidget* name_lbl = gtk_label_new("Notebook Name:");
    gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), name_lbl);

    GtkWidget* name_entry = gtk_entry_new();
    if (init_name != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(name_entry), init_name);
    }
    gtk_entry_set_activates_default(GTK_ENTRY(name_entry), TRUE);
    gtk_box_append(GTK_BOX(box), name_entry);
    d.name_entry = name_entry;

    /* 2. Choose or Enter Icon */
    GtkWidget* icon_lbl = gtk_label_new("Notebook Icon (preset or custom emoji):");
    gtk_widget_set_halign(icon_lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_top(icon_lbl, 6);
    gtk_box_append(GTK_BOX(box), icon_lbl);

    static const gchar* const PRESET_ICONS_ROW1[] = {
        "📓", "💼", "💡", "📚", "🔬", "🎯", "🛠", "🎨", "⭐"
    };
    static const gchar* const PRESET_ICONS_ROW2[] = {
        "🚀", "📝", "📁", "🏷️", "☕", "🔒", "🌍", "❤️", "📅"
    };

    GtkWidget* pbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    for (guint i = 0; i < G_N_ELEMENTS(PRESET_ICONS_ROW1); i++) {
        GtkWidget* b = gtk_button_new_with_label(PRESET_ICONS_ROW1[i]);
        gtk_widget_set_focusable(b, FALSE);
        g_signal_connect(b, "clicked", G_CALLBACK(on_preset_icon_clicked), &d);
        gtk_box_append(GTK_BOX(pbox1), b);
    }
    gtk_box_append(GTK_BOX(box), pbox1);

    GtkWidget* pbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    for (guint i = 0; i < G_N_ELEMENTS(PRESET_ICONS_ROW2); i++) {
        GtkWidget* b = gtk_button_new_with_label(PRESET_ICONS_ROW2[i]);
        gtk_widget_set_focusable(b, FALSE);
        g_signal_connect(b, "clicked", G_CALLBACK(on_preset_icon_clicked), &d);
        gtk_box_append(GTK_BOX(pbox2), b);
    }
    gtk_box_append(GTK_BOX(box), pbox2);

    /* Custom icon entry */
    GtkWidget* icon_entry_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(icon_entry_row, 4);
    GtkWidget* custom_lbl = gtk_label_new("Icon / Emoji:");
    gtk_box_append(GTK_BOX(icon_entry_row), custom_lbl);

    GtkWidget* icon_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(icon_entry), (init_icon != NULL && init_icon[0] != '\0') ? init_icon : "📓");
    gtk_entry_set_activates_default(GTK_ENTRY(icon_entry), TRUE);
    gtk_widget_set_hexpand(icon_entry, TRUE);
    gtk_box_append(GTK_BOX(icon_entry_row), icon_entry);
    gtk_box_append(GTK_BOX(box), icon_entry_row);
    d.icon_entry = icon_entry;

    /* Live Preview */
    GtkWidget* prev_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(prev_box, 8);
    gtk_widget_set_margin_bottom(prev_box, 4);
    GtkWidget* prev_lbl = gtk_label_new("");
    gtk_widget_set_halign(prev_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(prev_box), prev_lbl);
    gtk_box_append(GTK_BOX(box), prev_box);
    d.preview_label = prev_lbl;

    g_signal_connect(name_entry, "changed", G_CALLBACK(on_notebook_field_changed), &d);
    g_signal_connect(icon_entry, "changed", G_CALLBACK(on_notebook_field_changed), &d);
    g_signal_connect(dlg, "response", G_CALLBACK(on_notebook_dlg_response), &d);

    update_notebook_preview(&d);

    gtk_window_present(GTK_WINDOW(dlg));
    if (focus_icon) {
        gtk_widget_grab_focus(icon_entry);
        gtk_editable_select_region(GTK_EDITABLE(icon_entry), 0, -1);
    } else {
        gtk_widget_grab_focus(name_entry);
        if (init_name != NULL) {
            gtk_editable_select_region(GTK_EDITABLE(name_entry), 0, -1);
        }
    }

    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);

    gboolean success = FALSE;
    if (d.response == GTK_RESPONSE_OK) {
        const gchar* raw_name = gtk_editable_get_text(GTK_EDITABLE(name_entry));
        const gchar* raw_icon = gtk_editable_get_text(GTK_EDITABLE(icon_entry));
        gchar* trimmed_name = (raw_name != NULL) ? g_strstrip(g_strdup(raw_name)) : NULL;
        gchar* trimmed_icon = (raw_icon != NULL) ? g_strstrip(g_strdup(raw_icon)) : NULL;

        if (trimmed_name != NULL && trimmed_name[0] != '\0' &&
            strchr(trimmed_name, '/') == NULL && strchr(trimmed_name, '\\') == NULL &&
            g_strcmp0(trimmed_name, ".") != 0 && g_strcmp0(trimmed_name, "..") != 0) {
            *out_name = trimmed_name;
            *out_icon = (trimmed_icon != NULL && trimmed_icon[0] != '\0') ? trimmed_icon : g_strdup("📓");
            if (trimmed_icon == NULL || trimmed_icon[0] == '\0') {
                g_free(trimmed_icon);
            }
            success = TRUE;
        } else {
            g_free(trimmed_name);
            g_free(trimmed_icon);
        }
    }

    gtk_window_destroy(GTK_WINDOW(dlg));
    return success;
}

static gboolean notes_confirm(AppData* app, const gchar* title, const gchar* message,
                              const gchar* ok_label, gboolean destructive) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons(title, app ? app->window : NULL,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "Cancel", GTK_RESPONSE_CANCEL,
                                                 NULL);
    GtkWidget* ok_btn = gtk_dialog_add_button(GTK_DIALOG(dlg), ok_label, GTK_RESPONSE_OK);
    if (destructive) {
        gtk_widget_add_css_class(ok_btn, "destructive-action");
    } else {
        gtk_widget_add_css_class(ok_btn, "suggested-action");
    }
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), destructive ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_OK);

    GtkWidget* box = notes_dialog_body(dlg);
    GtkWidget* lbl = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl), 55);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl);

    SimpleDialogData d = { g_main_loop_new(NULL, FALSE), GTK_RESPONSE_CANCEL, NULL };
    g_signal_connect(dlg, "response", G_CALLBACK(on_simple_dlg_response), &d);

    gtk_window_present(GTK_WINDOW(dlg));
    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);
    gtk_window_destroy(GTK_WINDOW(dlg));

    return (d.response == GTK_RESPONSE_OK);
}

/* ------------------------------------------------------------------------- */
/* Tree Store Population & Integration                                       */
/* ------------------------------------------------------------------------- */

void notes_append_to_store(GtkTreeStore* store, AppData* app) {
    if (store == NULL || app == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        return;
    }
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    /* 1. Notebooks */
    for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
        NotebookItem* nb = (NotebookItem*)l->data;
        const gchar* icon = (nb->icon != NULL && nb->icon[0] != '\0') ? nb->icon : "📓";
        gchar* label = g_strdup_printf("%s %s", icon, nb->name);
        guint n_notes = g_list_length(nb->notes);
        gchar* badge = (n_notes > 0) ? g_strdup_printf("<span foreground=\"#6e7681\">%u</span>", n_notes)
                                    : g_strdup("");

        GtkTreeIter diter;
        gtk_tree_store_append(store, &diter, NULL);
        gtk_tree_store_set(store, &diter,
                           COL_NAME, nb->name,
                           COL_TYPE, "notebook",
                           COL_LABEL, label,
                           COL_PATH, nb->path,
                           COL_LOADED, TRUE,
                           COL_GIT_STATUS, badge,
                           -1);
        g_free(label);
        g_free(badge);

        if (nb->notes == NULL) {
            GtkTreeIter dummy;
            gtk_tree_store_append(store, &dummy, &diter);
            gtk_tree_store_set(store, &dummy,
                               COL_NAME, "",
                               COL_TYPE, "-",
                               COL_LABEL, "  (empty notebook)",
                               COL_PATH, "",
                               COL_LOADED, TRUE,
                               COL_GIT_STATUS, "",
                               -1);
        } else {
            for (GList* nl = nb->notes; nl != NULL; nl = nl->next) {
                NoteItem* note = (NoteItem*)nl->data;
                const gchar* nicon = note_type_to_icon(note->type);
                gchar* nlabel = g_strdup_printf("%s %s", nicon, note->title);
                const gchar* tbadge = (note->type == NOTE_TYPE_ORG) ? "<span foreground=\"#c586c0\" size=\"small\">org</span>" :
                                      ((note->type == NOTE_TYPE_RICH) ? "<span foreground=\"#4ec9b0\" size=\"small\">rich</span>" : "");
                GtkTreeIter fiter;
                gtk_tree_store_append(store, &fiter, &diter);
                gtk_tree_store_set(store, &fiter,
                                   COL_NAME, note->title,
                                   COL_TYPE, "note",
                                   COL_LABEL, nlabel,
                                   COL_PATH, note->path,
                                   COL_LOADED, TRUE,
                                   COL_GIT_STATUS, tbadge,
                                   -1);
                g_free(nlabel);
            }
        }
    }

    /* 2. Trash Bin (Corbeille) */
    gchar* trash_dir = notes_get_trash_dir();
    guint n_trash = g_list_length(mgr->trash);
    gchar* t_badge = (n_trash > 0) ? g_strdup_printf("<span foreground=\"#e06c75\">%u</span>", n_trash)
                                   : g_strdup("");
    gchar* t_label = g_strdup_printf("🗑 Trash%s", n_trash == 0 ? " (empty)" : "");

    GtkTreeIter titer;
    gtk_tree_store_append(store, &titer, NULL);
    gtk_tree_store_set(store, &titer,
                       COL_NAME, "Trash",
                       COL_TYPE, "trash",
                       COL_LABEL, t_label,
                       COL_PATH, trash_dir,
                       COL_LOADED, TRUE,
                       COL_GIT_STATUS, t_badge,
                       -1);
    g_free(trash_dir);
    g_free(t_label);
    g_free(t_badge);

    if (mgr->trash == NULL) {
        GtkTreeIter dummy;
        gtk_tree_store_append(store, &dummy, &titer);
        gtk_tree_store_set(store, &dummy,
                           COL_NAME, "",
                           COL_TYPE, "-",
                           COL_LABEL, "  (trash is empty)",
                           COL_PATH, "",
                           COL_LOADED, TRUE,
                           COL_GIT_STATUS, "",
                           -1);
    } else {
        for (GList* tl = mgr->trash; tl != NULL; tl = tl->next) {
            TrashNoteItem* tnote = (TrashNoteItem*)tl->data;
            const gchar* nicon = note_type_to_icon(tnote->type);
            gchar* nlabel = g_strdup_printf("%s %s", nicon, tnote->title);
            gchar* origin_badge = g_markup_printf_escaped("<span foreground=\"#6e7681\" size=\"small\">from %s</span>",
                                                  tnote->original_nb ? tnote->original_nb : "General");
            GtkTreeIter fiter;
            gtk_tree_store_append(store, &fiter, &titer);
            gtk_tree_store_set(store, &fiter,
                               COL_NAME, tnote->title,
                               COL_TYPE, "trash_note",
                               COL_LABEL, nlabel,
                               COL_PATH, tnote->path,
                               COL_LOADED, TRUE,
                               COL_GIT_STATUS, origin_badge,
                               -1);
            g_free(nlabel);
            g_free(origin_badge);
        }
    }
}

void notes_reexpand(AppData* app) {
    if (app == NULL || app->tree_view == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        return;
    }
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_first(app->model, &iter)) {
        return;
    }

    do {
        gchar* type = NULL;
        gchar* name = NULL;
        gtk_tree_model_get(app->model, &iter, COL_TYPE, &type, COL_NAME, &name, -1);
        if (g_strcmp0(type, "notebook") == 0 && name != NULL) {
            for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
                NotebookItem* nb = (NotebookItem*)l->data;
                if (g_strcmp0(nb->name, name) == 0 && nb->expanded) {
                    GtkTreePath* tp = gtk_tree_model_get_path(app->model, &iter);
                    gtk_tree_view_expand_row(app->tree_view, tp, FALSE);
                    gtk_tree_path_free(tp);
                    break;
                }
            }
        } else if (g_strcmp0(type, "trash") == 0 && mgr->trash_expanded) {
            GtkTreePath* tp = gtk_tree_model_get_path(app->model, &iter);
            gtk_tree_view_expand_row(app->tree_view, tp, FALSE);
            gtk_tree_path_free(tp);
        }
        g_free(type);
        g_free(name);
    } while (gtk_tree_model_iter_next(app->model, &iter));
}

gboolean notes_handle_row_toggled(AppData* app, GtkTreeIter* iter, gboolean expanded) {
    if (app == NULL || iter == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        return FALSE;
    }
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return FALSE;

    gchar* type = NULL;
    gchar* name = NULL;
    gtk_tree_model_get(app->model, iter, COL_TYPE, &type, COL_NAME, &name, -1);

    if (g_strcmp0(type, "notebook") == 0 && name != NULL) {
        for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
            NotebookItem* nb = (NotebookItem*)l->data;
            if (g_strcmp0(nb->name, name) == 0) {
                nb->expanded = expanded;
                notes_save(app);
                const gchar* icon = (nb->icon != NULL && nb->icon[0] != '\0') ? nb->icon : "📓";
                gchar* label = g_strdup_printf("%s %s", icon, nb->name);
                gtk_tree_store_set(GTK_TREE_STORE(app->model), iter, COL_LABEL, label, -1);
                g_free(label);
                break;
            }
        }
        g_free(type);
        g_free(name);
        return TRUE;
    } else if (g_strcmp0(type, "trash") == 0) {
        mgr->trash_expanded = expanded;
        notes_save(app);
        g_free(type);
        g_free(name);
        return TRUE;
    }

    g_free(type);
    g_free(name);
    return FALSE;
}

static gboolean find_note_iter_by_path(GtkTreeModel* model, GtkTreeIter* parent, const gchar* path, GtkTreeIter* out) {
    GtkTreeIter child;
    if (!gtk_tree_model_iter_children(model, &child, parent)) return FALSE;
    do {
        gchar* p = NULL;
        gtk_tree_model_get(model, &child, COL_PATH, &p, -1);
        if (g_strcmp0(p, path) == 0) {
            g_free(p);
            *out = child;
            return TRUE;
        }
        g_free(p);
        if (find_note_iter_by_path(model, &child, path, out)) return TRUE;
    } while (gtk_tree_model_iter_next(model, &child));
    return FALSE;
}

void notes_refresh(AppData* app) {
    if (app == NULL || app->model == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        return;
    }

    gchar* sel_path = NULL;
    GtkTreeIter sel_iter;
    if (app->selection != NULL && gtk_tree_selection_get_selected(app->selection, NULL, &sel_iter)) {
        gtk_tree_model_get(app->model, &sel_iter, COL_PATH, &sel_path, -1);
    }

    if (app->selection != NULL) {
        g_signal_handlers_block_by_func(app->selection, (gpointer)on_selection_changed, app);
    }

    gtk_tree_store_clear(GTK_TREE_STORE(app->model));
    notes_append_to_store(GTK_TREE_STORE(app->model), app);
    notes_reexpand(app);

    if (app->selection != NULL) {
        g_signal_handlers_unblock_by_func(app->selection, (gpointer)on_selection_changed, app);
    }

    if (sel_path != NULL) {
        GtkTreeIter found;
        if (find_note_iter_by_path(app->model, NULL, sel_path, &found)) {
            GtkTreePath* tp = gtk_tree_model_get_path(app->model, &found);
            select_path(app, tp);
            gtk_tree_path_free(tp);
        } else {
            select_first_row(app);
        }
        g_free(sel_path);
    } else {
        select_first_row(app);
    }
    update_path_label(app);
}

/* ------------------------------------------------------------------------- */
/* Selection Helpers                                                         */
/* ------------------------------------------------------------------------- */

typedef enum {
    SEL_NONE = 0,
    SEL_NOTEBOOK,
    SEL_NOTE,
    SEL_TRASH,
    SEL_TRASH_NOTE
} NotesSelectionType;

static NotesSelectionType get_selected_info(AppData* app, NotebookItem** out_nb, NoteItem** out_note, TrashNoteItem** out_trash) {
    if (out_nb) *out_nb = NULL;
    if (out_note) *out_note = NULL;
    if (out_trash) *out_trash = NULL;
    if (app == NULL || app->selection == NULL || app->sidebar_view != SIDEBAR_VIEW_NOTES) return SEL_NONE;

    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &iter)) return SEL_NONE;

    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return SEL_NONE;

    gchar* type = NULL;
    gchar* path = NULL;
    gchar* name = NULL;
    gtk_tree_model_get(app->model, &iter, COL_TYPE, &type, COL_PATH, &path, COL_NAME, &name, -1);

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

    g_free(type);
    g_free(path);
    g_free(name);
    return res;
}

static void update_open_editors(AppData* app, const gchar* old_path, const gchar* new_path, const gchar* new_name) {
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
}

/* ------------------------------------------------------------------------- */
/* User Actions                                                              */
/* ------------------------------------------------------------------------- */

void notes_new_note(AppData* app) {
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    NotebookItem* target_nb = NULL;
    NotesSelectionType sel = get_selected_info(app, &target_nb, NULL, NULL);
    if (sel == SEL_TRASH || sel == SEL_TRASH_NOTE || target_nb == NULL) {
        if (mgr->notebooks != NULL) {
            target_nb = (NotebookItem*)mgr->notebooks->data;
        }
    }
    if (target_nb == NULL) return;

    gchar* default_title = g_strdup_printf("Untitled-%d", mgr->next_note_id++);
    gchar* title = NULL;
    NoteType type = NOTE_TYPE_RICH;
    if (!prompt_note_dialog(app->window, "New Note", "Create", default_title, NOTE_TYPE_RICH, &title, &type)) {
        g_free(default_title);
        return;
    }
    g_free(default_title);
    if (title == NULL) return;

    const gchar* ext = note_type_to_ext(type);
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

    /* Template content based on note type */
    gchar* starter = NULL;
    if (type == NOTE_TYPE_RICH) {
        starter = g_strdup_printf(
            "# %s\n\n"
            "Rich format note supporting **bold**, *italic*, lists, tables, and images.\n\n"
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

    g_file_set_contents(file_path, starter, -1, NULL);
    g_free(starter);

    NoteItem* note = note_item_new(filename, title, file_path, type);
    target_nb->notes = g_list_append(target_nb->notes, note);
    target_nb->expanded = TRUE;

    notes_save(app);

    /* Open new note in editor tab */
    editor_open_file(app, title, file_path);

    notes_refresh(app);

    GtkTreeIter iter;
    if (find_note_iter_by_path(app->model, NULL, file_path, &iter)) {
        GtkTreePath* tp = gtk_tree_model_get_path(app->model, &iter);
        select_path(app, tp);
        gtk_tree_path_free(tp);
    }

    g_free(title);
    g_free(filename);
    g_free(file_path);
}

void notes_new_notebook(AppData* app) {
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    gchar* name = NULL;
    gchar* icon = NULL;
    if (!prompt_notebook_dialog(app->window, "New Notebook", "Create", "Notebook", "📓", FALSE, &name, &icon)) {
        return;
    }

    gchar* root = notes_get_root_dir();
    gchar* dir_path = g_build_filename(root, name, NULL);
    g_free(root);

    if (g_file_test(dir_path, G_FILE_TEST_EXISTS)) {
        notes_confirm(app, "Notebook Exists", "A notebook with that name already exists. Please choose a different name.", "OK", FALSE);
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

    GtkTreeIter iter;
    if (find_note_iter_by_path(app->model, NULL, dir_path, &iter)) {
        GtkTreePath* tp = gtk_tree_model_get_path(app->model, &iter);
        select_path(app, tp);
        gtk_tree_path_free(tp);
    }

    g_free(name);
    g_free(icon);
    g_free(dir_path);
}

void notes_convert_selected_type(AppData* app, NoteType target_type) {
    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    NotesSelectionType sel = get_selected_info(app, &nb, &note, NULL);
    if (sel != SEL_NOTE || note == NULL || nb == NULL) return;
    if (note->type == target_type) return;

    const gchar* ext = note_type_to_ext(target_type);
    gchar* new_filename = g_strdup_printf("%s%s", note->title, ext);
    gchar* new_path = g_build_filename(nb->path, new_filename, NULL);

    if (g_file_test(new_path, G_FILE_TEST_EXISTS) && g_strcmp0(new_path, note->path) != 0) {
        notes_confirm(app, "Note Exists", "A note with this name and format already exists in this notebook.", "OK", FALSE);
        g_free(new_filename);
        g_free(new_path);
        return;
    }

    if (g_rename(note->path, new_path) == 0) {
        update_open_editors(app, note->path, new_path, note->title);
        g_free(note->filename);
        g_free(note->path);
        note->filename = new_filename;
        note->path = new_path;
        note->type = target_type;

        notes_save(app);
        notes_refresh(app);

        if (app->viewed_path != NULL) {
            load_file_content(app, note->filename, note->path);
        }
    } else {
        g_free(new_filename);
        g_free(new_path);
    }
}

void notes_edit_selected(AppData* app) {
    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    TrashNoteItem* tnote = NULL;
    NotesSelectionType sel = get_selected_info(app, &nb, &note, &tnote);

    if (sel == SEL_NOTE && note != NULL && nb != NULL) {
        gchar* new_title = NULL;
        NoteType new_type = note->type;
        if (!prompt_note_dialog(app->window, "Edit Note", "Save", note->title, note->type, &new_title, &new_type)) {
            return;
        }

        gboolean title_changed = (g_strcmp0(new_title, note->title) != 0);
        gboolean type_changed = (new_type != note->type);

        if (!title_changed && !type_changed) {
            g_free(new_title);
            return;
        }

        const gchar* ext = note_type_to_ext(new_type);
        gchar* new_filename = g_strdup_printf("%s%s", new_title, ext);
        gchar* new_path = g_build_filename(nb->path, new_filename, NULL);

        if (g_file_test(new_path, G_FILE_TEST_EXISTS) && g_strcmp0(new_path, note->path) != 0) {
            notes_confirm(app, "Note Exists", "A note with that title and format already exists in this notebook.", "OK", FALSE);
            g_free(new_title);
            g_free(new_filename);
            g_free(new_path);
            return;
        }

        if (g_rename(note->path, new_path) == 0) {
            update_open_editors(app, note->path, new_path, new_title);
            g_free(note->filename);
            g_free(note->title);
            g_free(note->path);
            note->filename = new_filename;
            note->title = new_title;
            note->path = new_path;
            note->type = new_type;

            notes_save(app);
            notes_refresh(app);

            if (app->viewed_path != NULL) {
                load_file_content(app, note->filename, note->path);
            }
        } else {
            g_free(new_title);
            g_free(new_filename);
            g_free(new_path);
        }
    } else if (sel == SEL_NOTEBOOK && nb != NULL) {
        gchar* new_name = NULL;
        gchar* new_icon = NULL;
        if (!prompt_notebook_dialog(app->window, "Edit Notebook", "Save", nb->name, nb->icon, FALSE, &new_name, &new_icon)) {
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
                notes_confirm(app, "Notebook Exists", "A notebook with that name already exists.", "OK", FALSE);
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

            update_open_editors(app, nb->path, new_dir, NULL);
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

void notes_change_notebook_icon(AppData* app) {
    NotebookItem* nb = NULL;
    NotesSelectionType sel = get_selected_info(app, &nb, NULL, NULL);
    if (sel != SEL_NOTEBOOK || nb == NULL) return;

    gchar* new_name = NULL;
    gchar* new_icon = NULL;
    if (!prompt_notebook_dialog(app->window, "Change Notebook Icon", "Save", nb->name, nb->icon, TRUE, &new_name, &new_icon)) {
        return;
    }

    gboolean icon_changed = (g_strcmp0(new_icon, nb->icon) != 0);
    gboolean name_changed = (g_strcmp0(new_name, nb->name) != 0);

    if (icon_changed) {
        g_free(nb->icon);
        nb->icon = new_icon;
    } else {
        g_free(new_icon);
    }

    if (name_changed) {
        gchar* root = notes_get_root_dir();
        gchar* new_dir = g_build_filename(root, new_name, NULL);
        g_free(root);

        if (!g_file_test(new_dir, G_FILE_TEST_EXISTS) && g_rename(nb->path, new_dir) == 0) {
            update_open_editors(app, nb->path, new_dir, NULL);
            g_free(nb->name);
            g_free(nb->path);
            nb->name = new_name;
            nb->path = new_dir;
            for (GList* nl = nb->notes; nl != NULL; nl = nl->next) {
                NoteItem* n = (NoteItem*)nl->data;
                g_free(n->path);
                n->path = g_build_filename(nb->path, n->filename, NULL);
            }
        } else {
            g_free(new_name);
            g_free(new_dir);
        }
    } else {
        g_free(new_name);
    }

    if (icon_changed || name_changed) {
        notes_save(app);
        notes_refresh(app);
    }
}

void notes_move_selected(AppData* app) {
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    NotebookItem* src_nb = NULL;
    NoteItem* note = NULL;
    NotesSelectionType sel = get_selected_info(app, &src_nb, &note, NULL);

    if (sel != SEL_NOTE || note == NULL || src_nb == NULL) return;
    if (g_list_length(mgr->notebooks) < 2) {
        notes_confirm(app, "Move Note", "There are no other notebooks to move this note to. Create another notebook first.", "OK", FALSE);
        return;
    }

    /* Build dialog with dropdown of target notebooks */
    GtkWidget* dlg = gtk_dialog_new_with_buttons("Move Note to Notebook", app->window,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "Cancel", GTK_RESPONSE_CANCEL,
                                                 "Move", GTK_RESPONSE_OK,
                                                 NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    GtkWidget* box = notes_dialog_body(dlg);

    gchar* prompt = g_strdup_printf("Move note '%s' to notebook:", note->title);
    GtkWidget* lbl = gtk_label_new(prompt);
    g_free(prompt);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl);

    GPtrArray* target_notebooks = g_ptr_array_new();
    GPtrArray* labels = g_ptr_array_new_with_free_func(g_free);
    for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
        NotebookItem* nb = (NotebookItem*)l->data;
        if (nb != src_nb) {
            g_ptr_array_add(target_notebooks, nb);
            g_ptr_array_add(labels, g_strdup_printf("%s %s", nb->icon ? nb->icon : "📓", nb->name));
        }
    }
    g_ptr_array_add(labels, NULL);

    GtkWidget* dd = gtk_drop_down_new_from_strings((const char* const*)labels->pdata);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), 0);
    gtk_box_append(GTK_BOX(box), dd);

    SimpleDialogData d = { g_main_loop_new(NULL, FALSE), GTK_RESPONSE_CANCEL, NULL };
    g_signal_connect(dlg, "response", G_CALLBACK(on_simple_dlg_response), &d);

    gtk_window_present(GTK_WINDOW(dlg));
    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);

    if (d.response == GTK_RESPONSE_OK) {
        guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd));
        if (idx < target_notebooks->len) {
            NotebookItem* dest_nb = (NotebookItem*)g_ptr_array_index(target_notebooks, idx);
            gchar* new_path = g_build_filename(dest_nb->path, note->filename, NULL);

            if (g_rename(note->path, new_path) == 0) {
                update_open_editors(app, note->path, new_path, note->title);
                g_free(note->path);
                note->path = new_path;

                src_nb->notes = g_list_remove(src_nb->notes, note);
                dest_nb->notes = g_list_append(dest_nb->notes, note);
                dest_nb->expanded = TRUE;

                notes_save(app);
                notes_refresh(app);
            } else {
                g_free(new_path);
            }
        }
    }

    g_ptr_array_unref(target_notebooks);
    g_ptr_array_unref(labels);
    gtk_window_destroy(GTK_WINDOW(dlg));
}

void notes_delete_selected(AppData* app) {
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    TrashNoteItem* tnote = NULL;
    NotesSelectionType sel = get_selected_info(app, &nb, &note, &tnote);

    if (sel == SEL_NOTE && note != NULL && nb != NULL) {
        /* Move note to Trash */
        gchar* trash_dir = notes_get_trash_dir();
        gchar* trash_filename = g_strdup_printf("%" G_GINT64_FORMAT "_%s", (gint64)time(NULL), note->filename);
        gchar* trash_path = g_build_filename(trash_dir, trash_filename, NULL);
        g_free(trash_dir);

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
        gchar* msg = g_strdup_printf("Move notebook '%s' and its %u note%s to Trash?", nb->name, count, count == 1 ? "" : "s");
        if (!notes_confirm(app, "Delete Notebook", msg, "Move to Trash", TRUE)) {
            g_free(msg);
            return;
        }
        g_free(msg);

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
        gchar* msg = g_strdup_printf("Permanently delete '%s'? This action cannot be undone.", tnote->title);
        if (notes_confirm(app, "Delete Permanently", msg, "Delete", TRUE)) {
            g_unlink(tnote->path);
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
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    TrashNoteItem* tnote = NULL;
    NotesSelectionType sel = get_selected_info(app, NULL, NULL, &tnote);
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
        dest_nb = notebook_item_new(nb_name, "📓", nb_path);
        mgr->notebooks = g_list_append(mgr->notebooks, dest_nb);
        g_free(nb_path);
    }

    const gchar* ext = note_type_to_ext(tnote->type);
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

        GtkTreeIter iter;
        if (find_note_iter_by_path(app->model, NULL, target_path, &iter)) {
            GtkTreePath* tp = gtk_tree_model_get_path(app->model, &iter);
            select_path(app, tp);
            gtk_tree_path_free(tp);
        }
    }

    g_free(note_filename);
    g_free(target_path);
}

void notes_empty_trash(AppData* app) {
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    guint count = g_list_length(mgr->trash);
    if (count == 0) {
        notes_confirm(app, "Empty Trash", "The Trash bin is already empty.", "OK", FALSE);
        return;
    }

    gchar* msg = g_strdup_printf("Permanently delete all %u item%s in the Trash? This cannot be undone.",
                                 count, count == 1 ? "" : "s");
    if (!notes_confirm(app, "Empty Trash", msg, "Empty Trash", TRUE)) {
        g_free(msg);
        return;
    }
    g_free(msg);

    for (GList* l = mgr->trash; l != NULL; l = l->next) {
        TrashNoteItem* t = (TrashNoteItem*)l->data;
        g_unlink(t->path);
    }
    g_list_free_full(mgr->trash, (GDestroyNotify)trash_item_free);
    mgr->trash = NULL;

    notes_save(app);
    notes_refresh(app);
}

void notes_move_reorder(AppData* app, gint delta) {
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    NotesSelectionType sel = get_selected_info(app, &nb, &note, NULL);

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
    NotesManager* mgr = get_notes_mgr(app);
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

void notes_toggle_collapse_expand(AppData* app) {
    NotesManager* mgr = get_notes_mgr(app);
    if (mgr == NULL) return;

    gboolean any_expanded = FALSE;
    for (GList* l = mgr->notebooks; l != NULL; l = l->next) {
        NotebookItem* nb = (NotebookItem*)l->data;
        if (nb->expanded) {
            any_expanded = TRUE;
            break;
        }
    }

    gboolean target = !any_expanded;
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
    NotesSelectionType sel = get_selected_info(app, &nb, &note, &tnote);

    if (sel == SEL_NOTE && note != NULL) {
        editor_open_file(app, note->filename ? note->filename : note->title, note->path);
    } else if (sel == SEL_TRASH_NOTE && tnote != NULL) {
        gchar* msg = g_strdup_printf("Note '%s' is in the Trash bin.\nDo you want to restore it so you can edit it?", tnote->title);
        if (notes_confirm(app, "Restore Note", msg, "Restore", FALSE)) {
            notes_restore_selected(app);
        }
        g_free(msg);
    } else if (sel == SEL_NOTEBOOK || sel == SEL_TRASH) {
        /* Toggle expand / collapse */
        if (app->selection != NULL) {
            GtkTreeIter iter;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &iter)) {
                GtkTreePath* tp = gtk_tree_model_get_path(app->model, &iter);
                if (gtk_tree_view_row_expanded(app->tree_view, tp)) {
                    gtk_tree_view_collapse_row(app->tree_view, tp);
                } else {
                    gtk_tree_view_expand_row(app->tree_view, tp, FALSE);
                }
                gtk_tree_path_free(tp);
            }
        }
    }
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
    case GDK_KEY_i:
    case GDK_KEY_I:
        if (ctrl) return FALSE;
        {
            NotebookItem* nb = NULL;
            NotesSelectionType st = get_selected_info(app, &nb, NULL, NULL);
            if (st == SEL_NOTEBOOK && nb != NULL) {
                notes_change_notebook_icon(app);
                break;
            }
        }
        return FALSE;
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

    app->pending_g = FALSE;
    app->pending_z = FALSE;
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
    case NOTE_ACTION_CHANGE_ICON:    notes_change_notebook_icon(app); break;
    case NOTE_ACTION_MOVE:           notes_move_selected(app); break;
    case NOTE_ACTION_DELETE:         notes_delete_selected(app); break;
    case NOTE_ACTION_RESTORE:        notes_restore_selected(app); break;
    case NOTE_ACTION_EMPTY_TRASH:    notes_empty_trash(app); break;
    case NOTE_ACTION_MOVE_UP:        notes_move_reorder(app, -1); break;
    case NOTE_ACTION_MOVE_DOWN:      notes_move_reorder(app, +1); break;
    case NOTE_ACTION_SORT:           notes_toggle_sort(app); break;
    case NOTE_ACTION_COLLAPSE:       notes_toggle_collapse_expand(app); break;
    case NOTE_ACTION_CONVERT_TEXT:   notes_convert_selected_type(app, NOTE_TYPE_TEXT); break;
    case NOTE_ACTION_CONVERT_RICH:   notes_convert_selected_type(app, NOTE_TYPE_RICH); break;
    case NOTE_ACTION_CONVERT_ORG:    notes_convert_selected_type(app, NOTE_TYPE_ORG); break;
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

    gint bx = 0, by = 0;
    gtk_tree_view_convert_widget_to_bin_window_coords(app->tree_view, (gint)x, (gint)y, &bx, &by);
    GtkTreePath* tp = NULL;
    if (gtk_tree_view_get_path_at_pos(app->tree_view, bx, by, &tp, NULL, NULL, NULL) && tp != NULL) {
        select_path(app, tp);
        gtk_tree_path_free(tp);
    } else {
        gtk_tree_selection_unselect_all(app->selection);
    }

    NotebookItem* nb = NULL;
    NoteItem* note = NULL;
    TrashNoteItem* tnote = NULL;
    NotesSelectionType sel = get_selected_info(app, &nb, &note, &tnote);

    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(app->tree_view));
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    GdkRectangle rect = { (gint)x, (gint)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    if (sel == SEL_NOTE) {
        note_menu_item(box, popover, app, "Open Note", "Enter", NOTE_ACTION_OPEN, TRUE);
        note_menu_item(box, popover, app, "Edit / Rename Note...", "F2", NOTE_ACTION_RENAME, TRUE);
        note_menu_item(box, popover, app, "Move to Notebook...", "m", NOTE_ACTION_MOVE, TRUE);
        note_menu_separator(box);
        if (note != NULL && note->type != NOTE_TYPE_TEXT) {
            note_menu_item(box, popover, app, "Convert to Plain Text (📄 .txt)", NULL, NOTE_ACTION_CONVERT_TEXT, TRUE);
        }
        if (note != NULL && note->type != NOTE_TYPE_RICH) {
            note_menu_item(box, popover, app, "Convert to Rich Format (✨ .md)", NULL, NOTE_ACTION_CONVERT_RICH, TRUE);
        }
        if (note != NULL && note->type != NOTE_TYPE_ORG) {
            note_menu_item(box, popover, app, "Convert to Org-mode (🦄 .org)", NULL, NOTE_ACTION_CONVERT_ORG, TRUE);
        }
        note_menu_separator(box);
        note_menu_item(box, popover, app, "Move Up", "Shift+K", NOTE_ACTION_MOVE_UP, TRUE);
        note_menu_item(box, popover, app, "Move Down", "Shift+J", NOTE_ACTION_MOVE_DOWN, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, "Move to Trash", "Delete", NOTE_ACTION_DELETE, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, "New Note Here", "a", NOTE_ACTION_NEW_NOTE, TRUE);
        note_menu_item(box, popover, app, "New Notebook...", "Shift+A", NOTE_ACTION_NEW_NOTEBOOK, TRUE);
    } else if (sel == SEL_NOTEBOOK) {
        note_menu_item(box, popover, app, "New Note Here", "a", NOTE_ACTION_NEW_NOTE, TRUE);
        note_menu_item(box, popover, app, "Edit Notebook...", "F2", NOTE_ACTION_RENAME, TRUE);
        note_menu_item(box, popover, app, "Change Icon...", "i", NOTE_ACTION_CHANGE_ICON, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, "Move Up", "Shift+K", NOTE_ACTION_MOVE_UP, TRUE);
        note_menu_item(box, popover, app, "Move Down", "Shift+J", NOTE_ACTION_MOVE_DOWN, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, "Delete Notebook to Trash", "Delete", NOTE_ACTION_DELETE, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, "New Notebook...", "Shift+A", NOTE_ACTION_NEW_NOTEBOOK, TRUE);
    } else if (sel == SEL_TRASH_NOTE) {
        note_menu_item(box, popover, app, "Restore Note", "R", NOTE_ACTION_RESTORE, TRUE);
        note_menu_item(box, popover, app, "Delete Permanently", "Delete", NOTE_ACTION_DELETE, TRUE);
        note_menu_separator(box);
        note_menu_item(box, popover, app, "Empty Trash...", NULL, NOTE_ACTION_EMPTY_TRASH, TRUE);
    } else if (sel == SEL_TRASH) {
        note_menu_item(box, popover, app, "Empty Trash...", NULL, NOTE_ACTION_EMPTY_TRASH, TRUE);
    } else {
        note_menu_item(box, popover, app, "New Note", "a", NOTE_ACTION_NEW_NOTE, TRUE);
        note_menu_item(box, popover, app, "New Notebook...", "Shift+A", NOTE_ACTION_NEW_NOTEBOOK, TRUE);
    }

    note_menu_separator(box);
    note_menu_item(box, popover, app, "Sort / Order Mode", NULL, NOTE_ACTION_SORT, TRUE);
    note_menu_item(box, popover, app, "Collapse / Expand All", NULL, NOTE_ACTION_COLLAPSE, TRUE);

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
