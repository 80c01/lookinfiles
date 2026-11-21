/* The history of a repository, filtered (see git_log.h) */
#include "git/git_log.h"
#include "git/git_lib.h"
#include <string.h>

gchar** git_log_words(const gchar* text) {
    gchar* folded = g_utf8_casefold(text != NULL ? text : "", -1);
    gchar** parts = g_strsplit_set(folded, " \t", -1);
    g_free(folded);
    GPtrArray* kept = g_ptr_array_new();
    for (gchar** w = parts; *w != NULL; w++) {
        if ((*w)[0] != '\0') g_ptr_array_add(kept, g_strdup(*w));
    }
    g_strfreev(parts);
    if (kept->len == 0) {
        g_ptr_array_free(kept, TRUE);
        return NULL;
    }
    g_ptr_array_add(kept, NULL);
    return (gchar**)g_ptr_array_free(kept, FALSE);
}

/* Every word is in the message, the author's name or email, or the hash */
static gboolean commit_matches(git_commit* c, const gchar* hash, gchar** words) {
    if (words == NULL) return TRUE;
    const git_signature* author = git_commit_author(c);
    const gchar* message = git_commit_message(c);
    gchar* text = g_strdup_printf("%s\n%s <%s>\n%s", message != NULL ? message : "",
                                  author != NULL && author->name != NULL ? author->name : "",
                                  author != NULL && author->email != NULL ? author->email : "", hash);
    gchar* valid = g_utf8_make_valid(text, -1);
    gchar* folded = g_utf8_casefold(valid, -1);
    gboolean all = TRUE;
    for (gchar** w = words; *w != NULL && all; w++) all = strstr(folded, *w) != NULL;
    g_free(folded);
    g_free(valid);
    g_free(text);
    return all;
}

/* The object at path (a file or a folder) in the tree of c: FALSE when
 * there is none */
static gboolean path_oid(git_commit* c, const gchar* path, git_oid* oid) {
    git_tree* tree = NULL;
    if (git_commit_tree(&tree, c) != 0) return FALSE;
    git_tree_entry* entry = NULL;
    gboolean found = git_tree_entry_bypath(&entry, tree, path) == 0;
    if (found) git_oid_cpy(oid, git_tree_entry_id(entry));
    git_tree_entry_free(entry);
    git_tree_free(tree);
    return found;
}

/* c changed path, as `git log -- path` shows it: path differs from every
 * parent (a merge that took it from one parent is left out); a root commit
 * when it has path */
static gboolean commit_touches(git_commit* c, const gchar* path) {
    git_oid mine;
    gboolean has = path_oid(c, path, &mine);
    guint n = git_commit_parentcount(c);
    if (n == 0) return has;
    for (guint i = 0; i < n; i++) {
        git_commit* parent = NULL;
        if (git_commit_parent(&parent, c, i) != 0) continue;
        git_oid theirs;
        gboolean had = path_oid(parent, path, &theirs);
        git_commit_free(parent);
        if (had == has && (!has || git_oid_equal(&mine, &theirs))) return FALSE; /* same as this parent */
    }
    return TRUE;
}

/* Commits looked at between two checks of the cancellable */
#define LOG_CANCEL_STEP 256

GPtrArray* git_read_log(const gchar* root, guint skip, guint max, gchar** words, const gchar* path,
                        gboolean all_branches, GCancellable* cancellable, gboolean* more) {
    if (more != NULL) *more = FALSE;
    git_repository* repo = git_lib_open(root);
    if (repo == NULL) {
        return NULL;
    }
    GPtrArray* commits = g_ptr_array_new_with_free_func(git_commit_info_free);
    git_revwalk* walk = NULL;
    if (git_revwalk_new(&walk, repo) == 0) {
        /* Children before their parents (the graph needs it), newest first */
        git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
        gint64 now = g_get_real_time() / G_USEC_PER_SEC;
        git_oid oid;
        guint seen = 0;
        if (all_branches) { /* refs that are not commits are left out */
            git_revwalk_push_glob(walk, "heads");
            git_revwalk_push_glob(walk, "remotes");
            git_revwalk_push_glob(walk, "tags");
        }
        if (git_revwalk_push_head(walk) == 0) { /* fails without commits */
            while (git_revwalk_next(&oid, walk) == 0) {
                if (++seen % LOG_CANCEL_STEP == 0 && g_cancellable_is_cancelled(cancellable)) {
                    g_clear_pointer(&commits, g_ptr_array_unref);
                    break;
                }
                git_commit* c = NULL;
                if (git_commit_lookup(&c, repo, &oid) != 0) {
                    continue;
                }
                const gchar* hash = git_oid_tostr_s(&oid);
                if (!commit_matches(c, hash, words) || (path != NULL && !commit_touches(c, path))) {
                    git_commit_free(c);
                    continue;
                }
                if (skip > 0) { /* on an earlier page */
                    skip--;
                    git_commit_free(c);
                    continue;
                }
                if (commits->len == max) { /* one more is kept: there are more */
                    if (more != NULL) *more = TRUE;
                    git_commit_free(c);
                    break;
                }
                const gchar* summary = git_commit_summary(c);
                const git_signature* author = git_commit_author(c);
                GitCommitInfo* ci = g_new0(GitCommitInfo, 1);
                ci->hash = g_strdup(hash);
                ci->short_hash = git_lib_short_id((const git_object*)c);
                ci->subject = g_utf8_make_valid(summary != NULL ? summary : "", -1);
                const gchar* message = git_commit_message(c);
                ci->message = g_utf8_make_valid(message != NULL ? message : "", -1);
                g_strchomp(ci->message);
                ci->author = g_utf8_make_valid(author != NULL && author->name != NULL ? author->name : "", -1);
                ci->time = (gint64)git_commit_time(c);
                ci->date = git_relative_date(now - ci->time);
                guint n_parents = git_commit_parentcount(c);
                ci->parents = g_new0(gchar*, n_parents + 1);
                for (guint p = 0; p < n_parents; p++) {
                    gchar sha[GIT_OID_MAX_HEXSIZE + 1]; /* not git_oid_tostr_s(): its buffer holds hash */
                    ci->parents[p] = g_strdup(git_oid_tostr(sha, sizeof sha, git_commit_parent_id(c, p)));
                }
                g_ptr_array_add(commits, ci);
                git_commit_free(c);
            }
        }
        git_revwalk_free(walk);
    }
    git_repository_free(repo);
    return commits;
}
