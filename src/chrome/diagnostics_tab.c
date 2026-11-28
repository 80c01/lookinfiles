/* The Diagnostics tab: what is missing on this computer (see diagnostics_tab.h) */
#include "chrome/diagnostics_tab.h"

#include <string.h>

#include "code/code_symbol.h"
#include "code/lsp_config.h"
#include "core/app_events.h"
#include "core/app_paths.h"
#include "core/diagnostics.h"
#include "core/i18n.h"
#include "core/ssh_session.h"
#include "core/vfs.h"
#include "explorer/remote_store.h"
#include "git/git_tab.h"
#include "terminal/terminal.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "widgets/focus_route.h"

#define PAGE_KEY "lookin-diagnostics-tab"
#define ROW_KEY "lookin-diagnostics-row"
#define LANG_SCAN_MAX 2000 /* entries of the folder read to find its languages */

typedef enum { ROW_HEADER, ROW_ITEM, ROW_MORE } RowKind;

/* A line of the list: a section title, a check, or the fold of a section's
 * other checks (language servers, compilers) */
G_DECLARE_FINAL_TYPE(DiagRow, diag_row, DIAG, ROW, GObject)
struct _DiagRow {
    GObject parent;
    RowKind kind;
    gchar* title;   /* section title, fold text */
    DiagItem* item; /* the check (owned by the tab's items) */
    gchar* command; /* install command, or NULL */
    DiagSection section; /* the section folded (ROW_MORE) */
};
G_DEFINE_TYPE(DiagRow, diag_row, G_TYPE_OBJECT)

static void diag_row_finalize(GObject* obj) {
    DiagRow* row = DIAG_ROW(obj);
    g_free(row->title);
    g_free(row->command);
    G_OBJECT_CLASS(diag_row_parent_class)->finalize(obj);
}

static void diag_row_class_init(DiagRowClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = diag_row_finalize;
}

static void diag_row_init(DiagRow* row) {
    (void)row;
}

static DiagRow* row_new(RowKind kind, const gchar* title, DiagItem* item, DiagFamily family) {
    DiagRow* row = g_object_new(diag_row_get_type(), NULL);
    row->kind = kind;
    row->title = g_strdup(title);
    row->item = item;
    if (item != NULL) row->command = diagnostics_install_command(item, family);
    return row;
}

typedef struct {
    AppData* app;
    GtkWidget* page;   /* the notebook page (app->diagnostics_page) */
    GtkWidget* label;  /* tab label (owned ref: kept while the page is detached) */
    GtkWidget* info;   /* header: the system and its package manager */
    GtkWidget* machines; /* GtkDropDown: this computer, then the remote machines */
    GPtrArray* machine_ids; /* RemoteMachine ids of the dropdown after its first entry */
    gchar* machine_id; /* the machine checked, NULL: this computer */
    GCancellable* cancel; /* cancelled with the tab */
    GtkWidget* list;   /* GtkListView */
    GListStore* store; /* DiagRow */
    GPtrArray* items;  /* DiagItem of the last checks, or NULL */
    GHashTable* relevant; /* ids of the language servers and compilers of the folder's languages */
    DiagFamily family;
    guint unfolded;    /* bits (1 << DiagSection) of the sections whose other checks are shown */
    gboolean busy;
    gchar* select_id;  /* check to select once the checks are done, or NULL */
    guint generation;  /* bumped by each check: older results are dropped */
} DiagTab;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".diagnostics { background-color: @bg_view; }"
                  ".diagnostics .diag-header { padding: 4px 10px; color: @fg; }"
                  ".diagnostics .diag-info { color: @fg_subtle; font-size: @size_small; }"
                  ".diagnostics .diag-section { color: @fg_strong; font-weight: bold; padding: 10px 2px 2px 2px; }"
                  ".diagnostics .diag-feature { color: @fg_text; }"
                  ".diagnostics .diag-detail { color: @fg_subtle; font-size: @size_small; }"
                  ".diagnostics .diag-command { font-family: @font_code; font-size: @size_code; color: @fg_text;"
                  "  background-color: @bg_block; border-radius: 4px; padding: 2px 6px; }"
                  ".diagnostics .diag-ok { color: @success; }"
                  ".diagnostics .diag-missing { color: @warning; }"
                  ".diagnostics listview { background: none; }"
                  ".diagnostics listview row { padding: 3px 10px; }");
}

static DiagTab* tab_state(AppData* app) {
    return app != NULL && app->diagnostics_page != NULL
               ? g_object_get_data(G_OBJECT(app->diagnostics_page), PAGE_KEY)
               : NULL;
}

/* "ctags / universal-ctags" */
static gchar* programs_text(gchar** programs) {
    return programs != NULL ? g_strjoinv(" / ", programs) : g_strdup("");
}

