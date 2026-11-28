/* Folder suggestions under a folder field (see folder_suggest.h) */
#include "explorer/folder_suggest.h"
#include "core/folder_complete.h"
#include "explorer/nav_history.h"
#include "search/fuzzy_match.h"
#include "widgets/path_suggest.h"

#include <string.h>

typedef struct {
    gint score;
    const gchar* text;
} Scored;

static gint compare_scored(gconstpointer a, gconstpointer b) {
    const Scored* x = a;
    const Scored* y = b;
    if (x->score != y->score) return y->score > x->score ? 1 : -1;
    return g_utf8_collate(x->text, y->text);
}

/* The recent folders going on from the path typed */
static void add_recent_below(const RecentList* recent, const gchar* text, GPtrArray* out) {
    const gchar* home = g_get_home_dir();
    gboolean tilde = text[0] == '~';
    gchar* typed = tilde ? g_strconcat(home, text + 1, NULL) : g_strdup(text);
    for (guint i = 0; i < recent->folders->len && out->len < PATH_SUGGEST_MAX; i++) {
        const gchar* path = g_ptr_array_index(recent->folders, i);
        if (!g_str_has_prefix(path, typed) || strcmp(path, typed) == 0) continue;
        g_ptr_array_add(out, tilde ? folder_complete_tilde(path, home) : g_strdup(path));
    }
    g_free(typed);
}

/* The recent folders matching text as in Ctrl+P (all of them, most recent
 * first, for no text) */
static void add_recent_matching(const RecentList* recent, const gchar* text, GPtrArray* out) {
    const gchar* home = g_get_home_dir();
    GArray* scored = g_array_new(FALSE, FALSE, sizeof(Scored));
    GPtrArray* shown = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < recent->folders->len; i++) {
        gchar* path = folder_complete_tilde(g_ptr_array_index(recent->folders, i), home);
        g_ptr_array_add(shown, path);
        gint score = text[0] == '\0' ? -(gint)i : fuzzy_match_score(text, path);
        if (text[0] != '\0' && score < 0) continue;
        Scored sc = { score, path };
        g_array_append_val(scored, sc);
    }
    g_array_sort(scored, compare_scored);
    for (guint i = 0; i < scored->len && out->len < PATH_SUGGEST_MAX; i++) {
        g_ptr_array_add(out, g_strdup(g_array_index(scored, Scored, i).text));
    }
    g_array_unref(scored);
    g_ptr_array_unref(shown);
}

static void add_recent(const gchar* text, gboolean is_path, GPtrArray* out, gpointer data) {
    const RecentList* recent = nav_history_recent(data);
    if (is_path) {
        add_recent_below(recent, text, out);
    } else {
        add_recent_matching(recent, text, out);
    }
}

void folder_suggest_attach(GtkWidget* entry, AppData* app) {
    path_suggest_attach(entry, PATH_SUGGEST_FOLDERS);
    path_suggest_set_extra(entry, add_recent, app);
}

gboolean folder_suggest_is_open(GtkWidget* entry) {
    return path_suggest_is_open(entry);
}
