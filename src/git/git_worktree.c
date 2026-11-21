/* The uncommitted changes of a work tree (see git_worktree.h) */
#include "git/git_worktree.h"
#include "core/i18n.h"
#include "git/git_lib.h"

void git_change_free(gpointer data) {
    GitChange* c = data;
    if (c == NULL) return;
    g_free(c->path);
    g_free(c->old_path);
    g_free(c);
}

const gchar* git_change_group_title(GitChangeGroup group) {
    switch (group) {
    /* TRANSLATORS: the groups of the Changes view of the Git tab */
    case GIT_CHANGE_STAGED: return _("Staged");
    case GIT_CHANGE_UNSTAGED: return _("Changes");
    case GIT_CHANGE_UNTRACKED: return _("Untracked");
    default: return "";
    }
}

static gchar status_letter(git_delta_t status) {
    switch (status) {
    case GIT_DELTA_ADDED:
    case GIT_DELTA_COPIED: return 'A';
    case GIT_DELTA_DELETED: return 'D';
    case GIT_DELTA_RENAMED: return 'R';
    case GIT_DELTA_TYPECHANGE: return 'T';
    case GIT_DELTA_UNTRACKED: return 'U';
    case GIT_DELTA_CONFLICTED: return '!';
    default: return 'M';
    }
}

/* The options of both diffs: type changes, git's indent heuristic, big
 * files as binary; with_untracked: the untracked files, one by one, with
 * their content (their lines count as added) */
static void diff_options(git_diff_options* opts, gboolean with_untracked) {
    git_diff_options_init(opts, GIT_DIFF_OPTIONS_VERSION);
    opts->flags |= GIT_DIFF_INCLUDE_TYPECHANGE | GIT_DIFF_INDENT_HEURISTIC;
    if (with_untracked) {
        opts->flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS | GIT_DIFF_SHOW_UNTRACKED_CONTENT;
    }
    opts->max_size = GIT_CHANGE_MAX_SIZE;
}

/* The tree of HEAD, NULL on a branch without commits yet */
static git_tree* head_tree(git_repository* repo) {
    git_object* obj = NULL;
    if (git_revparse_single(&obj, repo, "HEAD^{tree}") != 0) return NULL;
    return (git_tree*)obj;
}

/* Index against HEAD, renames found as `git status` finds them */
static git_diff* staged_diff(git_repository* repo, git_index* index) {
    git_diff_options opts;
    diff_options(&opts, FALSE);
    git_tree* tree = head_tree(repo);
    git_diff* diff = NULL;
    if (git_diff_tree_to_index(&diff, repo, tree, index, &opts) == 0) {
        git_diff_find_options find;
        git_diff_find_options_init(&find, GIT_DIFF_FIND_OPTIONS_VERSION);
        find.flags = GIT_DIFF_FIND_RENAMES;
        git_diff_find_similar(diff, &find);
    }
    git_tree_free(tree);
    return diff;
}

/* Work tree against the index; path: only that file (NULL: all) */
static git_diff* workdir_diff(git_repository* repo, git_index* index, const gchar* path) {
    git_diff_options opts;
    diff_options(&opts, TRUE);
    gchar* spec[1] = { (gchar*)path };
    if (path != NULL) {
        opts.pathspec.strings = spec;
        opts.pathspec.count = 1;
        opts.flags |= GIT_DIFF_DISABLE_PATHSPEC_MATCH;
    }
    git_diff* diff = NULL;
    if (git_diff_index_to_workdir(&diff, repo, index, &opts) != 0) return NULL;
    return diff;
}

/* The change of delta i of diff, with its line counts */
static GitChange* change_of(git_diff* diff, gsize i, GitChangeGroup group) {
    const git_diff_delta* d = git_diff_get_delta(diff, i);
    GitChange* c = g_new0(GitChange, 1);
    const gchar* path = d->new_file.path != NULL ? d->new_file.path : d->old_file.path;
    c->path = g_strdup(path != NULL ? path : "");
    if (d->status == GIT_DELTA_RENAMED && g_strcmp0(d->old_file.path, path) != 0) {
        c->old_path = g_strdup(d->old_file.path);
    }
    c->group = group;
    c->status = status_letter(d->status);
    c->added = -1;
    c->removed = -1;
    git_patch* patch = NULL;
    if (git_patch_from_diff(&patch, diff, i) == 0 && patch != NULL) {
        const git_diff_delta* pd = git_patch_get_delta(patch);
        if ((pd->flags & GIT_DIFF_FLAG_BINARY) == 0) {
            gsize added = 0;
            gsize removed = 0;
            git_patch_line_stats(NULL, &added, &removed, patch);
            c->added = (gint)MIN(added, (gsize)G_MAXINT);
            c->removed = (gint)MIN(removed, (gsize)G_MAXINT);
        }
    }
    git_patch_free(patch);
    return c;
}