/* The second line of a check: what was found, or why it is missing */
static gchar* detail_text(const DiagRow* row) {
    const DiagItem* item = row->item;
    if (!item->checked) return g_strdup(_("Checking…"));
    if (item->found != NULL) return g_strdup(item->found);
    if (item->section == DIAG_SECTION_BUILT_IN) {
        return g_strdup(row->command != NULL ? _("Not in this build: install the package below, then build again")
                                             : _("Not in this build"));
    }
    if (g_strcmp0(item->id, "jupyter") == 0) {
        return g_strdup(_("Python cannot import jupyter_client and ipykernel (a project's virtual environment "
                          "needs them too)"));
    }
    gchar* programs = programs_text(item->programs);
    gchar* text = row->command != NULL ? g_strdup_printf(_("Not found: %s"), programs)
                                       : g_strdup_printf(_("Not found: %s (no install command known for this "
                                                           "system)"),
                                                         programs);
    g_free(programs);
    return text;
}

static void on_copy_clicked(GtkButton* button, gpointer data) {
    DiagTab* st = data;
    DiagRow* row = g_object_get_data(G_OBJECT(button), ROW_KEY);
    if (row == NULL || row->command == NULL) return;
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(button)), row->command);
    gchar* text = g_strdup_printf(_("Copied: %s"), row->command);
    app_notice(st->app, text, FALSE);
    g_free(text);
}

/* What to type in a new terminal of the workspace to run command on the
 * machine checked: the command itself when the terminals open there, an ssh
 * line from this computer; NULL when the terminals open on another machine */
static gchar* typed_command(DiagTab* st, const gchar* command) {
    RemoteMachine* active = remote_get_active_machine(st->app);
    if (g_strcmp0(active != NULL ? active->id : NULL, st->machine_id) == 0) return g_strdup(command);
    if (active != NULL) return NULL;
    RemoteMachine* rm = remote_find_by_id(st->app, st->machine_id);
    if (rm == NULL) return NULL;
    SshTarget target = remote_machine_target(rm);
    GString* s = g_string_new("ssh -t");
    if (target.port > 0 && target.port != 22) g_string_append_printf(s, " -p %d", target.port);
    if (target.identity_file != NULL && target.identity_file[0] != '\0') {
        gchar* q = g_shell_quote(target.identity_file);
        g_string_append_printf(s, " -i %s", q);
        g_free(q);
    }
    gchar* dest = ssh_session_destination(&target);
    gchar* q_dest = g_shell_quote(dest);
    gchar* q_command = g_shell_quote(command);
    g_string_append_printf(s, " %s %s", q_dest, q_command);
    g_free(q_command);
    g_free(q_dest);
    g_free(dest);
    return g_string_free(s, FALSE);
}

static void on_terminal_clicked(GtkButton* button, gpointer data) {
    DiagTab* st = data;
    DiagRow* row = g_object_get_data(G_OBJECT(button), ROW_KEY);
    if (row == NULL || row->command == NULL) return;
    gchar* text = typed_command(st, row->command);
    if (text != NULL) terminal_open_typed(st->app, text);
    g_free(text);
}

static void fill_store(DiagTab* st);

static void on_more_clicked(GtkButton* button, gpointer data) {
    DiagTab* st = data;
    DiagRow* row = g_object_get_data(G_OBJECT(button), ROW_KEY);
    if (row == NULL) return;
    st->unfolded ^= 1u << row->section;
    fill_store(st);
}

/* Row widget: [icon] feature / detail ... command [Copy]; a section title;
 * or the fold button */
static void on_setup(GtkSignalListItemFactory* factory, GtkListItem* li, gpointer data) {
    (void)factory;
    DiagTab* st = data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    /* One icon per state, the state's own shown */
    static const gchar* const ICONS[] = {"view-more", "object-select", "dialog-warning"};
    static const gchar* const CLASSES[] = {"diag-detail", "diag-ok", "diag-missing"};
    for (gsize i = 0; i < G_N_ELEMENTS(ICONS); i++) {
        GtkWidget* icon = icon_image_new(ICONS[i]);
        gtk_widget_add_css_class(icon, CLASSES[i]);
        gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(box), icon);
        g_object_set_data(G_OBJECT(box), ICONS[i], icon);
    }
    GtkWidget* texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(texts, TRUE);
    GtkWidget* feature = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(feature), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(feature), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(texts), feature);
    GtkWidget* detail = gtk_label_new(NULL);
    gtk_widget_add_css_class(detail, "diag-detail");
    gtk_label_set_xalign(GTK_LABEL(detail), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(detail), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(texts), detail);
    gtk_box_append(GTK_BOX(box), texts);
    GtkWidget* command = gtk_label_new(NULL);
    gtk_widget_add_css_class(command, "diag-command");
    gtk_label_set_selectable(GTK_LABEL(command), TRUE);
    gtk_widget_set_focusable(command, FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(command), PANGO_ELLIPSIZE_END);
    gtk_widget_set_valign(command, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), command);
    GtkWidget* copy = gtk_button_new_with_label(_("Copy"));
    gtk_widget_set_tooltip_text(copy, _("Copy the Install Command"));
    gtk_widget_set_valign(copy, GTK_ALIGN_CENTER);
    g_signal_connect(copy, "clicked", G_CALLBACK(on_copy_clicked), st);
    gtk_box_append(GTK_BOX(box), copy);
    GtkWidget* term = gtk_button_new_with_label(_("Open in Terminal"));
    gtk_widget_set_valign(term, GTK_ALIGN_CENTER);
    g_signal_connect(term, "clicked", G_CALLBACK(on_terminal_clicked), st);
    gtk_box_append(GTK_BOX(box), term);
    g_object_set_data(G_OBJECT(box), "terminal", term);
    GtkWidget* more = gtk_button_new_with_label("");
    gtk_button_set_has_frame(GTK_BUTTON(more), FALSE);
    g_signal_connect(more, "clicked", G_CALLBACK(on_more_clicked), st);
    gtk_box_append(GTK_BOX(box), more);
    g_object_set_data(G_OBJECT(box), "feature", feature);
    g_object_set_data(G_OBJECT(box), "detail", detail);
    g_object_set_data(G_OBJECT(box), "command", command);
    g_object_set_data(G_OBJECT(box), "copy", copy);
    g_object_set_data(G_OBJECT(box), "more", more);
    gtk_list_item_set_child(li, box);
}

