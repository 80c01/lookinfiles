/* Multi-file symbol rename via LSP (see code_rename.h) */
#include "code/code_rename.h"
#include "code/code_intel.h"
#include "code/code_symbol.h"
#include "code/lsp_edit.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "editor/editor.h"
#include "editor/file_save.h"
#include "widgets/dialog.h"
#include <string.h>

#define RESPONSE_RENAME 1

typedef struct {
    AppData* app;
    EditorTab* tab;
    CodeNavMessageFunc message;
    gint line;
    gint col; /* byte index */
    gchar* old_name;
    gchar* new_name;
    gboolean utf8;
} RenameCtx;

static void rename_ctx_free(RenameCtx* ctx) {
    if (ctx == NULL) return;
    g_free(ctx->old_name);
    g_free(ctx->new_name);
    g_free(ctx);
}

static gboolean tab_alive(AppData* app, EditorTab* tab) {
    return app != NULL && tab != NULL && g_list_find(app->editors, tab) != NULL;
}

static void report_msg(RenameCtx* ctx, const gchar* msg) {
    if (ctx->message != NULL && tab_alive(ctx->app, ctx->tab)) {
        ctx->message(ctx->tab, msg);
    }
}

/* Deep copy of LspFileEdits* for background worker */
static LspFileEdits* copy_file_edits(const LspFileEdits* src) {
    LspFileEdits* dst = g_new0(LspFileEdits, 1);
    dst->path = g_strdup(src->path);
    dst->edits = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_text_edit_free);
    for (guint i = 0; src->edits != NULL && i < src->edits->len; i++) {
        const LspTextEdit* te = g_ptr_array_index(src->edits, i);
        LspTextEdit* c = g_new0(LspTextEdit, 1);
        *c = *te;
        c->new_text = g_strdup(te->new_text);
        g_ptr_array_add(dst->edits, c);
    }
    return dst;
}

/* Apply edits directly to an open GtkTextBuffer in descending order (single undo) */
typedef struct {
    gint sl, sc;
    gint el, ec;
    const gchar* text;
} BufEdit;

static gint cmp_buf_edit_desc(gconstpointer a, gconstpointer b) {
    const BufEdit* ea = a;
    const BufEdit* eb = b;
    if (ea->sl != eb->sl) return eb->sl - ea->sl;
    if (ea->sc != eb->sc) return eb->sc - ea->sc;
    if (ea->el != eb->el) return eb->el - ea->el;
    return eb->ec - ea->ec;
}

static gchar* buf_line_slice(GtkTextBuffer* buf, gint line) {
    gint lines = gtk_text_buffer_get_line_count(buf);
    if (line < 0 || line >= lines) return g_strdup("");
    GtkTextIter s, e;
    gtk_text_buffer_get_iter_at_line(buf, &s, line);
    e = s;
    if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_to_line_end(&e);
    return gtk_text_iter_get_slice(&s, &e);
}

static guint apply_edits_to_buffer(GtkTextBuffer* buf, GPtrArray* edits, gboolean utf8) {
    if (buf == NULL || edits == NULL || edits->len == 0) return 0;
    GArray* list = g_array_sized_new(FALSE, FALSE, sizeof(BufEdit), edits->len);
    for (guint i = 0; i < edits->len; i++) {
        const LspTextEdit* te = g_ptr_array_index(edits, i);
        gchar* sline = buf_line_slice(buf, te->start_line);
        gint sc = lsp_column_to_byte(sline, -1, te->start_col, utf8);
        g_free(sline);
        gchar* eline = buf_line_slice(buf, te->end_line);
        gint ec = lsp_column_to_byte(eline, -1, te->end_col, utf8);
        g_free(eline);
        BufEdit be = {.sl = te->start_line, .sc = sc, .el = te->end_line, .ec = ec,
                      .text = te->new_text != NULL ? te->new_text : ""};
        g_array_append_val(list, be);
    }
    g_array_sort(list, cmp_buf_edit_desc);

    gint lines = gtk_text_buffer_get_line_count(buf);
    gtk_text_buffer_begin_user_action(buf);
    for (guint i = 0; i < list->len; i++) {
        const BufEdit* be = &g_array_index(list, BufEdit, i);
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_line(buf, &s, CLAMP(be->sl, 0, lines - 1));
        GtkTextIter seol = s;
        if (!gtk_text_iter_ends_line(&seol)) gtk_text_iter_forward_to_line_end(&seol);
        gtk_text_iter_set_line_index(&s, CLAMP(be->sc, 0, gtk_text_iter_get_line_index(&seol)));

        gtk_text_buffer_get_iter_at_line(buf, &e, CLAMP(be->el, 0, lines - 1));
        GtkTextIter eeol = e;
        if (!gtk_text_iter_ends_line(&eeol)) gtk_text_iter_forward_to_line_end(&eeol);
        gtk_text_iter_set_line_index(&e, CLAMP(be->ec, 0, gtk_text_iter_get_line_index(&eeol)));

        gtk_text_buffer_delete(buf, &s, &e);
        gtk_text_buffer_insert(buf, &s, be->text, -1);
    }
    gtk_text_buffer_end_user_action(buf);
    guint n = list->len;
    g_array_free(list, TRUE);
    return n;
}

