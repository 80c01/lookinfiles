#ifndef GIT_STATE_H
#define GIT_STATE_H

#include <glib.h>

/* Git state of the explorer's folder, read by a git scan: the badge of each
 * changed path, the repository root, and the upstream counts. One for the
 * shown workspace (AppData), one kept by each other workspace. Every
 * function takes NULL as "no state": no badges, no changes, no upstream. */
typedef struct GitState GitState;

/* Takes statuses (full_path -> badge markup, owned; NULL: none) */
GitState* git_state_new(GHashTable* statuses, const gchar* root, gboolean has_upstream,
                        guint ahead, guint behind);
void git_state_free(GitState* state);

/* Map of full_path -> badge markup, NULL if none */
GHashTable* git_state_badges(const GitState* state);
/* Badge markup of path, NULL if unchanged */
const gchar* git_state_badge(const GitState* state, const gchar* path);
/* Repository root the state was read for, NULL outside a repository */
const gchar* git_state_root(const GitState* state);
/* Uncommitted changes (some path has a badge) */
gboolean git_state_has_changes(const GitState* state);
/* HEAD has an upstream branch; then commits ahead of it and behind it */
gboolean git_state_has_upstream(const GitState* state);
guint git_state_ahead(const GitState* state);
guint git_state_behind(const GitState* state);

/* Same badges on the same paths */
gboolean git_state_same_badges(const GitState* a, const GitState* b);
/* Same changes flag and upstream counts */
gboolean git_state_same_counts(const GitState* a, const GitState* b);

#endif /* GIT_STATE_H */
