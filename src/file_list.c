#include "file_list.h"
#include "editor.h"
#include "minimap.h"
#include "terminal.h"
#include "viewer.h"
#include "search.h"
#include "ui.h"

/* File extension ("" if none), ignoring dot-files initial dot */
const gchar* file_extension(const gchar* name) {
    if (name == NULL) {
        return "";
    }
    const gchar* dot = strrchr(name, '.');
    return (dot != NULL && dot != name) ? dot + 1 : "";
}

/* Return TRUE if file name indicates Markdown content */
gboolean is_markdown_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "md") == 0 ||
            g_ascii_strcasecmp(ext, "markdown") == 0 ||
            g_ascii_strcasecmp(ext, "mdown") == 0 ||
            g_ascii_strcasecmp(ext, "mkd") == 0 ||
            g_ascii_strcasecmp(ext, "mkdn") == 0);
}

/* Return TRUE if file name indicates HTML content */
gboolean is_html_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "html") == 0 ||
            g_ascii_strcasecmp(ext, "htm") == 0 ||
            g_ascii_strcasecmp(ext, "xhtml") == 0 ||
            g_ascii_strcasecmp(ext, "shtml") == 0);
}

/* Return TRUE if file name indicates an image format */
gboolean is_image_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "png") == 0 ||
            g_ascii_strcasecmp(ext, "jpg") == 0 ||
            g_ascii_strcasecmp(ext, "jpeg") == 0 ||
            g_ascii_strcasecmp(ext, "gif") == 0 ||
            g_ascii_strcasecmp(ext, "svg") == 0 ||
            g_ascii_strcasecmp(ext, "svgz") == 0 ||
            g_ascii_strcasecmp(ext, "bmp") == 0 ||
            g_ascii_strcasecmp(ext, "ico") == 0 ||
            g_ascii_strcasecmp(ext, "webp") == 0 ||
            g_ascii_strcasecmp(ext, "tiff") == 0 ||
            g_ascii_strcasecmp(ext, "tif") == 0 ||
            g_ascii_strcasecmp(ext, "pnm") == 0 ||
            g_ascii_strcasecmp(ext, "pbm") == 0 ||
            g_ascii_strcasecmp(ext, "pgm") == 0 ||
            g_ascii_strcasecmp(ext, "ppm") == 0 ||
            g_ascii_strcasecmp(ext, "xpm") == 0 ||
            g_ascii_strcasecmp(ext, "xbm") == 0 ||
            g_ascii_strcasecmp(ext, "tga") == 0 ||
            g_ascii_strcasecmp(ext, "icns") == 0 ||
            g_ascii_strcasecmp(ext, "cur") == 0);
}

/* Return TRUE if file name indicates a PDF document */
gboolean is_pdf_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "pdf") == 0);
}

/* Return TRUE if file content can be rendered richly in preview pane */
gboolean is_renderable_file(const gchar* name) {
    return is_markdown_file(name) || is_html_file(name) || is_image_file(name) || is_pdf_file(name);
}

/* Sort: directories first, then according to active sort mode (thread-safe) */
static gint compare_entries_with_data(gconstpointer a, gconstpointer b, gpointer user_data) {
    const FileEntry* ea = (const FileEntry*)a;
    const FileEntry* eb = (const FileEntry*)b;
    gint sort_mode = GPOINTER_TO_INT(user_data);

    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }

    gint res = 0;
    switch (sort_mode) {
    case SORT_DATE:
        res = (ea->mtime > eb->mtime) ? -1 : (ea->mtime < eb->mtime) ? 1 : 0; /* newest first */
        break;
    case SORT_SIZE:
        res = (ea->size > eb->size) ? -1 : (ea->size < eb->size) ? 1 : 0; /* largest first */
        break;
    case SORT_EXT: {
        gchar* xa = g_utf8_casefold(file_extension(ea->name), -1);
        gchar* xb = g_utf8_casefold(file_extension(eb->name), -1);
        res = g_strcmp0(xa, xb);
        g_free(xa);
        g_free(xb);
        break;
    }
    default: /* SORT_NAME */
        break;
    }

    if (res == 0) {
        res = g_utf8_collate(ea->name, eb->name); /* tie-breaker by name */
    }
    return res;
}

/* VS Code-like emoji icon for a tree entry, based on name and extension */
const gchar* file_icon(const gchar* name, gboolean is_dir) {
    if (is_dir) {
        return "📁";
    }
    if (name == NULL || name[0] == '\0') {
        return "📄";
    }

    /* 1. Exact filename matches */
#define NAME_IS(x) (g_ascii_strcasecmp(name, (x)) == 0)
#define NAME_EXACT(x) (g_strcmp0(name, (x)) == 0)

    if (NAME_IS("Makefile") || NAME_IS("makefile") || NAME_IS("GNUmakefile") ||
        NAME_IS("CMakeLists.txt") || NAME_IS("meson.build")) {
        return "🛠️";
    }
    if (NAME_IS("Dockerfile") || NAME_IS("Containerfile") ||
        NAME_IS("docker-compose.yml") || NAME_IS("docker-compose.yaml") ||
        NAME_IS(".dockerignore")) {
        return "🐳";
    }
    if (NAME_IS(".gitignore") || NAME_IS(".gitattributes") ||
        NAME_IS(".gitmodules") || NAME_IS(".gitconfig")) {
        return "🔀";
    }
    if (NAME_IS("package.json")) {
        return "📦";
    }
    if (NAME_IS("package-lock.json") || NAME_IS("yarn.lock") ||
        NAME_IS("pnpm-lock.yaml") || NAME_IS("Cargo.lock") ||
        NAME_IS("Gemfile.lock") || NAME_IS("composer.lock") ||
        NAME_IS("poetry.lock") || NAME_IS("Pipfile.lock")) {
        return "🔒";
    }
    if (NAME_IS("LICENSE") || NAME_IS("LICENCE") || NAME_IS("COPYING")) {
        return "⚖️";
    }
    if (NAME_IS("README") || NAME_IS("README.md") || NAME_IS("README.txt") ||
        NAME_IS("CHANGELOG") || NAME_IS("CHANGELOG.md") ||
        NAME_IS("CONTRIBUTING") || NAME_IS("CONTRIBUTING.md")) {
        return "📖";
    }
    if (NAME_EXACT(".env") || g_str_has_prefix(name, ".env.")) {
        return "🔑";
    }
    if (NAME_IS("Cargo.toml")) {
        return "🦀";
    }
    if (NAME_IS("go.mod") || NAME_IS("go.sum")) {
        return "🐹";
    }
    if (NAME_IS("tsconfig.json") || NAME_IS("jsconfig.json")) {
        return "🟦";
    }
    if (NAME_IS(".editorconfig") || NAME_IS(".prettierrc") ||
        g_str_has_prefix(name, ".eslintrc")) {
        return "⚙️";
    }
    if (NAME_IS("Gemfile") || NAME_IS("Rakefile")) {
        return "💎";
    }
#undef NAME_IS
#undef NAME_EXACT

    /* 2. Extension matches */
    const gchar* dot = strrchr(name, '.');
    if (dot == NULL || dot == name) {
        if (name[0] == '.') {
            const gchar* base = name + 1;
            if (g_ascii_strcasecmp(base, "bashrc") == 0 ||
                g_ascii_strcasecmp(base, "zshrc") == 0 ||
                g_ascii_strcasecmp(base, "profile") == 0) {
                return "💻";
            }
            return "⚙️";
        }
        return "📄";
    }

#define EXT_IS(x) (g_ascii_strcasecmp(dot, (x)) == 0)
    /* C language: blue circle like VS Code C */
    if (EXT_IS(".c") || EXT_IS(".h")) {
        return "🔵";
    }
    /* C++: blue diamond */
    if (EXT_IS(".cc") || EXT_IS(".cpp") || EXT_IS(".cxx") ||
        EXT_IS(".hpp") || EXT_IS(".hh") || EXT_IS(".hxx") || EXT_IS(".inl")) {
        return "🔷";
    }
    /* C# */
    if (EXT_IS(".cs") || EXT_IS(".csx")) {
        return "🟩";
    }
    /* Rust */
    if (EXT_IS(".rs")) {
        return "🦀";
    }
    /* Go */
    if (EXT_IS(".go")) {
        return "🐹";
    }
    /* Python */
    if (EXT_IS(".py") || EXT_IS(".pyw") || EXT_IS(".pyx") || EXT_IS(".ipynb")) {
        return "🐍";
    }
    /* Java */
    if (EXT_IS(".java") || EXT_IS(".jar") || EXT_IS(".class")) {
        return "☕";
    }
    /* Kotlin */
    if (EXT_IS(".kt") || EXT_IS(".kts")) {
        return "🟣";
    }
    /* Swift */
    if (EXT_IS(".swift")) {
        return "🐦";
    }
    /* PHP */
    if (EXT_IS(".php")) {
        return "🐘";
    }
    /* Ruby */
    if (EXT_IS(".rb") || EXT_IS(".erb")) {
        return "💎";
    }
    /* React JSX / TSX */
    if (EXT_IS(".jsx") || EXT_IS(".tsx")) {
        return "⚛️";
    }
    /* TypeScript: blue square */
    if (EXT_IS(".ts") || EXT_IS(".mts") || EXT_IS(".cts")) {
        return "🟦";
    }
    /* JavaScript: yellow square */
    if (EXT_IS(".js") || EXT_IS(".mjs") || EXT_IS(".cjs")) {
        return "🟨";
    }
    /* Vue */
    if (EXT_IS(".vue")) {
        return "🟢";
    }
    /* Svelte */
    if (EXT_IS(".svelte")) {
        return "🔥";
    }
    /* HTML */
    if (EXT_IS(".html") || EXT_IS(".htm") || EXT_IS(".xhtml")) {
        return "🌐";
    }
    /* CSS */
    if (EXT_IS(".css")) {
        return "🎨";
    }
    /* SCSS / SASS: pink flower */
    if (EXT_IS(".scss") || EXT_IS(".sass")) {
        return "🌸";
    }
    if (EXT_IS(".less")) {
        return "🎨";
    }
    /* JSON */
    if (EXT_IS(".json") || EXT_IS(".jsonc") || EXT_IS(".json5")) {
        return "📋";
    }
    /* YAML */
    if (EXT_IS(".yaml") || EXT_IS(".yml")) {
        return "🧾";
    }
    /* TOML / INI / Conf */
    if (EXT_IS(".toml") || EXT_IS(".ini") || EXT_IS(".cfg") ||
        EXT_IS(".conf") || EXT_IS(".properties")) {
        return "⚙️";
    }
    /* XML */
    if (EXT_IS(".xml") || EXT_IS(".xsl") || EXT_IS(".xsd")) {
        return "🌐";
    }
    /* Shell / Scripting */
    if (EXT_IS(".sh") || EXT_IS(".bash") || EXT_IS(".zsh") ||
        EXT_IS(".fish") || EXT_IS(".ksh") || EXT_IS(".bat") ||
        EXT_IS(".cmd") || EXT_IS(".ps1")) {
        return "💻";
    }
    /* SQL / Database */
    if (EXT_IS(".sql") || EXT_IS(".db") || EXT_IS(".sqlite") ||
        EXT_IS(".sqlite3") || EXT_IS(".db3")) {
        return "🗄️";
    }
    /* Markdown / Documentation */
    if (EXT_IS(".md") || EXT_IS(".markdown") || EXT_IS(".mdown") ||
        EXT_IS(".rst")) {
        return "📝";
    }
    /* PDF */
    if (EXT_IS(".pdf")) {
        return "📕";
    }
    /* Images */
    if (EXT_IS(".png") || EXT_IS(".jpg") || EXT_IS(".jpeg") || EXT_IS(".gif") ||
        EXT_IS(".svg") || EXT_IS(".bmp") || EXT_IS(".ico") || EXT_IS(".webp") ||
        EXT_IS(".tiff") || EXT_IS(".tif") || EXT_IS(".avif")) {
        return "🖼️";
    }
    /* Audio */
    if (EXT_IS(".mp3") || EXT_IS(".wav") || EXT_IS(".ogg") ||
        EXT_IS(".flac") || EXT_IS(".aac") || EXT_IS(".m4a")) {
        return "🎵";
    }
    /* Video */
    if (EXT_IS(".mp4") || EXT_IS(".avi") || EXT_IS(".mkv") ||
        EXT_IS(".mov") || EXT_IS(".webm") || EXT_IS(".flv")) {
        return "🎬";
    }
    /* Archives */
    if (EXT_IS(".tar") || EXT_IS(".gz") || EXT_IS(".tgz") || EXT_IS(".zip") ||
        EXT_IS(".rar") || EXT_IS(".7z") || EXT_IS(".xz") || EXT_IS(".bz2") ||
        EXT_IS(".zst") || EXT_IS(".tbz2")) {
        return "📦";
    }
    /* Compiled binaries / libraries */
    if (EXT_IS(".o") || EXT_IS(".so") || EXT_IS(".a") || EXT_IS(".bin") ||
        EXT_IS(".exe") || EXT_IS(".dylib") || EXT_IS(".dll") || EXT_IS(".out")) {
        return "⚙️";
    }
    /* Lua */
    if (EXT_IS(".lua")) {
        return "🌙";
    }
    /* Dart */
    if (EXT_IS(".dart")) {
        return "🎯";
    }
    /* Lock files */
    if (EXT_IS(".lock")) {
        return "🔒";
    }
    /* Build files */
    if (EXT_IS(".cmake") || EXT_IS(".mk")) {
        return "🛠️";
    }
    /* Text / Plain */
    if (EXT_IS(".txt") || EXT_IS(".log") || EXT_IS(".csv") ||
        EXT_IS(".tsv") || EXT_IS(".diff") || EXT_IS(".patch")) {
        return "📄";
    }
    return "📄";
#undef EXT_IS
}