/* Add the deltas of diff that belong to group (untracked: only the
 * untracked files, else all but those) to out, up to max. FALSE when
 * cancelled. */
static gboolean collect(GPtrArray* out, git_diff* diff, GitChangeGroup group, guint max, gboolean* more,
                        GCancellable* cancellable) {
    if (diff == NULL) return TRUE;
    gsize n = git_diff_num_deltas(diff);
    for (gsize i = 0; i < n; i++) {
        const git_diff_delta* d = git_diff_get_delta(diff, i);
        if (d->status == GIT_DELTA_UNMODIFIED || d->status == GIT_DELTA_IGNORED) continue;
        gboolean untracked = d->status == GIT_DELTA_UNTRACKED;
        if (untracked != (group == GIT_CHANGE_UNTRACKED)) continue;
        if (g_cancellable_is_cancelled(cancellable)) return FALSE;
        if (max > 0 && out->len >= max) {
            *more = TRUE;
            return TRUE;
        }
        g_ptr_array_add(out, change_of(diff, i, group));
    }
    return TRUE;
}

GPtrArray* git_read_changes(const gchar* root, guint max, GCancellable* cancellable, gboolean* more) {
    gboolean left_out = FALSE;
    if (more != NULL) *more = FALSE;
    git_repository* repo = git_lib_open(root);
    if (repo == NULL) return NULL;
    git_index* index = NULL;
    if (git_repository_index(&index, repo) != 0) {
        git_repository_free(repo);
        return NULL;
    }
    GPtrArray* out = g_ptr_array_new_with_free_func(git_change_free);
    git_diff* staged = staged_diff(repo, index);
    git_diff* work = g_cancellable_is_cancelled(cancellable) ? NULL : workdir_diff(repo, index, NULL);
    gboolean ok = !g_cancellable_is_cancelled(cancellable) &&
                  collect(out, staged, GIT_CHANGE_STAGED, max, &left_out, cancellable) &&
                  collect(out, work, GIT_CHANGE_UNSTAGED, max, &left_out, cancellable) &&
                  collect(out, work, GIT_CHANGE_UNTRACKED, max, &left_out, cancellable);
    git_diff_free(work);
    git_diff_free(staged);
    git_index_free(index);
    git_repository_free(repo);
    if (!ok) {
        g_ptr_array_unref(out);
        return NULL;
    }
    if (more != NULL) *more = left_out;
    return out;
}

/* The patch of the delta of diff whose new (or old) path is path, "" if
 * there is none */
static gchar* patch_of(git_diff* diff, const gchar* path) {
    GString* out = g_string_new(NULL);
    gsize n = git_diff_num_deltas(diff);
    for (gsize i = 0; i < n; i++) {
        const git_diff_delta* d = git_diff_get_delta(diff, i);
        const gchar* name = d->new_file.path != NULL ? d->new_file.path : d->old_file.path;
        if (g_strcmp0(name, path) != 0) continue;
        git_patch* patch = NULL;
        if (git_patch_from_diff(&patch, diff, i) == 0 && patch != NULL) {
            git_patch_print(patch, git_lib_patch_line, out);
        }
        git_patch_free(patch);
        break;
    }
    gchar* text = g_utf8_make_valid(out->str, (gssize)out->len);
    g_string_free(out, TRUE);
    return text;
}

gchar* git_change_patch(const gchar* root, const gchar* path, GitChangeGroup group) {
    if (path == NULL || path[0] == '\0') return NULL;
    git_repository* repo = git_lib_open(root);
    if (repo == NULL) return NULL;
    git_index* index = NULL;
    gchar* text = NULL;
    if (git_repository_index(&index, repo) == 0) {
        /* Staged: the whole index, for the renames (a path filter would keep
         * only the new name, an added file); the work tree: that file only */
        git_diff* diff = group == GIT_CHANGE_STAGED ? staged_diff(repo, index) : workdir_diff(repo, index, path);
        if (diff != NULL) text = patch_of(diff, path);
        git_diff_free(diff);
    }
    git_index_free(index);
    git_repository_free(repo);
    return text;
}
