#ifndef GIT_REFS_H
#define GIT_REFS_H

#include <glib.h>

/* Kind of a git ref pointing to a commit, in display order */
typedef enum {
    GIT_REF_HEAD = 0, /* checked-out branch, or detached HEAD */
    GIT_REF_BRANCH,   /* local branch (refs/heads/*) */
    GIT_REF_TAG,      /* lightweight or annotated tag (refs/tags/*) */
    GIT_REF_REMOTE    /* remote-tracking branch (refs/remotes/*) */
} GitRefKind;

/* One branch or tag pointing to a commit (git_read_refs()) */
typedef struct {
    gchar* name;       /* short name: "main", "HEAD", "v0.20.0", "origin/main" */
    GitRefKind kind;
    gboolean detached; /* TRUE only when HEAD is detached */
} GitRefInfo;

void git_ref_info_free(gpointer info);

/* Label shown on a commit badge ("HEAD → main", "HEAD", "feat", "tag: v1.0",
 * "origin/main"). Caller frees. */
gchar* git_ref_display_name(const GitRefInfo* ref);

/* Map from full 40-char commit SHA (gchar*) to GPtrArray* of GitRefInfo*
 * (sorted by kind then name), for the repository whose work tree starts at
 * root. Empty when the repo has no refs; NULL when root is not a work tree.
 * Worker thread. Caller destroys with g_hash_table_unref(). */
GHashTable* git_read_refs(const gchar* root);

#endif /* GIT_REFS_H */