/* Total top-level entries in the model */
gint list_count(AppData* app) {
    return gtk_tree_model_iter_n_children(app->model, NULL);
}

/* Return path of the first visible row */
GtkTreePath* get_first_visible_path(GtkTreeView* view) {
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreeIter it;
    if (gtk_tree_model_get_iter_first(model, &it)) {
        return gtk_tree_model_get_path(model, &it);
    }
    return NULL;
}

/* Return next visible row path (following expansions) or NULL if at end */
GtkTreePath* get_next_visible_path(GtkTreeView* view, GtkTreePath* path) {
    if (path == NULL) {
        return NULL;
    }
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreePath* curr = gtk_tree_path_copy(path);

    /* 1. If row is expanded, next visible row is its first child */
    if (gtk_tree_view_row_expanded(view, curr)) {
        GtkTreePath* child = gtk_tree_path_copy(curr);
        gtk_tree_path_down(child);
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(model, &it, child)) {
            gtk_tree_path_free(curr);
            return child;
        }
        gtk_tree_path_free(child);
    }

    /* 2. Try next sibling at current level, or walk up to ancestors */
    while (gtk_tree_path_get_depth(curr) > 0) {
        GtkTreePath* next = gtk_tree_path_copy(curr);
        gtk_tree_path_next(next);
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(model, &it, next)) {
            gtk_tree_path_free(curr);
            return next;
        }
        gtk_tree_path_free(next);
        if (!gtk_tree_path_up(curr)) {
            break;
        }
    }

    gtk_tree_path_free(curr);
    return NULL;
}

/* Return previous visible row path (following expansions) or NULL if at top */
GtkTreePath* get_prev_visible_path(GtkTreeView* view, GtkTreePath* path) {
    if (path == NULL) {
        return NULL;
    }
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreePath* curr = gtk_tree_path_copy(path);

    /* 1. Try previous sibling */
    if (gtk_tree_path_prev(curr)) {
        /* If that sibling is expanded, step to its last visible descendant */
        while (gtk_tree_view_row_expanded(view, curr)) {
            GtkTreeIter parent_it;
            if (!gtk_tree_model_get_iter(model, &parent_it, curr)) {
                break;
            }
            gint n = gtk_tree_model_iter_n_children(model, &parent_it);
            if (n <= 0) {
                break;
            }
            gtk_tree_path_down(curr);
            for (gint i = 1; i < n; i++) {
                gtk_tree_path_next(curr);
            }
        }
        return curr;
    }

    /* 2. No previous sibling: if we have a parent, parent is previous visible */
    if (gtk_tree_path_get_depth(curr) > 1) {
        gtk_tree_path_up(curr);
        return curr;
    }

    gtk_tree_path_free(curr);
    return NULL;
}

/* Return path of the last visible row */
GtkTreePath* get_last_visible_path(GtkTreeView* view) {
    GtkTreePath* cur = get_first_visible_path(view);
    if (cur == NULL) {
        return NULL;
    }
    while (TRUE) {
        GtkTreePath* next = get_next_visible_path(view, cur);
        if (next == NULL) {
            return cur;
        }
        gtk_tree_path_free(cur);
        cur = next;
    }
}

/* Select row at path and scroll it into view */
void select_path(AppData* app, GtkTreePath* path) {
    if (path == NULL) {
        return;
    }
    gboolean was_selected = gtk_tree_selection_path_is_selected(app->selection, path);
    gtk_tree_selection_select_path(app->selection, path);
    gtk_tree_view_set_cursor(app->tree_view, path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell(app->tree_view, path, NULL, FALSE, 0.0, 0.0);
    if (was_selected) {
        on_selection_changed(app->selection, app);
    }
}

/* Select the first visible row */
void select_first_row(AppData* app) {
    GtkTreePath* p = get_first_visible_path(app->tree_view);
    if (p != NULL) {
        select_path(app, p);
        gtk_tree_path_free(p);
    }
}

/* Select the last visible row */
void select_last_row(AppData* app) {
    GtkTreePath* p = get_last_visible_path(app->tree_view);
    if (p != NULL) {
        select_path(app, p);
        gtk_tree_path_free(p);
    }
}

/* Select the next visible row */
void select_next_row(AppData* app) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
    GtkTreePath* next = get_next_visible_path(app->tree_view, path);
    if (next != NULL) {
        select_path(app, next);
        gtk_tree_path_free(next);
    }
    gtk_tree_path_free(path);
}

/* Select the previous visible row */
void select_prev_row(AppData* app) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
    GtkTreePath* prev = get_prev_visible_path(app->tree_view, path);
    if (prev != NULL) {
        select_path(app, prev);
        gtk_tree_path_free(prev);
    }
    gtk_tree_path_free(path);
}

/* Number of visible rows in tree view (for Ctrl-F / Ctrl-B) */
gint count_visible_rows(AppData* app) {
    GtkTreePath* start = NULL;
    GtkTreePath* end = NULL;
    if (!gtk_tree_view_get_visible_range(app->tree_view, &start, &end)) {
        return 10;
    }
    gint count = 1;
    GtkTreePath* cur = gtk_tree_path_copy(start);
    while (gtk_tree_path_compare(cur, end) != 0) {
        GtkTreePath* next = get_next_visible_path(app->tree_view, cur);
        if (next == NULL) {
            break;
        }
        gtk_tree_path_free(cur);
        cur = next;
        count++;
    }
    gtk_tree_path_free(cur);
    gtk_tree_path_free(start);
    gtk_tree_path_free(end);
    return (count > 0) ? count : 10;
}