static void on_bind(GtkSignalListItemFactory* factory, GtkListItem* li, gpointer data) {
    (void)factory;
    DiagRow* row = gtk_list_item_get_item(li);
    GtkWidget* box = gtk_list_item_get_child(li);
    GtkWidget* icon_wait = g_object_get_data(G_OBJECT(box), "view-more");
    GtkWidget* icon_ok = g_object_get_data(G_OBJECT(box), "object-select");
    GtkWidget* icon_missing = g_object_get_data(G_OBJECT(box), "dialog-warning");
    GtkWidget* feature = g_object_get_data(G_OBJECT(box), "feature");
    GtkWidget* detail = g_object_get_data(G_OBJECT(box), "detail");
    GtkWidget* command = g_object_get_data(G_OBJECT(box), "command");
    GtkWidget* copy = g_object_get_data(G_OBJECT(box), "copy");
    GtkWidget* term = g_object_get_data(G_OBJECT(box), "terminal");
    GtkWidget* more = g_object_get_data(G_OBJECT(box), "more");
    gboolean is_item = row->kind == ROW_ITEM;
    gtk_list_item_set_selectable(li, is_item);
    gtk_list_item_set_activatable(li, FALSE);
    const DiagItem* item = row->item;
    gtk_widget_set_visible(icon_wait, is_item && !item->checked);
    gtk_widget_set_visible(icon_ok, is_item && item->checked && item->found != NULL);
    gtk_widget_set_visible(icon_missing, is_item && item->checked && item->found == NULL);
    gtk_widget_set_visible(detail, is_item);
    gtk_widget_set_visible(feature, row->kind != ROW_MORE);
    gtk_widget_set_visible(more, row->kind == ROW_MORE);
    gtk_widget_set_visible(command, is_item && row->command != NULL);
    gtk_widget_set_visible(copy, is_item && row->command != NULL);
    g_object_set_data(G_OBJECT(copy), ROW_KEY, row);
#ifdef HAVE_VTE
    gtk_widget_set_visible(term, is_item && row->command != NULL);
#else
    gtk_widget_set_visible(term, FALSE);
#endif
    g_object_set_data(G_OBJECT(term), ROW_KEY, row);
    g_object_set_data(G_OBJECT(more), ROW_KEY, row);
    if (is_item && row->command != NULL) {
        gchar* typed = typed_command(data, row->command);
        gtk_widget_set_sensitive(term, typed != NULL);
        gtk_widget_set_tooltip_text(term, typed != NULL ? _("Type the Install Command in a New Terminal (Not Run)")
                                                        : _("The terminals of this workspace open on another machine"));
        g_free(typed);
    }
    gtk_widget_remove_css_class(feature, "diag-section");
    gtk_widget_remove_css_class(feature, "diag-feature");
    if (row->kind == ROW_HEADER) {
        gtk_widget_add_css_class(feature, "diag-section");
        gtk_label_set_text(GTK_LABEL(feature), row->title);
        return;
    }
    if (row->kind == ROW_MORE) {
        gtk_button_set_label(GTK_BUTTON(more), row->title);
        return;
    }
    gtk_widget_add_css_class(feature, "diag-feature");
    gtk_label_set_text(GTK_LABEL(feature),
                       item->section == DIAG_SECTION_LANG_SERVERS ? item->feature : _(item->feature));
    gchar* text = detail_text(row);
    gtk_label_set_text(GTK_LABEL(detail), text);
    gtk_widget_set_tooltip_text(detail, text);
    g_free(text);
    gtk_label_set_text(GTK_LABEL(command), row->command != NULL ? row->command : "");
    gtk_widget_set_tooltip_text(command, row->command);
}