typedef struct {
    AppData* app;
    EditorTab* tab;
    CodeNavMessageFunc message;
    gchar* old_name;
    gchar* new_name;
    gboolean utf8;
    GPtrArray* disk_files; /* LspFileEdits* */
    guint open_edits;
    guint open_files;
    guint disk_edits;
    guint disk_ok;
    guint disk_failed;
} DiskRenameJob;

static void disk_rename_job_free(gpointer data) {
    DiskRenameJob* j = data;
    g_free(j->old_name);
    g_free(j->new_name);
    if (j->disk_files != NULL) g_ptr_array_unref(j->disk_files);
    g_free(j);
}

static void finish_rename_summary(AppData* app, EditorTab* tab, CodeNavMessageFunc message,
                                  const gchar* old_name, const gchar* new_name,
                                  guint total_edits, guint total_files, guint failed) {
    gchar* e_str = g_strdup_printf(ngettext("%u edit", "%u edits", total_edits), total_edits);
    gchar* f_str = g_strdup_printf(ngettext("%u file", "%u files", total_files), total_files);
    gchar* summary;
    if (failed > 0) {
        summary = g_strdup_printf(_("Renamed “%1$s” to “%2$s” (%3$s in %4$s; %5$u failed)"),
                                  old_name, new_name, e_str, f_str, failed);
    } else {
        summary = g_strdup_printf(_("Renamed “%1$s” to “%2$s” (%3$s in %4$s)"),
                                  old_name, new_name, e_str, f_str);
    }
    app_notice(app, summary, FALSE);
    if (message != NULL && tab_alive(app, tab)) message(tab, summary);
    g_free(summary);
    g_free(e_str);
    g_free(f_str);
    app_emit(app, APP_EVENT_FILES_CHANGED);
    app_emit(app, APP_EVENT_STATUS_CHANGED);
}

static void disk_rename_worker(GTask* task, gpointer src, gpointer task_data, GCancellable* canc) {
    (void)src; (void)canc;
    DiskRenameJob* j = task_data;
    for (guint i = 0; i < j->disk_files->len; i++) {
        LspFileEdits* fe = g_ptr_array_index(j->disk_files, i);
        gchar* content = NULL;
        gsize len = 0;
        if (!g_file_get_contents(fe->path, &content, &len, NULL)) {
            j->disk_failed++;
            continue;
        }
        gchar* updated = lsp_edit_apply(content, fe->edits, j->utf8, NULL);
        g_free(content);
        if (updated == NULL) {
            j->disk_failed++;
            continue;
        }
        if (file_save_text(fe->path, updated, -1, NULL)) {
            j->disk_ok++;
            j->disk_edits += fe->edits->len;
        } else {
            j->disk_failed++;
        }
        g_free(updated);
    }
    g_task_return_boolean(task, TRUE);
}

static void on_disk_rename_done(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src; (void)data;
    GTask* task = G_TASK(res);
    DiskRenameJob* j = g_task_get_task_data(task);
    if (j == NULL) return;
    finish_rename_summary(j->app, j->tab, j->message, j->old_name, j->new_name,
                          j->open_edits + j->disk_edits, j->open_files + j->disk_ok, j->disk_failed);
}