/* Jump forward by one page of visible rows */
void page_down_rows(AppData* app) {
    gint count = count_visible_rows(app);
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* cur = gtk_tree_model_get_path(app->model, &it);
    for (gint i = 0; i < count; i++) {
        GtkTreePath* next = get_next_visible_path(app->tree_view, cur);
        if (next == NULL) {
            break;
        }
        gtk_tree_path_free(cur);
        cur = next;
    }
    select_path(app, cur);
    gtk_tree_path_free(cur);
}

/* Jump backward by one page of visible rows */
void page_up_rows(AppData* app) {
    gint count = count_visible_rows(app);
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* cur = gtk_tree_model_get_path(app->model, &it);
    for (gint i = 0; i < count; i++) {
        GtkTreePath* prev = get_prev_visible_path(app->tree_view, cur);
        if (prev == NULL) {
            break;
        }
        gtk_tree_path_free(cur);
        cur = prev;
    }
    select_path(app, cur);
    gtk_tree_path_free(cur);
}

static void scroll_selected_row_aligned(AppData* app, gfloat row_align) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        return;
    }
    GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
    if (path != NULL) {
        gtk_tree_view_scroll_to_cell(app->tree_view, path, NULL, TRUE, row_align, 0.0f);
        gtk_tree_path_free(path);
    }
}

/* Center the currently selected row vertically in the tree view (vim 'zz') */
void center_selected_row(AppData* app) {
    scroll_selected_row_aligned(app, 0.5f);
}

/* Scroll the currently selected row to the top of the tree view (vim 'zt') */
void top_selected_row(AppData* app) {
    scroll_selected_row_aligned(app, 0.0f);
}

/* Scroll the currently selected row to the bottom of the tree view (vim 'zb') */
void bottom_selected_row(AppData* app) {
    scroll_selected_row_aligned(app, 1.0f);
}

/* Select top-level row matching name (e.g. directory just left) */
gboolean select_by_name(AppData* app, const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_first(app->model, &it)) {
        return FALSE;
    }
    do {
        gchar* row_name = NULL;
        gchar* row_type = NULL;
        gtk_tree_model_get(app->model, &it,
                           COL_NAME, &row_name,
                           COL_TYPE, &row_type, -1);
        gboolean match = (g_strcmp0(row_type, "bookmark") != 0 &&
                          g_strcmp0(row_name, name) == 0);
        g_free(row_name);
        g_free(row_type);
        if (match) {
            GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
            select_path(app, p);
            gtk_tree_path_free(p);
            return TRUE;
        }
    } while (gtk_tree_model_iter_next(app->model, &it));
    return FALSE;
}

/* Recursive helper to search for a row by absolute path */
static gboolean find_iter_by_path_recursive_internal(GtkTreeModel* model, GtkTreeIter* parent,
                                                     const gchar* full_path, GtkTreeIter* result,
                                                     gboolean allow_bookmarks) {
    GtkTreeIter child;
    if (!gtk_tree_model_iter_children(model, &child, parent)) {
        return FALSE;
    }
    do {
        gchar* path_val = NULL;
        gchar* type_val = NULL;
        gtk_tree_model_get(model, &child,
                           COL_PATH, &path_val,
                           COL_TYPE, &type_val, -1);
        gboolean is_bm = (g_strcmp0(type_val, "bookmark") == 0);
        gboolean match = (g_strcmp0(path_val, full_path) == 0);
        g_free(path_val);
        g_free(type_val);

        if (match && (allow_bookmarks || !is_bm)) {
            *result = child;
            return TRUE;
        }
        if (find_iter_by_path_recursive_internal(model, &child, full_path, result, allow_bookmarks)) {
            return TRUE;
        }
    } while (gtk_tree_model_iter_next(model, &child));
    return FALSE;
}

static gboolean find_iter_by_path_recursive(GtkTreeModel* model, GtkTreeIter* parent,
                                            const gchar* full_path, GtkTreeIter* result) {
    /* First try non-bookmark entries so regular directory rows take precedence */
    if (find_iter_by_path_recursive_internal(model, parent, full_path, result, FALSE)) {
        return TRUE;
    }
    return find_iter_by_path_recursive_internal(model, parent, full_path, result, TRUE);
}

/* Select row matching full path */
gboolean select_by_path(AppData* app, const gchar* full_path) {
    if (full_path == NULL) {
        return FALSE;
    }
    GtkTreeIter it;
    if (find_iter_by_path_recursive(app->model, NULL, full_path, &it)) {
        GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
        select_path(app, p);
        gtk_tree_path_free(p);
        return TRUE;
    }
    return FALSE;
}

/* Find Git repository root directory from start_dir (or any parent), NULL if not in repo */
gchar* find_git_root(const gchar* start_dir) {
    if (start_dir == NULL || start_dir[0] == '\0') {
        return NULL;
    }
    if (strstr(start_dir, "/.git/") != NULL || g_str_has_suffix(start_dir, "/.git") ||
        g_strcmp0(start_dir, ".git") == 0) {
        return NULL;
    }
    gchar* curr = g_canonicalize_filename(start_dir, NULL);
    while (curr != NULL && curr[0] != '\0') {
        gchar* dot_git = g_build_filename(curr, ".git", NULL);
        gboolean exists = g_file_test(dot_git, G_FILE_TEST_EXISTS);
        g_free(dot_git);
        if (exists) {
            return curr; /* found git root */
        }
        gchar* parent = g_path_get_dirname(curr);
        if (g_strcmp0(curr, parent) == 0 || g_strcmp0(parent, ".") == 0) {
            g_free(parent);
            break;
        }
        g_free(curr);
        curr = parent;
    }
    g_free(curr);
    return NULL;
}

/* Run 'git status --porcelain=v1 -uall' and return a mapping of full_path -> badge markup */
GHashTable* scan_git_statuses(const gchar* dir) {
    gchar* raw_root = find_git_root(dir);
    if (raw_root == NULL) {
        return NULL;
    }
    gchar* git_root = g_canonicalize_filename(raw_root, NULL);
    g_free(raw_root);

    gchar* argv[] = {
        "git",
        "--no-optional-locks",
        "-c",
        "core.quotepath=false",
        "status",
        "--porcelain=v1",
        "-uall",
        NULL
    };

    gchar* stdout_buf = NULL;
    gchar* stderr_buf = NULL;
    gint exit_status = 0;
    GError* err = NULL;
    gboolean ok = g_spawn_sync(git_root,
                               argv,
                               NULL,
                               G_SPAWN_SEARCH_PATH,
                               NULL,
                               NULL,
                               &stdout_buf,
                               &stderr_buf,
                               &exit_status,
                               &err);
    if (err != NULL) {
        g_error_free(err);
    }
    g_free(stderr_buf);

    if (!ok || exit_status != 0 || stdout_buf == NULL) {
        g_free(stdout_buf);
        g_free(git_root);
        return NULL;
    }

    GHashTable* statuses = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gsize root_len = strlen(git_root);

    gchar** lines = g_strsplit(stdout_buf, "\n", -1);
    for (gint i = 0; lines != NULL && lines[i] != NULL; i++) {
        const gchar* line = lines[i];
        if (strlen(line) < 4) {
            continue;
        }
        gchar x = line[0];
        gchar y = line[1];
        const gchar* rel = line + 3;
        while (*rel == ' ') {
            rel++;
        }
        if (*rel == '\0') {
            continue;
        }

        /* If renamed: "R  old -> new", target is "new" */
        const gchar* arrow = strstr(rel, " -> ");
        if (arrow != NULL) {
            rel = arrow + 4;
        }

        /* Unquote if enclosed in double quotes */
        gchar* unquoted = NULL;
        if (rel[0] == '"') {
            gsize len = strlen(rel);
            if (len >= 2 && rel[len - 1] == '"') {
                unquoted = g_strndup(rel + 1, len - 2);
                rel = unquoted;
            }
        }

        const gchar* badge = NULL;
        gboolean is_modified = FALSE;
        gboolean is_untracked = FALSE;

        if (x == '?' && y == '?') {
            /* Untracked */
            badge = "<span foreground=\"#73c991\" weight=\"bold\">U</span>";
            is_untracked = TRUE;
        } else if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) {
            /* Conflict / Unmerged */
            badge = "<span foreground=\"#e51400\" weight=\"bold\">!</span>";
            is_modified = TRUE;
        } else if (x == 'A' && (y == ' ' || y == '\0')) {
            /* Added / Staged */
            badge = "<span foreground=\"#73c991\" weight=\"bold\">A</span>";
            is_modified = TRUE;
        } else if (x == 'D' || y == 'D') {
            /* Deleted */
            badge = "<span foreground=\"#c74e39\" weight=\"bold\">D</span>";
            is_modified = TRUE;
        } else if (x == 'R' || y == 'R') {
            /* Renamed */
            badge = "<span foreground=\"#4fc1ff\" weight=\"bold\">R</span>";
            is_modified = TRUE;
        } else if (x == 'M' || y == 'M' || x == 'A' || x == 'T' || y == 'T') {
            /* Modified */
            badge = "<span foreground=\"#e2c08d\" weight=\"bold\">M</span>";
            is_modified = TRUE;
        } else {
            /* Other working tree change */
            badge = "<span foreground=\"#e2c08d\" weight=\"bold\">M</span>";
            is_modified = TRUE;
        }

        gchar* full_raw = g_build_filename(git_root, rel, NULL);
        gchar* full_path = g_canonicalize_filename(full_raw, NULL);
        g_free(full_raw);
        if (unquoted != NULL) {
            g_free(unquoted);
        }

        /* Store file's status */
        g_hash_table_replace(statuses, g_strdup(full_path), g_strdup(badge));

        /* Rollup to parent directories up to git_root */
        gchar* parent = g_path_get_dirname(full_path);
        g_free(full_path);

        while (parent != NULL) {
            gsize parent_len = strlen(parent);
            gboolean under_root = (g_strcmp0(parent, git_root) == 0) ||
                                  (parent_len > root_len &&
                                   g_str_has_prefix(parent, git_root) &&
                                   parent[root_len] == '/');
            if (!under_root) {
                g_free(parent);
                break;
            }

            const gchar* existing = (const gchar*)g_hash_table_lookup(statuses, parent);
            if (is_modified) {
                /* Modification takes precedence over untracked */
                if (existing == NULL || strstr(existing, ">M<") == NULL) {
                    g_hash_table_replace(statuses, g_strdup(parent),
                                         g_strdup("<span foreground=\"#e2c08d\" weight=\"bold\">M</span>"));
                }
            } else if (is_untracked) {
                if (existing == NULL) {
                    g_hash_table_replace(statuses, g_strdup(parent),
                                         g_strdup("<span foreground=\"#73c991\" weight=\"bold\">U</span>"));
                }
            }

            if (g_strcmp0(parent, git_root) == 0) {
                g_free(parent);
                break;
            }

            gchar* next = g_path_get_dirname(parent);
            if (g_strcmp0(next, parent) == 0 || g_strcmp0(next, ".") == 0) {
                g_free(next);
                g_free(parent);
                break;
            }
            g_free(parent);
            parent = next;
        }
    }

    g_strfreev(lines);
    g_free(stdout_buf);
    g_free(git_root);

    return statuses;
}

