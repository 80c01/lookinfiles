/* What the bar of an editor tab tells about its file: git branch, relative path, language, code navigation backend, diagnostics. */
#include "editor/editor_info.h"
#include "code/code_intel.h"
#include "code/code_nav.h"
#include "core/file_types.h"
#include "core/i18n.h"
#include "git/git_utils.h"
#include <string.h>

#define PLACE_KEY "editor-info-place"

/* Branch and relative path of a file, read once per path */
typedef struct {
    gchar* path;
    gchar* branch;
    gchar* relpath;
} FilePlace;

static void file_place_free(gpointer data) {
    FilePlace* p = data;
    g_free(p->path);
    g_free(p->branch);
    g_free(p->relpath);
    g_free(p);
}

/* path below dir ("src/main.c"), NULL when it is not inside dir */
static gchar* path_below(const gchar* path, const gchar* dir) {
    if (dir == NULL) {
        return NULL;
    }
    gsize len = strlen(dir);
    while (len > 1 && dir[len - 1] == '/') {
        len--;
    }
    if (len == 0 || strncmp(path, dir, len) != 0 || path[len] != '/') {
        return NULL;
    }
    return g_strdup(path + len + 1);
}

static FilePlace* file_place_new(AppData* app, EditorTab* tab) {
    FilePlace* p = g_new0(FilePlace, 1);
    p->path = g_strdup(tab->path);
    if (tab->path == NULL || tab->is_buffer) {
        p->relpath = g_strdup(tab->name != NULL ? tab->name : "");
        return p;
    }
    gchar* dir = g_path_get_dirname(tab->path);
    gchar* root = find_git_root(dir);
    p->branch = git_read_branch(dir);
    g_free(dir);
    p->relpath = path_below(tab->path, root);
    g_free(root);
    if (p->relpath == NULL) {
        p->relpath = path_below(tab->path, app->base_dir);
    }
    if (p->relpath == NULL) {
        gchar* home = path_below(tab->path, g_get_home_dir());
        p->relpath = home != NULL ? g_strconcat("~/", home, NULL) : g_strdup(tab->path);
        g_free(home);
    }
    return p;
}

static const FilePlace* file_place(AppData* app, EditorTab* tab) {
    FilePlace* p = g_object_get_data(G_OBJECT(tab->view), PLACE_KEY);
    if (p == NULL || g_strcmp0(p->path, tab->path) != 0) {
        p = file_place_new(app, tab);
        g_object_set_data_full(G_OBJECT(tab->view), PLACE_KEY, p, file_place_free);
    }
    return p;
}

void editor_info_forget(EditorTab* tab) {
    if (tab != NULL && tab->view != NULL) {
        g_object_set_data(G_OBJECT(tab->view), PLACE_KEY, NULL);
    }
}

static void fill_language(EditorTab* tab, EditorInfo* info) {
    if (tab->is_buffer && g_strcmp0(tab->name, "*dungeon*") == 0) {
        info->language = g_strdup("Dungeon");
        info->language_id = g_strdup("dungeon");
        return;
    }
#ifdef HAVE_GSV
    if (GTK_SOURCE_IS_BUFFER(tab->buffer)) {
        GtkSourceLanguage* lang = gtk_source_buffer_get_language(GTK_SOURCE_BUFFER(tab->buffer));
        if (lang != NULL) {
            info->language = g_strdup(gtk_source_language_get_name(lang));
            info->language_id = g_strdup(gtk_source_language_get_id(lang));
            return;
        }
    }
#endif
    const gchar* ext = file_extension(tab->name != NULL ? tab->name : "");
    if (ext[0] != '\0') {
        info->language = g_ascii_strup(ext, -1);
        info->language_id = g_ascii_strdown(ext, -1);
    } else {
        info->language = g_strdup("Text");
        info->language_id = g_strdup("text");
    }
}

void editor_info_get(AppData* app, EditorTab* tab, EditorInfo* info) {
    memset(info, 0, sizeof(*info));
    if (app == NULL || tab == NULL || tab->view == NULL) {
        return;
    }
    const FilePlace* place = file_place(app, tab);
    info->branch = g_strdup(place->branch);
    info->relpath = g_strdup(place->relpath);
    fill_language(tab, info);
    info->backend = code_intel_server_status(app, tab, &info->errors, &info->warnings);
    info->server = (info->backend != NULL);
    if (info->backend == NULL) {
        info->backend = g_strdup(code_nav_backend_label(tab));
    }
}

void editor_info_clear(EditorInfo* info) {
    g_free(info->branch);
    g_free(info->relpath);
    g_free(info->language);
    g_free(info->language_id);
    g_free(info->backend);
    memset(info, 0, sizeof(*info));
}

void editor_info_clear_diag_classes(GtkWidget* label) {
    if (label == NULL) return;
    gtk_widget_remove_css_class(label, "editor-diag-error");
    gtk_widget_remove_css_class(label, "editor-diag-warning");
    gtk_widget_remove_css_class(label, "editor-diag-info");
    gtk_widget_set_tooltip_text(label, NULL);
}

void editor_info_update_line_diag(AppData* app, EditorTab* tab, GtkWidget* target,
                                  const gchar* clear_text, gint line_zero, gboolean* shown) {
    if (target == NULL || tab == NULL || shown == NULL) return;
    LspSeverity sev = LSP_SEVERITY_ERROR;
    gchar* diag = code_intel_line_diagnostic(app, tab, line_zero, &sev);
    if (diag == NULL) {
        if (*shown) {
            editor_info_clear_diag_classes(target);
            gtk_label_set_text(GTK_LABEL(target), clear_text != NULL ? clear_text : "");
            *shown = FALSE;
        }
        return;
    }
    const gchar* pfx;
    if (tab->mode == EDITOR_MODE_EMACS || tab->mode == EDITOR_MODE_ORGMODE) {
        pfx = "Flymake: ";
    } else if (tab->mode == EDITOR_MODE_VIM) {
        pfx = (sev == LSP_SEVERITY_ERROR) ? "E: " : (sev == LSP_SEVERITY_WARNING) ? "W: " : "I: ";
    } else {
        pfx = (sev == LSP_SEVERITY_ERROR) ? _("Error: ")
              : (sev == LSP_SEVERITY_WARNING) ? _("Warning: ") : _("Info: ");
    }
    gchar* full = g_strconcat(pfx, diag, NULL);
    editor_info_clear_diag_classes(target);
    gtk_widget_remove_css_class(target, "vim-message-error");
    gtk_widget_add_css_class(target, sev == LSP_SEVERITY_ERROR ? "editor-diag-error"
                                     : sev == LSP_SEVERITY_WARNING ? "editor-diag-warning" : "editor-diag-info");
    gtk_label_set_text(GTK_LABEL(target), full);
    gtk_widget_set_tooltip_text(target, full);
    *shown = TRUE;
    g_free(full);
    g_free(diag);
}