static void on_rename_edits(AppData* app, EditorTab* tab, LspWorkspaceEdit* edit, gpointer data) {
    RenameCtx* ctx = data;
    if (edit == NULL || edit->files == NULL || edit->files->len == 0) {
        report_msg(ctx, _("No rename edits returned by language server"));
        rename_ctx_free(ctx);
        return;
    }

    guint open_edits = 0;
    guint open_files = 0;
    GPtrArray* disk = g_ptr_array_new_with_free_func((GDestroyNotify)lsp_file_edits_free);

    for (guint i = 0; i < edit->files->len; i++) {
        LspFileEdits* fe = g_ptr_array_index(edit->files, i);
        if (fe == NULL || fe->path == NULL || fe->edits == NULL || fe->edits->len == 0) continue;
        EditorTab* open = editor_find_tab(app, fe->path);
        if (open != NULL && open->buffer != NULL) {
            open_edits += apply_edits_to_buffer(open->buffer, fe->edits, ctx->utf8);
            open_files++;
        } else {
            g_ptr_array_add(disk, copy_file_edits(fe));
        }
    }

    if (disk->len > 0) {
        DiskRenameJob* j = g_new0(DiskRenameJob, 1);
        j->app = app;
        j->tab = tab;
        j->message = ctx->message;
        j->old_name = g_strdup(ctx->old_name);
        j->new_name = g_strdup(ctx->new_name);
        j->utf8 = ctx->utf8;
        j->disk_files = disk;
        j->open_edits = open_edits;
        j->open_files = open_files;
        GTask* task = g_task_new(NULL, NULL, on_disk_rename_done, NULL);
        g_task_set_task_data(task, j, disk_rename_job_free);
        g_task_run_in_thread(task, disk_rename_worker);
        g_object_unref(task);
    } else {
        g_ptr_array_unref(disk);
        finish_rename_summary(app, tab, ctx->message, ctx->old_name, ctx->new_name,
                              open_edits, open_files, 0);
    }
    rename_ctx_free(ctx);
}

typedef struct {
    RenameCtx* ctx;
    GtkWidget* dialog;
    GtkWidget* entry;
} RenameDialog;

static void rename_dialog_free(gpointer data) {
    RenameDialog* rd = data;
    if (rd->ctx != NULL) rename_ctx_free(rd->ctx);
    g_free(rd);
}

static void submit_rename(RenameDialog* rd) {
    gchar* next = g_strdup(gtk_editable_get_text(GTK_EDITABLE(rd->entry)));
    g_strstrip(next);
    if (next[0] == '\0' || g_strcmp0(next, rd->ctx->old_name) == 0) {
        g_free(next);
        app_dialog_destroy(APP_DIALOG(rd->dialog));
        return;
    }
    RenameCtx* ctx = rd->ctx;
    rd->ctx = NULL; /* transferred to rename request */
    ctx->new_name = next;
    app_dialog_destroy(APP_DIALOG(rd->dialog));

    if (!tab_alive(ctx->app, ctx->tab) ||
        !code_intel_rename(ctx->app, ctx->tab, ctx->line, ctx->col, ctx->new_name, on_rename_edits, ctx)) {
        report_msg(ctx, _("Could not send rename request to language server"));
        rename_ctx_free(ctx);
    }
}

static void on_dialog_response(AppDialog* dialog, gint response, gpointer data) {
    RenameDialog* rd = data;
    if (response == RESPONSE_RENAME) {
        submit_rename(rd);
        return;
    }
    app_dialog_destroy(dialog);
}

static void on_entry_activate(GtkEntry* entry, gpointer data) {
    (void)entry;
    submit_rename(data);
}