/* Off-thread directory scan snapshot node */
typedef struct DirScanNode {
    gchar* dirname;
    gboolean unreadable;
    GArray* entries;      /* FileEntry elements */
    GPtrArray* children;  /* DirScanNode* per entry (NULL if not expanded dir) */
} DirScanNode;

static void dir_scan_node_free(gpointer data) {
    DirScanNode* node = (DirScanNode*)data;
    if (node == NULL) {
        return;
    }
    g_free(node->dirname);
    if (node->entries != NULL) {
        for (guint i = 0; i < node->entries->len; i++) {
            g_free(g_array_index(node->entries, FileEntry, i).name);
        }
        g_array_unref(node->entries);
    }
    if (node->children != NULL) {
        for (guint i = 0; i < node->children->len; i++) {
            dir_scan_node_free(g_ptr_array_index(node->children, i));
        }
        g_ptr_array_unref(node->children);
    }
    g_free(node);
}

/* Scan and sort one directory level (and any expanded subdirectories) off-thread */
static DirScanNode* scan_dir_node_recursive(const gchar* dirname,
                                            gboolean show_hidden,
                                            gint sort_mode,
                                            GHashTable* expanded_set,
                                            GCancellable* cancellable) {
    DirScanNode* node = g_new0(DirScanNode, 1);
    node->dirname = g_strdup(dirname);

    GDir* dir = g_dir_open(dirname, 0, NULL);
    if (dir == NULL) {
        node->unreadable = TRUE;
        return node;
    }

    node->entries = g_array_new(FALSE, FALSE, sizeof(FileEntry));
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (cancellable != NULL && g_cancellable_is_cancelled(cancellable)) {
            g_dir_close(dir);
            return node;
        }
        if (!show_hidden && name[0] == '.') {
            continue;
        }
        gchar* full = g_build_filename(dirname, name, NULL);
        struct stat st;
        gboolean ok = (g_stat(full, &st) == 0);
        FileEntry e;
        e.name = g_strdup(name);
        e.is_dir = ok ? S_ISDIR(st.st_mode) : FALSE;
        e.mtime = ok ? (gint64)st.st_mtime : 0;
        e.size = (ok && !e.is_dir) ? (gint64)st.st_size : 0;
        g_free(full);
        g_array_append_vals(node->entries, &e, 1);
    }
    g_dir_close(dir);

    g_array_sort_with_data(node->entries, compare_entries_with_data,
                           GINT_TO_POINTER(sort_mode));

    node->children = g_ptr_array_sized_new(node->entries->len);
    for (guint i = 0; i < node->entries->len; i++) {
        const FileEntry* e = &g_array_index(node->entries, FileEntry, i);
        DirScanNode* child_node = NULL;
        if (e->is_dir && expanded_set != NULL) {
            gchar* full = g_build_filename(dirname, e->name, NULL);
            if (g_hash_table_contains(expanded_set, full)) {
                child_node = scan_dir_node_recursive(full, show_hidden, sort_mode,
                                                     expanded_set, cancellable);
            }
            g_free(full);
        }
        g_ptr_array_add(node->children, child_node);
    }

    return node;
}

static gchar* get_bookmarks_file_path(void) {
    const gchar* cfg_dir = g_get_user_config_dir();
    gchar* dir = g_build_filename(cfg_dir, "explorateur_fichiers", NULL);
    g_mkdir_with_parents(dir, 0755);
    gchar* path = g_build_filename(dir, "bookmarks", NULL);
    g_free(dir);
    return path;
}

void bookmarks_save(AppData* app) {
    if (app == NULL || app->bookmarks == NULL) {
        return;
    }
    gchar* path = get_bookmarks_file_path();
    GString* content = g_string_new("");
    for (guint i = 0; i < app->bookmarks->len; i++) {
        const gchar* b = (const gchar*)g_ptr_array_index(app->bookmarks, i);
        if (b != NULL && b[0] != '\0') {
            g_string_append(content, b);
            g_string_append_c(content, '\n');
        }
    }
    GError* error = NULL;
    g_file_set_contents(path, content->str, (gssize)content->len, &error);
    if (error != NULL) {
        g_error_free(error);
    }
    g_string_free(content, TRUE);
    g_free(path);
}

void bookmarks_init(AppData* app) {
    if (app == NULL) {
        return;
    }
    app->bookmarks = g_ptr_array_new_with_free_func(g_free);
    app->show_bookmarks = FALSE;

    gchar* path = get_bookmarks_file_path();
    gchar* content = NULL;
    gsize length = 0;
    if (g_file_get_contents(path, &content, &length, NULL)) {
        gchar** lines = g_strsplit(content, "\n", -1);
        for (gint i = 0; lines != NULL && lines[i] != NULL; i++) {
            gchar* line = g_strstrip(lines[i]);
            if (line[0] != '\0') {
                gboolean exists = FALSE;
                for (guint j = 0; j < app->bookmarks->len; j++) {
                    if (g_strcmp0((const gchar*)g_ptr_array_index(app->bookmarks, j), line) == 0) {
                        exists = TRUE;
                        break;
                    }
                }
                if (!exists) {
                    g_ptr_array_add(app->bookmarks, g_strdup(line));
                }
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    g_free(path);
}

void bookmark_toggle_current(AppData* app) {
    if (app == NULL) {
        return;
    }

    /* Check if a bookmark row is currently selected in tree view */
    if (app->selection != NULL) {
        GtkTreeIter it;
        if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
            gchar* type = NULL;
            gchar* full = NULL;
            gtk_tree_model_get(app->model, &it,
                               COL_TYPE, &type,
                               COL_PATH, &full, -1);
            if (g_strcmp0(type, "bookmark") == 0 && full != NULL) {
                for (guint i = 0; i < app->bookmarks->len; i++) {
                    const gchar* b = (const gchar*)g_ptr_array_index(app->bookmarks, i);
                    if (g_strcmp0(b, full) == 0) {
                        g_ptr_array_remove_index(app->bookmarks, i);
                        bookmarks_save(app);
                        refresh_listing(app);
                        break;
                    }
                }
                g_free(type);
                g_free(full);
                return;
            }
            g_free(type);
            g_free(full);
        }
    }

    /* Otherwise, toggle bookmark for the current working directory */
    if (app->base_dir == NULL || app->base_dir[0] == '\0') {
        return;
    }

    gboolean found = FALSE;
    for (guint i = 0; i < app->bookmarks->len; i++) {
        const gchar* b = (const gchar*)g_ptr_array_index(app->bookmarks, i);
        if (g_strcmp0(b, app->base_dir) == 0) {
            g_ptr_array_remove_index(app->bookmarks, i);
            found = TRUE;
            break;
        }
    }

    if (!found) {
        g_ptr_array_add(app->bookmarks, g_strdup(app->base_dir));
    }

    bookmarks_save(app);
    update_path_label(app);
    if (app->show_bookmarks) {
        refresh_listing(app);
    }
}

void toggle_bookmarks_view(AppData* app) {
    if (app == NULL) {
        return;
    }
    if (app->left_vbox != NULL && !gtk_widget_get_visible(app->left_vbox)) {
        gtk_widget_set_visible(app->left_vbox, TRUE);
        workspace_apply_positions(app);
    }
    app->show_bookmarks = !app->show_bookmarks;
    update_path_label(app);
    refresh_listing(app);
    if (app->show_bookmarks) {
        select_first_row(app);
    }
}

void on_bookmarks_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_bookmarks_view((AppData*)data);
}

static void append_bookmarks_to_store(GtkTreeStore* store, AppData* app) {
    if (store == NULL || app == NULL || !app->show_bookmarks) {
        return;
    }

    if (app->bookmarks != NULL && app->bookmarks->len > 0) {
        const gchar* home = g_get_home_dir();
        gsize home_len = (home != NULL) ? strlen(home) : 0;

        for (guint i = 0; i < app->bookmarks->len; i++) {
            const gchar* bpath = (const gchar*)g_ptr_array_index(app->bookmarks, i);
            if (bpath == NULL || bpath[0] == '\0') {
                continue;
            }

            gchar* bname = g_path_get_basename(bpath);
            gchar* display_path = NULL;

            if (home_len > 1 && g_str_has_prefix(bpath, home) &&
                (bpath[home_len] == '\0' || bpath[home_len] == '/')) {
                display_path = g_strdup_printf("~%s", bpath + home_len);
            } else {
                display_path = g_strdup(bpath);
            }

            gchar* label = NULL;
            if (g_strcmp0(bname, "/") == 0 || g_strcmp0(bpath, home) == 0) {
                label = g_strdup_printf("🔖 %s", display_path);
            } else {
                label = g_strdup_printf("🔖 %s  (%s)", bname, display_path);
            }

            GtkTreeIter iter;
            gtk_tree_store_append(store, &iter, NULL);
            gtk_tree_store_set(store, &iter,
                               COL_NAME, bname,
                               COL_TYPE, "bookmark",
                               COL_LABEL, label,
                               COL_PATH, bpath,
                               COL_LOADED, TRUE,
                               COL_GIT_STATUS, "",
                               -1);
            g_free(label);
            g_free(display_path);
            g_free(bname);
        }
    } else {
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, "Bookmarks",
                           COL_TYPE, "-",
                           COL_LABEL, "🔖 (no bookmarks — Ctrl+D to add)",
                           COL_PATH, "",
                           COL_LOADED, TRUE,
                           COL_GIT_STATUS, "",
                           -1);
    }
}

