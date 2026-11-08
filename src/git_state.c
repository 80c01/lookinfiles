/* Git state of the explorer's folder (see git_state.h). */
#include "git_state.h"

struct GitState {
    GHashTable* statuses; /* full_path -> badge markup, or NULL */
    gchar* root;
    gboolean has_upstream;
    guint ahead;
    guint behind;
};

GitState* git_state_new(GHashTable* statuses, const gchar* root, gboolean has_upstream,
                        guint ahead, guint behind) {
    GitState* s = g_new0(GitState, 1);
    s->statuses = statuses;
    s->root = g_strdup(root);
    s->has_upstream = has_upstream;
    s->ahead = has_upstream ? ahead : 0;
    s->behind = has_upstream ? behind : 0;
    return s;
}

void git_state_free(GitState* state) {
    if (state == NULL) {
        return;
    }
    if (state->statuses != NULL) {
        g_hash_table_destroy(state->statuses);
    }
    g_free(state->root);
    g_free(state);
}

GHashTable* git_state_badges(const GitState* state) {
    return (state != NULL) ? state->statuses : NULL;
}

const gchar* git_state_badge(const GitState* state, const gchar* path) {
    if (state == NULL || state->statuses == NULL || path == NULL) {
        return NULL;
    }
    return g_hash_table_lookup(state->statuses, path);
}

const gchar* git_state_root(const GitState* state) {
    return (state != NULL) ? state->root : NULL;
}

gboolean git_state_has_changes(const GitState* state) {
    return state != NULL && state->statuses != NULL && g_hash_table_size(state->statuses) > 0;
}

gboolean git_state_has_upstream(const GitState* state) {
    return state != NULL && state->has_upstream;
}

guint git_state_ahead(const GitState* state) {
    return (state != NULL) ? state->ahead : 0;
}

guint git_state_behind(const GitState* state) {
    return (state != NULL) ? state->behind : 0;
}

gboolean git_state_same_badges(const GitState* a, const GitState* b) {
    GHashTable* ma = git_state_badges(a);
    GHashTable* mb = git_state_badges(b);
    guint na = (ma != NULL) ? g_hash_table_size(ma) : 0;
    guint nb = (mb != NULL) ? g_hash_table_size(mb) : 0;
    if (na != nb) {
        return FALSE;
    }
    if (na == 0) {
        return TRUE;
    }
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, ma);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (g_strcmp0(value, g_hash_table_lookup(mb, key)) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

gboolean git_state_same_counts(const GitState* a, const GitState* b) {
    return git_state_has_changes(a) == git_state_has_changes(b) &&
           git_state_has_upstream(a) == git_state_has_upstream(b) &&
           git_state_ahead(a) == git_state_ahead(b) &&
           git_state_behind(a) == git_state_behind(b);
}
