/* Read branch and tag decorations of a git repository through libgit2.
 * Pure GLib + libgit2 (no AppData, no widgets); safe on worker threads. */
#include "git/git_refs.h"

#include <git2.h>
#include <string.h>

static void libgit2_ready(void) {
    static gsize done = 0;
    if (g_once_init_enter(&done)) {
        git_libgit2_init();
        g_once_init_leave(&done, 1);
    }
}

void git_ref_info_free(gpointer info) {
    GitRefInfo* r = info;
    if (r == NULL) {
        return;
    }
    g_free(r->name);
    g_free(r);
}

gchar* git_ref_display_name(const GitRefInfo* ref) {
    if (ref == NULL || ref->name == NULL) {
        return g_strdup("");
    }
    switch (ref->kind) {
    case GIT_REF_HEAD:
        if (ref->detached) {
            return g_strdup("HEAD");
        }
        return g_strdup_printf("HEAD \xe2\x86\x92 %s", ref->name);
    case GIT_REF_TAG:
        return g_strdup_printf("tag: %s", ref->name);
    case GIT_REF_BRANCH:
    case GIT_REF_REMOTE:
    default:
        return g_strdup(ref->name);
    }
}

static void add_ref(GHashTable* map, const gchar* sha, const gchar* name,
                    GitRefKind kind, gboolean detached) {
    GPtrArray* list = g_hash_table_lookup(map, sha);
    if (list == NULL) {
        list = g_ptr_array_new_with_free_func(git_ref_info_free);
        g_hash_table_insert(map, g_strdup(sha), list);
    }
    GitRefInfo* info = g_new0(GitRefInfo, 1);
    info->name = g_utf8_make_valid(name != NULL ? name : "", -1);
    info->kind = kind;
    info->detached = detached;
    g_ptr_array_add(list, info);
}

static gint cmp_refs(gconstpointer a, gconstpointer b) {
    const GitRefInfo* ra = *(const GitRefInfo* const*)a;
    const GitRefInfo* rb = *(const GitRefInfo* const*)b;
    if (ra->kind != rb->kind) {
        return (gint)ra->kind - (gint)rb->kind;
    }
    return g_strcmp0(ra->name, rb->name);
}

GHashTable* git_read_refs(const gchar* root) {
    if (root == NULL) {
        return NULL;
    }
    libgit2_ready();
    git_repository* repo = NULL;
    if (git_repository_open_ext(&repo, root, GIT_REPOSITORY_OPEN_NO_SEARCH, NULL) != 0) {
        return NULL;
    }
    if (git_repository_is_bare(repo)) {
        git_repository_free(repo);
        return NULL;
    }

    GHashTable* map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                            (GDestroyNotify)g_ptr_array_unref);

    /* Record checked-out branch name, or detached HEAD directly */
    gchar* head_ref_name = NULL;
    git_reference* head = NULL;
    if (git_repository_head(&head, repo) == 0) {
        if (git_repository_head_detached(repo) == 1) {
            git_object* obj = NULL;
            if (git_reference_peel(&obj, head, GIT_OBJECT_COMMIT) == 0) {
                add_ref(map, git_oid_tostr_s(git_object_id(obj)), "HEAD", GIT_REF_HEAD, TRUE);
                git_object_free(obj);
            }
        } else if (git_reference_is_branch(head)) {
            head_ref_name = g_strdup(git_reference_name(head));
        }
        git_reference_free(head);
    }

    git_reference_iterator* iter = NULL;
    if (git_reference_iterator_new(&iter, repo) == 0) {
        git_reference* ref = NULL;
        while (git_reference_next(&ref, iter) == 0) {
            if (git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC) {
                git_reference_free(ref);
                continue;
            }
            const gchar* full = git_reference_name(ref);
            const gchar* name = NULL;
            GitRefKind kind = GIT_REF_BRANCH;

            if (g_str_has_prefix(full, "refs/heads/")) {
                name = full + strlen("refs/heads/");
                kind = (g_strcmp0(full, head_ref_name) == 0) ? GIT_REF_HEAD : GIT_REF_BRANCH;
            } else if (g_str_has_prefix(full, "refs/tags/")) {
                name = full + strlen("refs/tags/");
                kind = GIT_REF_TAG;
            } else if (g_str_has_prefix(full, "refs/remotes/")) {
                name = full + strlen("refs/remotes/");
                kind = GIT_REF_REMOTE;
            }

            if (name != NULL && name[0] != '\0') {
                git_object* obj = NULL;
                if (git_reference_peel(&obj, ref, GIT_OBJECT_COMMIT) == 0) {
                    add_ref(map, git_oid_tostr_s(git_object_id(obj)), name, kind, FALSE);
                    git_object_free(obj);
                }
            }
            git_reference_free(ref);
        }
        git_reference_iterator_free(iter);
    }

    GHashTableIter hiter;
    gpointer value = NULL;
    g_hash_table_iter_init(&hiter, map);
    while (g_hash_table_iter_next(&hiter, NULL, &value)) {
        g_ptr_array_sort((GPtrArray*)value, cmp_refs);
    }

    g_free(head_ref_name);
    git_repository_free(repo);
    return map;
}