/* Apply a pre-scanned DirScanNode tree into GtkTreeStore on the main thread */
static void apply_dir_node_to_store(GtkTreeStore* store, AppData* app, GtkTreeIter* parent,
                                    const DirScanNode* node) {
    if (parent == NULL && app != NULL && app->show_bookmarks) {
        append_bookmarks_to_store(store, app);
        return;
    }

    if (node == NULL) {
        return;
    }

    if (node->unreadable) {
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, "(cannot read directory)",
                           COL_TYPE, "-",
                           COL_LABEL, (parent == NULL) ? "📄 (cannot read directory)"
                                                       : "  (cannot read directory)",
                           COL_PATH, node->dirname,
                           COL_LOADED, TRUE,
                           COL_GIT_STATUS, "",
                           -1);
        return;
    }

    if (node->entries->len == 0 && parent != NULL) {
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, "(empty)",
                           COL_TYPE, "-",
                           COL_LABEL, "  (empty)",
                           COL_PATH, "",
                           COL_LOADED, TRUE,
                           COL_GIT_STATUS, "",
                           -1);
        return;
    }

    for (guint i = 0; i < node->entries->len; i++) {
        const FileEntry* e = &g_array_index(node->entries, FileEntry, i);
        const DirScanNode* child_node =
            (node->children != NULL && i < node->children->len)
                ? (const DirScanNode*)g_ptr_array_index(node->children, i)
                : NULL;
        gchar* full = g_build_filename(node->dirname, e->name, NULL);
        gchar* can_full = g_canonicalize_filename(full, NULL);
        g_free(full);
        const gchar* git_badge = "";
        if (app != NULL && app->git_statuses != NULL) {
            const gchar* b = (const gchar*)g_hash_table_lookup(app->git_statuses, can_full);
            if (b != NULL) {
                git_badge = b;
            }
        }

        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);

        if (e->is_dir) {
            gboolean is_expanded = (child_node != NULL);
            const gchar* icon = is_expanded ? "📂" : "📁";
            gchar* label = g_strconcat(icon, " ", e->name, NULL);
            gtk_tree_store_set(store, &iter,
                               COL_NAME, e->name,
                               COL_TYPE, "dir",
                               COL_LABEL, label,
                               COL_PATH, can_full,
                               COL_LOADED, is_expanded,
                               COL_GIT_STATUS, git_badge,
                               -1);
            g_free(label);

            if (is_expanded) {
                apply_dir_node_to_store(store, app, &iter, child_node);
            } else {
                /* Add dummy child so expander arrow is shown */
                GtkTreeIter dummy;
                gtk_tree_store_append(store, &dummy, &iter);
                gtk_tree_store_set(store, &dummy,
                                   COL_NAME, "",
                                   COL_TYPE, "-",
                                   COL_LABEL, "...",
                                   COL_PATH, "",
                                   COL_LOADED, TRUE,
                                   COL_GIT_STATUS, "",
                                   -1);
            }
        } else {
            gchar* label = g_strconcat(file_icon(e->name, FALSE), " ", e->name, NULL);
            gtk_tree_store_set(store, &iter,
                               COL_NAME, e->name,
                               COL_TYPE, "file",
                               COL_LABEL, label,
                               COL_PATH, can_full,
                               COL_LOADED, TRUE,
                               COL_GIT_STATUS, git_badge,
                               -1);
            g_free(label);
        }
        g_free(can_full);
    }
}

/* Populate one directory level under parent in the GtkTreeStore */
static void populate_level(GtkTreeStore* store, AppData* app, GtkTreeIter* parent,
                           const gchar* dirname, gboolean show_hidden, gint sort_mode) {
    GHashTable* exp = (app != NULL) ? app->expanded_paths : NULL;
    DirScanNode* node = scan_dir_node_recursive(dirname, show_hidden, sort_mode, exp, NULL);
    apply_dir_node_to_store(store, app, parent, node);
    dir_scan_node_free(node);
}

/* Populate the tree store with directory contents */
void populate_store(GtkTreeStore* store, AppData* app, const gchar* dirname,
                    gboolean show_hidden, gint sort_mode) {
    if (app != NULL) {
        if (app->git_statuses != NULL) {
            g_hash_table_destroy(app->git_statuses);
            app->git_statuses = NULL;
        }
        app->git_statuses = scan_git_statuses(dirname);
    }
    gtk_tree_store_clear(store);
    populate_level(store, app, NULL, dirname, show_hidden, sort_mode);
}

/* Recursively expand paths that were previously expanded */
void reexpand_paths_recursive(AppData* app, GtkTreeIter* parent) {
    GtkTreeIter child;
    if (!gtk_tree_model_iter_children(app->model, &child, parent)) {
        return;
    }
    do {
        gchar* type = NULL;
        gchar* path_val = NULL;
        gtk_tree_model_get(app->model, &child,
                           COL_TYPE, &type,
                           COL_PATH, &path_val, -1);
        if (g_strcmp0(type, "dir") == 0 && path_val != NULL) {
            if (g_hash_table_contains(app->expanded_paths, path_val)) {
                GtkTreePath* tp = gtk_tree_model_get_path(app->model, &child);
                gtk_tree_view_expand_row(app->tree_view, tp, FALSE);
                gtk_tree_path_free(tp);
                reexpand_paths_recursive(app, &child);
            }
        }
        g_free(type);
        g_free(path_val);
    } while (gtk_tree_model_iter_next(app->model, &child));
}

/* Lazy loading handler when a directory row is about to be expanded */
gboolean on_tree_test_expand_row(GtkTreeView* tree_view, GtkTreeIter* iter,
                                 GtkTreePath* path, gpointer user_data) {
    (void)tree_view;
    (void)path;
    AppData* app = (AppData*)user_data;
    GtkTreeStore* store = GTK_TREE_STORE(app->model);

    gboolean loaded = FALSE;
    gchar* full_path = NULL;
    gtk_tree_model_get(app->model, iter,
                       COL_LOADED, &loaded,
                       COL_PATH, &full_path, -1);

    if (!loaded && full_path != NULL) {
        GtkTreeIter child;
        if (gtk_tree_model_iter_children(app->model, &child, iter)) {
            gtk_tree_store_remove(store, &child);
        }
        populate_level(store, app, iter, full_path, app->show_hidden, app->sort_mode);
        gtk_tree_store_set(store, iter, COL_LOADED, TRUE, -1);
    }
    g_free(full_path);
    return FALSE; /* FALSE allows expansion */
}

/* Handle row expansion: record path and switch to open folder icon */
void on_tree_row_expanded(GtkTreeView* tree_view, GtkTreeIter* iter,
                          GtkTreePath* path, gpointer user_data) {
    (void)tree_view;
    (void)path;
    AppData* app = (AppData*)user_data;
    gchar* name = NULL;
    gchar* full_path = NULL;
    gtk_tree_model_get(app->model, iter,
                       COL_NAME, &name,
                       COL_PATH, &full_path, -1);
    if (full_path != NULL && app->expanded_paths != NULL) {
        g_hash_table_add(app->expanded_paths, g_strdup(full_path));
        sync_directory_monitors(app);
    }
    if (name != NULL) {
        gchar* label = g_strconcat("📂 ", name, NULL);
        gtk_tree_store_set(GTK_TREE_STORE(app->model), iter, COL_LABEL, label, -1);
        g_free(label);
    }
    if (!app->switching_ws && !app->dir_scan_pending) {
        workspace_save_current(app);
    }
    g_free(name);
    g_free(full_path);
}