static const gchar* section_title(DiagSection section) {
    switch (section) {
    case DIAG_SECTION_CONNECTION:
        return _("Connection");
    case DIAG_SECTION_BUILT_IN:
        return _("Built In");
    case DIAG_SECTION_PROGRAMS:
        return _("Programs");
    case DIAG_SECTION_COMPILERS:
        return _("Compilers and Build Tools");
    case DIAG_SECTION_LANG_SERVERS:
        return _("Language Servers");
    }
    return "";
}

/* A check shown without unfolding its section: any but a missing language
 * server or compiler that the folder's languages do not need */
static gboolean item_shown(DiagTab* st, const DiagItem* item) {
    if (item->section != DIAG_SECTION_LANG_SERVERS && item->section != DIAG_SECTION_COMPILERS) return TRUE;
    return item->found != NULL || !item->checked ||
           (st->relevant != NULL && g_hash_table_contains(st->relevant, item->id));
}

/* The fold of a section: "Show 3 other languages" / "Hide the other tools" */
static gchar* fold_text(DiagSection section, gboolean unfolded, guint hidden) {
    if (section == DIAG_SECTION_COMPILERS) {
        return unfolded ? g_strdup(_("Hide the other tools"))
                        : g_strdup_printf(ngettext("Show %u other tool", "Show %u other tools", hidden), hidden);
    }
    return unfolded ? g_strdup(_("Hide the other languages"))
                    : g_strdup_printf(ngettext("Show %u other language", "Show %u other languages", hidden), hidden);
}

static void select_row(DiagTab* st) {
    if (st->select_id == NULL) return;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(st->store));
    for (guint i = 0; i < n; i++) {
        DiagRow* row = g_list_model_get_item(G_LIST_MODEL(st->store), i);
        gboolean match = row->item != NULL && g_strcmp0(row->item->id, st->select_id) == 0;
        g_object_unref(row);
        if (match) {
            gtk_list_view_scroll_to(GTK_LIST_VIEW(st->list), i, GTK_LIST_SCROLL_SELECT | GTK_LIST_SCROLL_FOCUS,
                                    NULL);
            g_clear_pointer(&st->select_id, g_free);
            return;
        }
    }
}

/* The rows of st->items: sections in order, the other language servers and
 * compilers folded */
static void fill_store(DiagTab* st) {
    g_list_store_remove_all(st->store);
    if (st->items == NULL) return;
    /* A folded check to select is unfolded to be seen */
    DiagItem* wanted = diagnostics_find(st->items, st->select_id);
    if (wanted != NULL && !item_shown(st, wanted)) st->unfolded |= 1u << wanted->section;
    for (gint section = DIAG_SECTION_CONNECTION; section <= DIAG_SECTION_LANG_SERVERS; section++) {
        gboolean titled = FALSE;
        guint hidden = 0;
        for (guint i = 0; i < st->items->len; i++) {
            DiagItem* item = g_ptr_array_index(st->items, i);
            if ((gint)item->section != section) continue;
            if (!item_shown(st, item)) {
                hidden++;
                continue;
            }
            if (!titled) {
                DiagRow* head = row_new(ROW_HEADER, section_title(section), NULL, st->family);
                g_list_store_append(st->store, head);
                g_object_unref(head);
                titled = TRUE;
            }
            DiagRow* row = row_new(ROW_ITEM, NULL, item, st->family);
            g_list_store_append(st->store, row);
            g_object_unref(row);
        }
        if (hidden == 0) continue;
        if (!titled) {
            DiagRow* head = row_new(ROW_HEADER, section_title(section), NULL, st->family);
            g_list_store_append(st->store, head);
            g_object_unref(head);
        }
        gboolean unfolded = (st->unfolded & (1u << section)) != 0;
        gchar* text = fold_text(section, unfolded, hidden);
        DiagRow* more = row_new(ROW_MORE, text, NULL, st->family);
        more->section = section;
        g_free(text);
        g_list_store_append(st->store, more);
        g_object_unref(more);
        if (!unfolded) continue;
        for (guint i = 0; i < st->items->len; i++) {
            DiagItem* item = g_ptr_array_index(st->items, i);
            if ((gint)item->section != section || item_shown(st, item)) continue;
            DiagRow* row = row_new(ROW_ITEM, NULL, item, st->family);
            g_list_store_append(st->store, row);
            g_object_unref(row);
        }
    }
    select_row(st);
}

/* --- Checks, in a worker thread --- */

typedef struct {
    GtkWidget* page;   /* owned ref */
    guint generation;
    gchar* config_ini; /* lsp.ini */
    gchar* folder;     /* folder whose languages come first, or NULL */
    GPtrArray* items;  /* result */
    GHashTable* relevant;
    DiagFamily family;
    gchar* os_name;
} CheckJob;

static void job_free(gpointer data) {
    CheckJob* job = data;
    g_clear_object(&job->page);
    g_free(job->config_ini);
    g_free(job->folder);
    if (job->items != NULL) g_ptr_array_unref(job->items);
    if (job->relevant != NULL) g_hash_table_unref(job->relevant);
    g_free(job->os_name);
    g_free(job);
}