static void show_rename_dialog(RenameCtx* ctx) {
    if (!tab_alive(ctx->app, ctx->tab) || ctx->app->window == NULL || app_dialog_is_open(ctx->app->window)) {
        rename_ctx_free(ctx);
        return;
    }
    RenameDialog* rd = g_new0(RenameDialog, 1);
    rd->ctx = ctx;

    rd->dialog = app_dialog_new(_("Rename Symbol"), ctx->app->window,
                                _("_Cancel"), APP_RESPONSE_CANCEL,
                                _("_Rename"), RESPONSE_RENAME, NULL);
    app_dialog_set_width(APP_DIALOG(rd->dialog), 420);
    app_dialog_set_default_response(APP_DIALOG(rd->dialog), RESPONSE_RENAME);

    GtkWidget* area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(area, 14);
    gtk_widget_set_margin_bottom(area, 14);
    gtk_widget_set_margin_start(area, 16);
    gtk_widget_set_margin_end(area, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(rd->dialog))), area);

    gchar* prompt = g_strdup_printf(_("New name for “%s”:"), ctx->old_name);
    GtkWidget* lbl = gtk_label_new(prompt);
    g_free(prompt);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_box_append(GTK_BOX(area), lbl);

    rd->entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(rd->entry), ctx->old_name);
    gtk_editable_select_region(GTK_EDITABLE(rd->entry), 0, -1);
    g_signal_connect(rd->entry, "activate", G_CALLBACK(on_entry_activate), rd);
    gtk_box_append(GTK_BOX(area), rd->entry);

    g_signal_connect(rd->dialog, "response", G_CALLBACK(on_dialog_response), rd);
    g_object_set_data_full(G_OBJECT(rd->dialog), "rename-dialog", rd, rename_dialog_free);
    app_dialog_present(APP_DIALOG(rd->dialog));
    gtk_widget_grab_focus(rd->entry);
}

static void on_prepare_done(AppData* app, EditorTab* tab, gboolean ok, const gchar* placeholder,
                            gint sl, gint sc, gint el, gint ec, gpointer data) {
    (void)app;
    RenameCtx* ctx = data;
    if (!ok) {
        report_msg(ctx, _("Cannot rename the symbol at the cursor"));
        rename_ctx_free(ctx);
        return;
    }
    if (placeholder != NULL && placeholder[0] != '\0') {
        g_free(ctx->old_name);
        ctx->old_name = g_strdup(placeholder);
    } else if (sl >= 0 && sl == el && ec > sc && tab_alive(ctx->app, tab)) {
        gchar* line = buf_line_slice(tab->buffer, sl);
        gsize len = strlen(line);
        if ((gsize)ec <= len) {
            g_free(ctx->old_name);
            ctx->old_name = g_strndup(line + sc, (gsize)(ec - sc));
        }
        g_free(line);
    }
    show_rename_dialog(ctx);
}

void code_rename_start(AppData* app, EditorTab* tab, CodeNavMessageFunc message) {
    if (app == NULL || tab == NULL || tab->buffer == NULL) return;
    if (!code_nav_is_code(tab)) {
        if (message != NULL) message(tab, _("No code navigation for this file type"));
        return;
    }
    if (!code_intel_supports_rename(app, tab)) {
        if (message != NULL) message(tab, _("Rename requires a running language server"));
        return;
    }

    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cur, gtk_text_buffer_get_insert(tab->buffer));
    gint line = gtk_text_iter_get_line(&cur);
    gint col = gtk_text_iter_get_line_index(&cur);

    gchar* line_str = buf_line_slice(tab->buffer, line);
    gint ws = 0, we = 0;
    CodeLang lang = code_lang_from_name(tab->path);
    gchar* word = code_word_at(line_str, -1, col, lang, &ws, &we)
                      ? g_strndup(line_str + ws, (gsize)(we - ws))
                      : NULL;
    g_free(line_str);
    if (word == NULL || word[0] == '\0') {
        g_free(word);
        if (message != NULL) message(tab, _("No symbol at the cursor"));
        return;
    }

    RenameCtx* ctx = g_new0(RenameCtx, 1);
    ctx->app = app;
    ctx->tab = tab;
    ctx->message = message;
    ctx->line = line;
    ctx->col = col;
    ctx->old_name = word;
    ctx->utf8 = code_intel_utf8_positions(app, tab);

    if (!code_intel_prepare_rename(app, tab, line, col, on_prepare_done, ctx)) {
        show_rename_dialog(ctx);
    }
}