/* Handle row collapse: remove path and switch back to closed folder icon */
void on_tree_row_collapsed(GtkTreeView* tree_view, GtkTreeIter* iter,
                           GtkTreePath* path, gpointer user_data) {
    (void)tree_view;
    (void)path;
    AppData* app = (AppData*)user_data;
    gchar* name = NULL;
    gchar* full_path = NULL;
    gtk_tree_model_get(app->model, iter,
                       COL_NAME, &name,
                       COL_PATH, &full_path, -1);
    if (full_path != NULL && app->expanded_paths != NULL) {
        gchar* prefix = g_strdup_printf("%s/", full_path);
        GHashTableIter hiter;
        gpointer hkey, hval;
        g_hash_table_iter_init(&hiter, app->expanded_paths);
        while (g_hash_table_iter_next(&hiter, &hkey, &hval)) {
            const gchar* p = (const gchar*)hkey;
            if (g_strcmp0(p, full_path) == 0 || g_str_has_prefix(p, prefix)) {
                g_hash_table_iter_remove(&hiter);
            }
        }
        g_free(prefix);
        sync_directory_monitors(app);
    }
    if (name != NULL) {
        gchar* label = g_strconcat("📁 ", name, NULL);
        gtk_tree_store_set(GTK_TREE_STORE(app->model), iter, COL_LABEL, label, -1);
        g_free(label);
    }

    /* Reset children to dummy row so future re-expansion reloads fresh from disk */
    GtkTreeIter child;
    while (gtk_tree_model_iter_children(app->model, &child, iter)) {
        gtk_tree_store_remove(GTK_TREE_STORE(app->model), &child);
    }
    GtkTreeIter dummy;
    gtk_tree_store_append(GTK_TREE_STORE(app->model), &dummy, iter);
    gtk_tree_store_set(GTK_TREE_STORE(app->model), &dummy,
                       COL_NAME, "",
                       COL_TYPE, "-",
                       COL_LABEL, "...",
                       COL_PATH, "",
                       COL_LOADED, TRUE,
                       COL_GIT_STATUS, "",
                       -1);
    gtk_tree_store_set(GTK_TREE_STORE(app->model), iter, COL_LOADED, FALSE, -1);

    if (!app->switching_ws && !app->dir_scan_pending) {
        workspace_save_current(app);
    }
    g_free(name);
    g_free(full_path);
}

static void on_breadcrumb_clicked(GtkButton* btn, gpointer data) {
    AppData* app = (AppData*)data;
    const gchar* target = (const gchar*)g_object_get_data(G_OBJECT(btn), "target-path");
    if (target != NULL) {
        if (app->show_bookmarks || g_strcmp0(target, app->base_dir) != 0) {
            app->show_bookmarks = FALSE;
            reload_directory(app, target);
        }
    }
}

void on_files_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app->left_vbox == NULL) {
        return;
    }
    gboolean cur = gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
    gboolean term_vis = (app->term_scroll != NULL) && gtk_widget_get_visible(app->term_scroll);
    if (cur && !preview_vis && !term_vis) {
        return; /* keep at least one panel visible */
    }
    workspace_save_current(app);
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->left_vbox, !cur);
    update_path_label(app);
    workspace_apply_positions(app);
    if (!cur) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    } else if (preview_vis) {
        gtk_widget_grab_focus(app->text_view);
    }
}

void on_preview_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app->viewer_box == NULL) {
        return;
    }
    gboolean cur = gtk_widget_get_visible(app->viewer_box);
    gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    gboolean term_vis = (app->term_scroll != NULL) && gtk_widget_get_visible(app->term_scroll);
    if (cur && !files_vis && !term_vis && !editor_vis) {
        return; /* keep at least one panel visible */
    }
    workspace_save_current(app);
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->viewer_box, !cur);
    update_path_label(app);
    workspace_apply_positions(app);
    if (!cur) {
        minimap_update_visibility(app);
        gtk_widget_grab_focus(app->text_view);
    } else if (files_vis) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    }
}

void on_editor_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    editor_toggle_dedicated((AppData*)data);
}

void on_term_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app->term_scroll == NULL) {
        return;
    }
    gboolean cur = gtk_widget_get_visible(app->term_scroll);
    gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    if (cur) {
        if (!files_vis && !preview_vis && !editor_vis) {
            return; /* keep at least one panel visible */
        }
        workspace_save_current(app);
        app->applying_layout = TRUE;
        gtk_widget_set_visible(app->term_scroll, FALSE);
        update_path_label(app);
        workspace_apply_positions(app);
        if (files_vis) {
            gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        } else if (preview_vis) {
            gtk_widget_grab_focus(app->text_view);
        }
    } else {
        workspace_save_current(app);
        app->applying_layout = TRUE;
        if (app->right_paned != NULL) {
            gtk_widget_set_visible(app->right_paned, TRUE);
        }
        gtk_widget_set_visible(app->term_scroll, TRUE);
#ifdef HAVE_VTE
        GPtrArray* panes = term_visible_panes(app);
        if (panes->len == 0) {
            term_spawn_pane(app, FALSE);
        } else {
            GtkWidget* target = (app->last_focused_vte != NULL)
                                    ? app->last_focused_vte
                                    : GTK_WIDGET(g_ptr_array_index(panes, 0));
            gtk_widget_grab_focus(target);
        }
        g_ptr_array_unref(panes);
#endif
        update_path_label(app);
        workspace_apply_positions(app);
    }
}

void on_sort_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    cycle_sort_mode((AppData*)data);
}

void on_hidden_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_hidden_files((AppData*)data);
}

void on_help_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_help_panel((AppData*)data);
}

typedef struct {
    gchar* label;
    gchar* full_path;
} BreadcrumbSeg;

/* Top bar: clickable path breadcrumbs + right-aligned sort/hidden/help pills */
void update_path_label(AppData* app) {
    static const gchar* sort_names[SORT_COUNT] = {"name", "date", "size", "ext"};
    static const gchar* sort_pill_labels[SORT_COUNT] = {
        "⇅ Sort: Name", "⇅ Sort: Date", "⇅ Sort: Size", "⇅ Sort: Ext"
    };

    if (app->path_label != NULL) {
        gchar* text = g_strdup_printf("Directory: %s   [sort: %s] [hidden: %s] [view: %s]",
                                       app->base_dir, sort_names[app->sort_mode],
                                       app->show_hidden ? "on" : "off",
                                       app->show_bookmarks ? "bookmarks" : "files");
        gtk_label_set_text(GTK_LABEL(app->path_label), text);
        g_free(text);
    }

    if (app->breadcrumb_box != NULL && app->base_dir != NULL) {
        const gchar* built_dir = (const gchar*)g_object_get_data(G_OBJECT(app->breadcrumb_box), "built-dir");
        if (g_strcmp0(built_dir, app->base_dir) != 0) {
            GtkWidget* child;
            while ((child = gtk_widget_get_first_child(app->breadcrumb_box)) != NULL) {
                gtk_box_remove(GTK_BOX(app->breadcrumb_box), child);
            }

            GArray* segs = g_array_new(FALSE, FALSE, sizeof(BreadcrumbSeg));
            const gchar* home = g_get_home_dir();
            gsize home_len = (home != NULL) ? strlen(home) : 0;
            gboolean under_home = (home_len > 1 &&
                                   g_str_has_prefix(app->base_dir, home) &&
                                   (app->base_dir[home_len] == '\0' ||
                                    app->base_dir[home_len] == '/'));

            gchar* accum = NULL;
            const gchar* rest = NULL;
            if (under_home) {
                BreadcrumbSeg root_seg = { g_strdup("🏠 ~"), g_strdup(home) };
                g_array_append_val(segs, root_seg);
                accum = g_strdup(home);
                rest = app->base_dir + home_len;
            } else {
                BreadcrumbSeg root_seg = { g_strdup("📁 /"), g_strdup("/") };
                g_array_append_val(segs, root_seg);
                accum = g_strdup("/");
                rest = app->base_dir;
            }

            gchar** parts = g_strsplit(rest, "/", -1);
            for (gint i = 0; parts != NULL && parts[i] != NULL; i++) {
                if (parts[i][0] == '\0') {
                    continue;
                }
                gchar* next_accum = g_build_filename(accum, parts[i], NULL);
                g_free(accum);
                accum = next_accum;

                BreadcrumbSeg seg = { g_strdup(parts[i]), g_strdup(accum) };
                g_array_append_val(segs, seg);
            }
            g_strfreev(parts);
            g_free(accum);

            guint total = segs->len;
            for (guint i = 0; i < total; i++) {
                /* Collapse middle segments if path is very deep (> 6 segments) */
                if (total > 6 && i == 1) {
                    GtkWidget* sep = gtk_label_new("›");
                    gtk_widget_add_css_class(sep, "breadcrumb-sep");
                    gtk_box_append(GTK_BOX(app->breadcrumb_box), sep);

                    GtkWidget* dots = gtk_label_new("…");
                    gtk_widget_add_css_class(dots, "breadcrumb-sep");
                    gtk_box_append(GTK_BOX(app->breadcrumb_box), dots);

                    i = total - 4;
                }

                if (i > 0) {
                    GtkWidget* sep = gtk_label_new("›");
                    gtk_widget_add_css_class(sep, "breadcrumb-sep");
                    gtk_box_append(GTK_BOX(app->breadcrumb_box), sep);
                }

                BreadcrumbSeg* s = &g_array_index(segs, BreadcrumbSeg, i);
                gboolean is_last = (i == total - 1);
                gchar* btn_text = (is_last && i > 0)
                                      ? g_strdup_printf("📂 %s", s->label)
                                      : g_strdup(s->label);
                GtkWidget* btn = gtk_button_new_with_label(btn_text);
                g_free(btn_text);
                gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
                gtk_widget_set_focusable(btn, FALSE);
                gtk_widget_add_css_class(btn, is_last ? "breadcrumb-current"
                                                      : "breadcrumb-item");
                gtk_widget_set_tooltip_text(btn, s->full_path);
                g_object_set_data_full(G_OBJECT(btn), "target-path",
                                       g_strdup(s->full_path), g_free);
                g_signal_connect(btn, "clicked",
                                 G_CALLBACK(on_breadcrumb_clicked), app);
                gtk_box_append(GTK_BOX(app->breadcrumb_box), btn);
            }

            for (guint i = 0; i < segs->len; i++) {
                BreadcrumbSeg* s = &g_array_index(segs, BreadcrumbSeg, i);
                g_free(s->label);
                g_free(s->full_path);
            }
            g_array_unref(segs);

            g_object_set_data_full(G_OBJECT(app->breadcrumb_box), "built-dir",
                                   g_strdup(app->base_dir), g_free);
        }
    }

    if (app->sort_btn != NULL) {
        gtk_button_set_label(GTK_BUTTON(app->sort_btn),
                             sort_pill_labels[app->sort_mode]);
    }

    if (app->hidden_btn != NULL) {
        gtk_button_set_label(GTK_BUTTON(app->hidden_btn),
                             app->show_hidden ? "👁 Hidden: On"
                                              : "👁 Hidden: Off");
        if (app->show_hidden) {
            gtk_widget_add_css_class(app->hidden_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->hidden_btn, "pill-active");
        }
    }

    if (app->bookmarks_btn != NULL) {
        gtk_button_set_label(GTK_BUTTON(app->bookmarks_btn),
                             app->show_bookmarks ? "🔖 Bookmarks: On"
                                                 : "🔖 Bookmarks: Off");
        if (app->show_bookmarks) {
            gtk_widget_add_css_class(app->bookmarks_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->bookmarks_btn, "pill-active");
        }
    }

    gboolean files_vis = (app->left_vbox == NULL) || gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box == NULL) || gtk_widget_get_visible(app->viewer_box);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    gboolean term_vis = (app->term_scroll != NULL) && gtk_widget_get_visible(app->term_scroll);

    if (app->editor_paned != NULL) {
        gtk_widget_set_visible(app->editor_paned, preview_vis || editor_vis);
    }
    if (app->right_paned != NULL) {
        gtk_widget_set_visible(app->right_paned, preview_vis || editor_vis || term_vis);
    }

    if (app->files_btn != NULL) {
        if (files_vis) {
            gtk_widget_add_css_class(app->files_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->files_btn, "pill-active");
        }
    }
    if (app->preview_btn != NULL) {
        if (preview_vis) {
            gtk_widget_add_css_class(app->preview_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->preview_btn, "pill-active");
        }
    }
    if (app->editor_btn != NULL) {
        if (editor_vis) {
            gtk_widget_add_css_class(app->editor_btn, "pill-active");
            gtk_button_set_label(GTK_BUTTON(app->editor_btn), "✏ Editor: Dedicated");
        } else {
            gtk_widget_remove_css_class(app->editor_btn, "pill-active");
            gtk_button_set_label(GTK_BUTTON(app->editor_btn), "✏ Editor: Docked");
        }
    }
    if (app->term_btn != NULL) {
        if (term_vis) {
            gtk_widget_add_css_class(app->term_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->term_btn, "pill-active");
        }
    }
    if (app->help_btn != NULL) {
        gboolean help_vis = (app->help_window != NULL) && gtk_widget_get_visible(app->help_window);
        if (help_vis) {
            gtk_widget_add_css_class(app->help_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(app->help_btn, "pill-active");
        }
    }

    workspace_save_current(app);
}