/* The language servers of every language (the C family once) */
static void add_lang_servers(GPtrArray* items, const gchar* ini) {
    for (gint lang = CODE_LANG_NONE + 1; lang <= CODE_LANG_LAST; lang++) {
        if (code_lang_family(lang) == CODE_LANG_C && lang != CODE_LANG_C) continue;
        const gchar* key = lsp_config_key(lang);
        if (key == NULL) continue;
        gchar** lines = lsp_config_command_lines(lang, ini);
        diagnostics_add_lang_server(items, key, code_lang_name(lang), lines);
        g_strfreev(lines);
    }
}

/* Ids of the language servers and compilers of the languages of dir's files
 * (dir and its subfolders, a bounded number of entries) */
static GHashTable* folder_languages(const gchar* dir) {
    GHashTable* ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (dir == NULL) return ids;
    GPtrArray* dirs = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(dirs, g_strdup(dir));
    guint seen = 0;
    for (guint d = 0; d < dirs->len && seen < LANG_SCAN_MAX; d++) {
        GDir* gd = g_dir_open(dirs->pdata[d], 0, NULL);
        if (gd == NULL) continue;
        const gchar* name;
        while ((name = g_dir_read_name(gd)) != NULL && seen < LANG_SCAN_MAX) {
            seen++;
            if (name[0] == '.') continue;
            if (d == 0) {
                gchar* path = g_build_filename(dir, name, NULL);
                if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
                    g_ptr_array_add(dirs, path);
                    continue;
                }
                g_free(path);
            }
            CodeLang lang = code_lang_family(code_lang_from_name(name));
            const gchar* key = lang != CODE_LANG_NONE ? lsp_config_key(lang) : NULL;
            if (key != NULL) g_hash_table_add(ids, g_strconcat("lsp-", key, NULL));
            const gchar* tool = diagnostics_build_tool_of(name);
            if (tool != NULL) g_hash_table_add(ids, g_strdup(tool));
        }
        g_dir_close(gd);
    }
    g_ptr_array_unref(dirs);
    return ids;
}

static void check_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source;
    (void)cancel;
    CheckJob* job = data;
    job->items = diagnostics_items(FALSE);
    add_lang_servers(job->items, job->config_ini);
    diagnostics_check_local(job->items);
    job->relevant = folder_languages(job->folder);
    job->family = diagnostics_local_family();
    gchar* text = NULL;
    if (g_file_get_contents("/etc/os-release", &text, NULL, NULL) ||
        g_file_get_contents("/usr/lib/os-release", &text, NULL, NULL)) {
        job->os_name = diagnostics_os_name(text);
    }
    g_free(text);
    g_task_return_boolean(task, TRUE);
}

static const gchar* family_name(DiagFamily family) {
    switch (family) {
    case DIAG_FAMILY_APT:
        return "apt";
    case DIAG_FAMILY_DNF:
        return "dnf";
    case DIAG_FAMILY_PACMAN:
        return "pacman";
    case DIAG_FAMILY_ZYPPER:
        return "zypper";
    case DIAG_FAMILY_APK:
        return "apk";
    case DIAG_FAMILY_BREW:
        return "Homebrew";
    case DIAG_FAMILY_UNKNOWN:
        break;
    }
    return NULL;
}

/* Header: "Ubuntu 24.04 LTS · apt · 3 missing" */
static void show_info(DiagTab* st, const gchar* os_name) {
    guint missing = 0;
    for (guint i = 0; st->items != NULL && i < st->items->len; i++) {
        DiagItem* item = g_ptr_array_index(st->items, i);
        if (item->found == NULL && item_shown(st, item)) {
            missing++;
        }
    }
    GString* s = g_string_new(os_name != NULL ? os_name : st->machine_id == NULL ? _("This computer")
                                                                              : _("unknown system"));
    const gchar* manager = family_name(st->family);
    if (manager != NULL) g_string_append_printf(s, " · %s", manager);
    g_string_append(s, " · ");
    if (missing == 0) {
        g_string_append(s, _("nothing missing"));
    } else {
        g_string_append_printf(s, ngettext("%u missing", "%u missing", missing), missing);
    }
    gtk_label_set_text(GTK_LABEL(st->info), s->str);
    gtk_widget_set_tooltip_text(st->info, s->str);
    g_string_free(s, TRUE);
}

/* Show the results of the checks (the tab takes items and relevant) */
static void show_results(DiagTab* st, GPtrArray* items, GHashTable* relevant, DiagFamily family,
                         const gchar* os_name) {
    st->busy = FALSE;
    g_list_store_remove_all(st->store); /* the rows point into the old items */
    if (st->items != NULL) g_ptr_array_unref(st->items);
    st->items = items;
    if (st->relevant != NULL) g_hash_table_unref(st->relevant);
    st->relevant = relevant;
    st->family = family;
    fill_store(st);
    show_info(st, os_name);
}