typedef struct {
    AppData* app;
    gchar* dirname;
    gboolean show_hidden;
    gint sort_mode;
    GHashTable* expanded_snapshot;
    gchar* select_path;
    gchar* select_name;
    DirScanNode* root_node;
    GHashTable* git_statuses;
} DirScanTaskData;

static void dir_scan_task_data_free(gpointer data) {
    DirScanTaskData* td = (DirScanTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->dirname);
    if (td->expanded_snapshot != NULL) {
        g_hash_table_destroy(td->expanded_snapshot);
    }
    if (td->git_statuses != NULL) {
        g_hash_table_destroy(td->git_statuses);
    }
    g_free(td->select_path);
    g_free(td->select_name);
    dir_scan_node_free(td->root_node);
    g_free(td);
}

static GHashTable* clone_string_set(GHashTable* src) {
    GHashTable* copy = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (src != NULL) {
        GHashTableIter iter;
        gpointer key;
        g_hash_table_iter_init(&iter, src);
        while (g_hash_table_iter_next(&iter, &key, NULL)) {
            g_hash_table_add(copy, g_strdup((const gchar*)key));
        }
    }
    return copy;
}

static void dir_scan_thread_func(GTask* task, gpointer source_object,
                                 gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    DirScanTaskData* td = (DirScanTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    td->root_node = scan_dir_node_recursive(td->dirname, td->show_hidden,
                                            td->sort_mode, td->expanded_snapshot,
                                            cancellable);

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    td->git_statuses = scan_git_statuses(td->dirname);

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
}

static void on_dir_scan_ready(GObject* source_object, GAsyncResult* res,
                              gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    DirScanTaskData* td = (DirScanTaskData*)g_task_get_task_data(task);

    if (td == NULL || g_cancellable_is_cancelled(cancellable) ||
        cancellable != td->app->dir_cancellable) {
        return;
    }

    AppData* app = td->app;
    app->dir_scan_pending = FALSE;

    if (td->root_node == NULL) {
        return;
    }

    GtkTreeStore* store = GTK_TREE_STORE(app->model);

    if (app->selection != NULL) {
        g_signal_handlers_block_by_func(app->selection,
                                        (gpointer)on_selection_changed, app);
    }

    if (app->git_statuses != NULL) {
        g_hash_table_destroy(app->git_statuses);
        app->git_statuses = NULL;
    }
    if (td->git_statuses != NULL) {
        app->git_statuses = td->git_statuses;
        td->git_statuses = NULL;
    }

    gtk_tree_store_clear(store);
    apply_dir_node_to_store(store, app, NULL, td->root_node);
    reexpand_paths_recursive(app, NULL);
    sync_directory_monitors(app);

    if (app->selection != NULL) {
        g_signal_handlers_unblock_by_func(app->selection,
                                          (gpointer)on_selection_changed, app);
    }

    if (td->select_path != NULL) {
        if (!select_by_path(app, td->select_path)) {
            select_first_row(app);
        }
    } else if (td->select_name != NULL) {
        if (!select_by_name(app, td->select_name)) {
            select_first_row(app);
        }
    } else {
        select_first_row(app);
    }

    update_path_label(app);
}

static void start_async_dir_scan(AppData* app, const gchar* dirname,
                                 const gchar* select_path, const gchar* select_name) {
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    app->dir_cancellable = g_cancellable_new();
    app->dir_scan_pending = TRUE;

    DirScanTaskData* td = g_new0(DirScanTaskData, 1);
    td->app = app;
    td->dirname = g_canonicalize_filename(dirname, NULL);
    td->show_hidden = app->show_hidden;
    td->sort_mode = app->sort_mode;
    td->expanded_snapshot = clone_string_set(app->expanded_paths);
    td->select_path = (select_path != NULL) ? g_canonicalize_filename(select_path, NULL) : NULL;
    td->select_name = g_strdup(select_name);

    GTask* task = g_task_new(NULL, app->dir_cancellable, on_dir_scan_ready, NULL);
    g_task_set_task_data(task, td, dir_scan_task_data_free);
    g_task_run_in_thread(task, dir_scan_thread_func);
    g_object_unref(task);
}

static void reload_directory_with_selection(AppData* app, const gchar* new_dir,
                                            const gchar* select_name) {
    if (!g_file_test(new_dir, G_FILE_TEST_IS_DIR)) {
        return;
    }

    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }

    g_free(app->base_dir);
    app->base_dir = g_canonicalize_filename(new_dir, NULL);
    app->dir_scan_pending = TRUE;
    if (!app->switching_ws && app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        g_free(app->workspaces[app->current_ws].selected_path);
        app->workspaces[app->current_ws].selected_path = NULL;
    }

    /* Clear prior search term */
    g_free(app->search_term);
    app->search_term = NULL;
    gtk_editable_set_text(GTK_EDITABLE(app->search_entry), "");

    /* Reset expanded paths when changing root directory */
    if (app->expanded_paths != NULL) {
        g_hash_table_remove_all(app->expanded_paths);
    }

    update_path_label(app);

    /* Clear right pane */
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
    set_view_monospace(app->text_view, FALSE);
    gtk_text_buffer_set_text(app->buffer, "", -1);
    app->viewer_match = -1;

    /* Stop monitoring previous viewed file */
    g_free(app->viewed_path);
    g_free(app->viewed_name);
    app->viewed_path = NULL;
    app->viewed_name = NULL;
    app->file_dirty = FALSE;
    app->dir_dirty = FALSE;
    monitor_file(app, NULL);
    monitor_directory(app);

    /* Scan directory asynchronously off the UI thread */
    start_async_dir_scan(app, new_dir, NULL, select_name);
}

/* Navigate to a new directory (Enter / Backspace) */
void reload_directory(AppData* app, const gchar* new_dir) {
    reload_directory_with_selection(app, new_dir, NULL);
}

/* Refresh the list while preserving selected entry and open expansions */
void refresh_listing_with_path(AppData* app, const gchar* select_path) {
    start_async_dir_scan(app, app->base_dir, select_path, NULL);
    update_path_label(app);
}

void refresh_listing(AppData* app) {
    gchar* cur_path = NULL;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        gtk_tree_model_get(app->model, &it, COL_PATH, &cur_path, -1);
    }

    refresh_listing_with_path(app, cur_path);
    g_free(cur_path);
}

/* Toggle visibility of hidden files (dot-prefixed) */
void toggle_hidden_files(AppData* app) {
    app->show_hidden = !app->show_hidden;
    refresh_listing(app);
}

/* Cycle sort mode: name -> date -> size -> extension */
void cycle_sort_mode(AppData* app) {
    app->sort_mode = (app->sort_mode + 1) % SORT_COUNT;
    refresh_listing(app);
}

/* Enter / double-click on a list entry: toggle directory expand/collapse or open editor tab */
void activate_selected_entry(AppData* app) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        return;
    }
    gchar* name = NULL;
    gchar* type = NULL;
    gchar* full = NULL;
    gtk_tree_model_get(app->model, &it,
                       COL_NAME, &name,
                       COL_TYPE, &type,
                       COL_PATH, &full, -1);

    if (g_strcmp0(type, "bookmark") == 0 && full != NULL) {
        app->show_bookmarks = FALSE;
        reload_directory(app, full);
    } else if (g_strcmp0(type, "dir") == 0) {
        GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
        if (gtk_tree_view_row_expanded(app->tree_view, path)) {
            gtk_tree_view_collapse_row(app->tree_view, path);
        } else {
            gtk_tree_view_expand_row(app->tree_view, path, FALSE);
        }
        gtk_tree_path_free(path);
    } else if (g_strcmp0(type, "file") == 0 && full != NULL) {
        if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
            workspace_save_current(app);
            app->applying_layout = TRUE;
            gtk_widget_set_visible(app->viewer_box, TRUE);
            update_path_label(app);
            workspace_apply_positions(app);
        }
        if (is_image_file(name) || is_pdf_file(name)) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        } else {
            editor_open_file(app, name, full);
        }
    }
    g_free(name);
    g_free(type);
    g_free(full);
}

/* Double click on a row: same action as Enter */
void on_row_activated(GtkTreeView* view, GtkTreePath* path,
                      GtkTreeViewColumn* column, gpointer data) {
    (void)view;
    (void)path;
    (void)column;
    activate_selected_entry((AppData*)data);
}

/* Treeview key press handler: j k gg G zz / n N BackSpace ? i o Ctrl+B Ctrl+F f m space l Right h Left */
gboolean on_tree_key_press(GtkEventControllerKey* controller, guint keyval,
                           guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    AppData* app = (AppData*)data;
    guint key = keyval;

    if (key != GDK_KEY_z && key != GDK_KEY_t && key != GDK_KEY_b) {
        app->pending_z = FALSE;
    }

    switch (key) {
    case GDK_KEY_j:
        app->pending_g = FALSE;
        select_next_row(app);
        return TRUE;
    case GDK_KEY_k:
        app->pending_g = FALSE;
        select_prev_row(app);
        return TRUE;
    case GDK_KEY_g:
        /* gg = jump to top */
        if (app->pending_g) {
            select_first_row(app);
            app->pending_g = FALSE;
        } else {
            app->pending_g = TRUE;
        }
        return TRUE;
    case GDK_KEY_G:
        app->pending_g = FALSE;
        select_last_row(app);
        return TRUE;
    case GDK_KEY_z:
        /* zz = center selected row vertically */
        app->pending_g = FALSE;
        if ((state & GDK_CONTROL_MASK) == 0) {
            if (app->pending_z) {
                center_selected_row(app);
                app->pending_z = FALSE;
            } else {
                app->pending_z = TRUE;
            }
            return TRUE;
        }
        app->pending_z = FALSE;
        return FALSE;
    case GDK_KEY_t:
        /* zt = scroll selected row to top of the panel */
        app->pending_g = FALSE;
        if ((state & GDK_CONTROL_MASK) == 0 && app->pending_z) {
            top_selected_row(app);
            app->pending_z = FALSE;
            return TRUE;
        }
        app->pending_z = FALSE;
        return FALSE;
    case GDK_KEY_l:
    case GDK_KEY_Right:
        app->pending_g = FALSE;
        {
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gchar* full = NULL;
                gtk_tree_model_get(app->model, &it,
                                   COL_TYPE, &type,
                                   COL_PATH, &full, -1);
                if (g_strcmp0(type, "bookmark") == 0 && full != NULL) {
                    reload_directory(app, full);
                } else if (g_strcmp0(type, "dir") == 0) {
                    GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
                    if (!gtk_tree_view_row_expanded(app->tree_view, p)) {
                        gtk_tree_view_expand_row(app->tree_view, p, FALSE);
                    } else {
                        /* Already expanded: step into first child */
                        GtkTreePath* child = gtk_tree_path_copy(p);
                        gtk_tree_path_down(child);
                        GtkTreeIter ch_it;
                        if (gtk_tree_model_get_iter(app->model, &ch_it, child)) {
                            select_path(app, child);
                        }
                        gtk_tree_path_free(child);
                    }
                    gtk_tree_path_free(p);
                }
                g_free(type);
                g_free(full);
            }
        }
        return TRUE;
    case GDK_KEY_h:
    case GDK_KEY_Left:
        app->pending_g = FALSE;
        {
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gtk_tree_model_get(app->model, &it, COL_TYPE, &type, -1);
                GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
                if (g_strcmp0(type, "dir") == 0 &&
                    gtk_tree_view_row_expanded(app->tree_view, p)) {
                    gtk_tree_view_collapse_row(app->tree_view, p);
                } else if (gtk_tree_path_get_depth(p) > 1) {
                    /* Jump to parent directory row */
                    gtk_tree_path_up(p);
                    select_path(app, p);
                }
                gtk_tree_path_free(p);
                g_free(type);
            }
        }
        return TRUE;
    case GDK_KEY_space:
        app->pending_g = FALSE;
        {
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gtk_tree_model_get(app->model, &it, COL_TYPE, &type, -1);
                if (g_strcmp0(type, "dir") == 0) {
                    GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
                    if (gtk_tree_view_row_expanded(app->tree_view, p)) {
                        gtk_tree_view_collapse_row(app->tree_view, p);
                    } else {
                        gtk_tree_view_expand_row(app->tree_view, p, FALSE);
                    }
                    gtk_tree_path_free(p);
                }
                g_free(type);
            }
        }
        return TRUE;
    case GDK_KEY_slash:
        app->pending_g = FALSE;
        show_search(app, FALSE);
        return TRUE;
    case GDK_KEY_n: /* next match */
        app->pending_g = FALSE;
        do_search(app, +1);
        return TRUE;
    case GDK_KEY_N: /* previous match */
        app->pending_g = FALSE;
        do_search(app, -1);
        return TRUE;
    case GDK_KEY_Return: /* Enter selected directory or open editor tab */
    case GDK_KEY_KP_Enter:
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
    case GDK_KEY_ISO_Enter:
        app->pending_g = FALSE;
        if ((state & GDK_CONTROL_MASK) != 0) {
            /* Ctrl+Enter: enter directory as root */
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gchar* full = NULL;
                gtk_tree_model_get(app->model, &it,
                                   COL_TYPE, &type,
                                   COL_PATH, &full, -1);
                if (g_strcmp0(type, "dir") == 0 && full != NULL) {
                    reload_directory(app, full);
                }
                g_free(type);
                g_free(full);
            }
        } else {
            activate_selected_entry(app);
        }
        return TRUE;
    case GDK_KEY_BackSpace: /* Parent directory (or return to files view from bookmarks) */
        app->pending_g = FALSE;
        if (app->show_bookmarks) {
            app->show_bookmarks = FALSE;
            refresh_listing(app);
            return TRUE;
        }
        {
            gchar* parent = g_path_get_dirname(app->base_dir);
            if (g_strcmp0(parent, app->base_dir) != 0) {
                /* Remember directory left to highlight it again in parent */
                gchar* came_from = g_path_get_basename(app->base_dir);
                reload_directory_with_selection(app, parent, came_from);
                g_free(came_from);
            }
            g_free(parent);
        }
        return TRUE;
    case GDK_KEY_question: /* ?: toggle internal keybindings help panel */
        if ((state & GDK_CONTROL_MASK) == 0) {
            app->pending_g = FALSE;
            toggle_help_panel(app);
            return TRUE;
        }
        app->pending_g = FALSE;
        return FALSE;
    case GDK_KEY_I: /* Toggle hidden files */
    case GDK_KEY_i:
        app->pending_g = FALSE;
        toggle_hidden_files(app);
        return TRUE;
    case GDK_KEY_o: /* Cycle sort: name -> date -> size -> ext */
        app->pending_g = FALSE;
        cycle_sort_mode(app);
        return TRUE;
    case GDK_KEY_b: /* Ctrl+B: page up; zb: scroll selected row to bottom of panel */
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            app->pending_z = FALSE;
            page_up_rows(app);
            return TRUE;
        }
        if (app->pending_z) {
            bottom_selected_row(app);
            app->pending_z = FALSE;
            return TRUE;
        }
        app->pending_z = FALSE;
        return FALSE;
    case GDK_KEY_d: /* Ctrl+D: bookmark current directory / remove selected bookmark */
    case GDK_KEY_D:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            bookmark_toggle_current(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_a: /* Ctrl+A: switch between files and bookmarks views in explorer panel */
    case GDK_KEY_A:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            toggle_bookmarks_view(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_f: /* Ctrl+F: page down; f alone: fullscreen */
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            page_down_rows(app);
            return TRUE;
        }
        toggle_fullscreen(app->window);
        return TRUE;
    case GDK_KEY_m:
    case GDK_KEY_M:
        app->pending_g = FALSE;
        if (app->viewed_name != NULL && is_renderable_file(app->viewed_name)) {
            app->render_raw = !app->render_raw;
            workspace_save_current(app);
            if (app->viewed_path != NULL) {
                load_file_content(app, app->viewed_name, app->viewed_path);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Escape:
        app->pending_g = FALSE;
        if (app->help_window != NULL && gtk_widget_get_visible(app->help_window)) {
            toggle_help_panel(app);
            return TRUE;
        }
        return FALSE;
    default:
        app->pending_g = FALSE; /* Any other key cancels gg and z sequences */
        app->pending_z = FALSE;
        break;
    }
    return FALSE;
}