static void on_checked(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    CheckJob* job = g_task_get_task_data(G_TASK(res));
    DiagTab* st = g_object_get_data(G_OBJECT(job->page), PAGE_KEY);
    if (st == NULL || job->generation != st->generation) return;
    /* With libssh the ssh program is not used: not missing */
    DiagItem* ssh = diagnostics_find(job->items, "ssh");
    if (ssh != NULL && ssh->found == NULL && ssh_session_transport(NULL) == SSH_TRANSPORT_LIBSSH) {
        ssh->found = g_strdup(_("Not needed: remote machines use the built-in libssh"));
    }
    show_results(st, g_steal_pointer(&job->items), g_steal_pointer(&job->relevant), job->family, job->os_name);
}

/* --- Checks of a remote machine: one command over its shared connection --- */

static const gchar* transport_short(SshTransport transport) {
    switch (transport) {
    case SSH_TRANSPORT_LIBSSH:
        return "libssh";
    case SSH_TRANSPORT_OPENSSH_MUX:
        return _("OpenSSH (shared)");
    case SSH_TRANSPORT_OPENSSH:
        return "OpenSSH";
    }
    return "OpenSSH";
}

static const gchar* transport_detail(SshTransport transport) {
    switch (transport) {
    case SSH_TRANSPORT_LIBSSH:
        return _("libssh (shared connection)");
    case SSH_TRANSPORT_OPENSSH_MUX:
        return _("OpenSSH (shared master connection)");
    case SSH_TRANSPORT_OPENSSH:
        return _("OpenSSH (separate connections)");
    }
    return _("OpenSSH (separate connections)");
}

static void add_transport_item(GPtrArray* items, SshTransport transport) {
    diagnostics_add_info(items, DIAG_SECTION_CONNECTION, "ssh-transport", N_("SSH transport"),
                         transport_detail(transport));
}

typedef struct {
    GtkWidget* page; /* owned ref */
    guint generation;
    GPtrArray* items;
    gchar* name; /* the machine's name */
    SshTransport transport;
} RemoteJob;

static void remote_job_free(RemoteJob* job) {
    g_clear_object(&job->page);
    if (job->items != NULL) g_ptr_array_unref(job->items);
    g_free(job->name);
    g_free(job);
}

static void on_remote_checked(gint status, const gchar* out, const gchar* err, const GError* error, gpointer data) {
    (void)status;
    RemoteJob* job = data;
    DiagTab* st = g_object_get_data(G_OBJECT(job->page), PAGE_KEY);
    if (st == NULL || job->generation != st->generation || g_cancellable_is_cancelled(st->cancel)) {
        remote_job_free(job);
        return;
    }
    if (out == NULL || strstr(out, "@@end") == NULL) {
        GPtrArray* info_items = diagnostics_items_empty();
        add_transport_item(info_items, job->transport);
        show_results(st, info_items, NULL, DIAG_FAMILY_UNKNOWN, NULL);
        gchar* why = g_strstrip(g_strdup(error != NULL ? error->message
                                         : err != NULL && err[0] != '\0' ? err
                                                                          : _("no reply")));
        gchar* text = g_strdup_printf(_("Cannot check %1$s: %2$s"), job->name, why);
        gtk_label_set_text(GTK_LABEL(st->info), text);
        gtk_widget_set_tooltip_text(st->info, text);
        g_free(text);
        g_free(why);
        remote_job_free(job);
        return;
    }
    gchar* os_release = NULL;
    diagnostics_remote_apply(job->items, out, &os_release);
    gchar* os_name = diagnostics_os_name(os_release);
    gchar* shown = g_strdup_printf("%s · %s · %s", job->name, transport_short(job->transport),
                                   os_name != NULL ? os_name : _("unknown system"));
    show_results(st, g_steal_pointer(&job->items), NULL, diagnostics_family(os_release), shown);
    g_free(shown);
    g_free(os_name);
    g_free(os_release);
    remote_job_free(job);
}

/* Check rm: the items with the language servers, the command sent */
static void run_remote_checks(DiagTab* st, RemoteMachine* rm) {
    const gchar* name = rm->name != NULL && rm->name[0] != '\0' ? rm->name : rm->host;
    SshTarget target = remote_machine_target(rm);
    SshTransport transport = ssh_session_transport(&target);
    if (!rm->is_connected) {
        GPtrArray* info_items = diagnostics_items_empty();
        add_transport_item(info_items, transport);
        show_results(st, info_items, NULL, DIAG_FAMILY_UNKNOWN, NULL);
        gchar* text = g_strdup_printf(_("%s is not connected: connect to it in the remote view, then Check Again"),
                                      name);
        gtk_label_set_text(GTK_LABEL(st->info), text);
        gtk_widget_set_tooltip_text(st->info, text);
        g_free(text);
        return;
    }
    RemoteJob* job = g_new0(RemoteJob, 1);
    job->page = g_object_ref(st->page);
    job->generation = st->generation;
    job->name = g_strdup(name);
    job->transport = transport;
    job->items = diagnostics_items(TRUE);
    add_transport_item(job->items, transport);
    gchar* dir = app_config_dir();
    gchar* ini = g_build_filename(dir, "lsp.ini", NULL);
    add_lang_servers(job->items, ini);
    g_free(ini);
    g_free(dir);
    gchar* command = diagnostics_remote_command(job->items);
    ssh_exec_async(&target, command, NULL, st->cancel, on_remote_checked, job);
    g_free(command);
}

static void run_checks(DiagTab* st) {
    st->generation++;
    st->busy = TRUE;
    gtk_label_set_text(GTK_LABEL(st->info), _("Checking…"));
    RemoteMachine* rm = st->machine_id != NULL ? remote_find_by_id(st->app, st->machine_id) : NULL;
    if (rm != NULL) {
        run_remote_checks(st, rm);
        return;
    }
    CheckJob* job = g_new0(CheckJob, 1);
    job->page = g_object_ref(st->page);
    job->generation = st->generation;
    gchar* dir = app_config_dir();
    job->config_ini = g_build_filename(dir, "lsp.ini", NULL);
    g_free(dir);
    const gchar* folder = st->app->base_dir;
    if (folder != NULL && g_path_is_absolute(folder) && !vfs_is_remote(folder)) {
        job->folder = g_strdup(folder);
    }
    GTask* task = g_task_new(NULL, NULL, on_checked, NULL);
    g_task_set_task_data(task, job, job_free);
    g_task_run_in_thread(task, check_thread);
    g_object_unref(task);
}

static void fill_machines(DiagTab* st);

static void on_refresh_clicked(GtkButton* button, gpointer data) {
    (void)button;
    DiagTab* st = data;
    fill_machines(st);
    if (!st->busy || st->machine_id != NULL) run_checks(st); /* a reply that does not come is dropped */
}

static void on_machine_selected(GObject* dropdown, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    DiagTab* st = data;
    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    const gchar* id = i > 0 && i - 1 < st->machine_ids->len ? g_ptr_array_index(st->machine_ids, i - 1) : NULL;
    if (g_strcmp0(id, st->machine_id) == 0) return;
    g_free(st->machine_id);
    st->machine_id = g_strdup(id);
    run_checks(st);
}

/* The machines of the dropdown again (the list may have changed); the one
 * checked stays selected, or this computer when it is gone */
static void fill_machines(DiagTab* st) {
    GtkStringList* names = gtk_string_list_new(NULL);
    gtk_string_list_append(names, _("This computer"));
    g_ptr_array_set_size(st->machine_ids, 0);
    guint selected = 0;
    for (GList* l = remote_machine_list(st->app); l != NULL; l = l->next) {
        RemoteMachine* rm = l->data;
        gtk_string_list_append(names, rm->name != NULL && rm->name[0] != '\0' ? rm->name : rm->host);
        g_ptr_array_add(st->machine_ids, g_strdup(rm->id));
        if (g_strcmp0(rm->id, st->machine_id) == 0) selected = st->machine_ids->len;
    }
    if (selected == 0) g_clear_pointer(&st->machine_id, g_free);
    g_signal_handlers_block_by_func(st->machines, on_machine_selected, st);
    gtk_drop_down_set_model(GTK_DROP_DOWN(st->machines), G_LIST_MODEL(names));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->machines), selected);
    g_signal_handlers_unblock_by_func(st->machines, on_machine_selected, st);
    gtk_widget_set_visible(st->machines, st->machine_ids->len > 0);
    g_object_unref(names);
}

/* --- The page --- */

static void tab_free(gpointer data) {
    DiagTab* st = data;
    g_list_store_remove_all(st->store);
    g_clear_object(&st->store);
    if (st->items != NULL) g_ptr_array_unref(st->items);
    if (st->relevant != NULL) g_hash_table_unref(st->relevant);
    g_free(st->select_id);
    g_free(st->machine_id);
    g_ptr_array_unref(st->machine_ids);
    g_cancellable_cancel(st->cancel);
    g_clear_object(&st->cancel);
    g_clear_object(&st->label);
    g_free(st);
}

static void close_page(AppData* app) {
    if (app->diagnostics_page == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* closed = app->diagnostics_page;
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    gint num = gtk_notebook_page_num(nb, closed);
    app->diagnostics_page = NULL;
    if (num >= 0) gtk_notebook_remove_page(nb, num);
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed); /* frees the state, once a check still running is done */
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_page(((DiagTab*)data)->app);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_page(((DiagTab*)data)->app);
}

/* Put the page in the notebook: after the Git tab and the result pages */
static void insert_page(AppData* app, DiagTab* st) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* git_page = git_tab_page(app);
    gint after = git_page != NULL ? gtk_notebook_page_num(nb, git_page) : 0;
    GtkWidget* pages[] = {app->refs_page, app->search_page, app->help_page, app->sysmon_page};
    for (gsize i = 0; i < G_N_ELEMENTS(pages); i++) {
        if (pages[i] != NULL) after = MAX(after, gtk_notebook_page_num(nb, pages[i]));
    }
    gtk_notebook_insert_page(nb, st->page, st->label, after + 1);
    app->diagnostics_page = st->page;
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

/* Take the page out of the notebook with its tab: an owned ref, or NULL */
static GtkWidget* detach_page(AppData* app) {
    GtkWidget* page = app->diagnostics_page;
    if (page == NULL || app->notebook == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint n = gtk_notebook_page_num(nb, page);
    app->diagnostics_page = NULL;
    if (n >= 0) gtk_notebook_remove_page(nb, n);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    return page;
}

static DiagTab* ensure_page(AppData* app) {
    DiagTab* st = tab_state(app);
    if (st != NULL || app->notebook == NULL) return st;
    ensure_css();
    st = g_new0(DiagTab, 1);
    st->app = app;
    st->store = g_list_store_new(diag_row_get_type());
    st->machine_ids = g_ptr_array_new_with_free_func(g_free);
    st->cancel = g_cancellable_new();
    RemoteMachine* active = remote_get_active_machine(app);
    if (active != NULL) st->machine_id = g_strdup(active->id);

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "diagnostics");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);
    st->page = page;

    /* Header: title, system, Check Again, close button */
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "diag-header");
    gtk_box_append(GTK_BOX(bar), icon_label_new("applications-system", _("Diagnostics")));
    st->info = gtk_label_new(NULL);
    gtk_widget_add_css_class(st->info, "diag-info");
    gtk_label_set_xalign(GTK_LABEL(st->info), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(st->info), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(st->info, TRUE);
    gtk_box_append(GTK_BOX(bar), st->info);
    st->machines = gtk_drop_down_new(NULL, NULL);
    gtk_widget_set_tooltip_text(st->machines, _("Machine to Check"));
    gtk_widget_set_valign(st->machines, GTK_ALIGN_CENTER);
    g_signal_connect(st->machines, "notify::selected", G_CALLBACK(on_machine_selected), st);
    gtk_box_append(GTK_BOX(bar), st->machines);
    fill_machines(st);
    GtkWidget* refresh = icon_button_new("view-refresh", _("Check Again"));
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_refresh_clicked), st);
    gtk_box_append(GTK_BOX(bar), refresh);
    GtkWidget* close = icon_button_new("window-close", _("Close Diagnostics"));
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), st);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(page), bar);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), st);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), st);
    GtkSingleSelection* sel = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(st->store)));
    gtk_single_selection_set_autoselect(sel, FALSE);
    gtk_single_selection_set_can_unselect(sel, TRUE);
    st->list = gtk_list_view_new(GTK_SELECTION_MODEL(sel), factory);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), st->list);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(page), scroll);

    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, st, tab_free);

    st->label = g_object_ref(icon_label_new("applications-system", _("Diagnostics")));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), st);
    gtk_widget_add_controller(st->label, GTK_EVENT_CONTROLLER(mclick));

    insert_page(app, st);
    run_checks(st);
    return st;
}

void diagnostics_tab_show(AppData* app, const gchar* id) {
    if (app == NULL) return;
    DiagTab* st = ensure_page(app);
    if (st == NULL) return;
    if (id != NULL) {
        g_free(st->select_id);
        st->select_id = g_strdup(id);
        if (!st->busy) fill_store(st);
    }
    /* The preview panel holds the tab: show it when hidden */
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        app_layout_begin(app);
        gtk_widget_set_visible(app->viewer_box, TRUE);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, st->page));
    gtk_widget_grab_focus(st->list);
}

gboolean diagnostics_tab_is_open(AppData* app) {
    return tab_state(app) != NULL;
}

gboolean diagnostics_tab_close_shown(AppData* app) {
    if (app == NULL || app->diagnostics_page == NULL || app->notebook == NULL) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, app->diagnostics_page)) return FALSE;
    if (!gtk_widget_get_mapped(app->diagnostics_page)) return FALSE;
    close_page(app);
    return TRUE;
}

void diagnostics_tab_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GtkWidget* page = detach_page(app);
    if (page == NULL) return;
    if (ws->diagnostics_page != NULL) g_object_unref(ws->diagnostics_page);
    ws->diagnostics_page = page;
}

void diagnostics_tab_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->diagnostics_page == NULL) return;
    GtkWidget* page = ws->diagnostics_page;
    ws->diagnostics_page = NULL;
    DiagTab* st = g_object_get_data(G_OBJECT(page), PAGE_KEY);
    if (st != NULL && app->notebook != NULL && app->diagnostics_page == NULL) insert_page(app, st);
    g_object_unref(page);
}

void diagnostics_tab_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_page(app);
        if (page != NULL) g_object_unref(page);
    }
    g_clear_object(&ws->diagnostics_page);
}
