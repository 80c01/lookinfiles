/* What a repository is set up with (see git_repo_info.h) */
#include "git/git_repo_info.h"
#include "core/i18n.h"
#include "git/git_lib.h"
#include <string.h>

#define MASK "•••"
/* A user name in a URL this long is taken for a token */
#define TOKEN_MIN_LEN 20

const gchar* git_config_level_title(GitConfigLevel level) {
    switch (level) {
    /* TRANSLATORS: where a Git setting comes from */
    case GIT_CONFIG_SYSTEM: return C_("git config level", "System");
    case GIT_CONFIG_GLOBAL: return C_("git config level", "Global");
    case GIT_CONFIG_REPOSITORY: return C_("git config level", "Repository");
    case GIT_CONFIG_WORKTREE: return C_("git config level", "Worktree");
    default: return C_("git config level", "Other");
    }
}

/* libgit2's level as ours (GIT_CONFIG_LEVEL_WORKTREE is not in libgit2 1.1:
 * a level past the repository's that is not the program's) */
static GitConfigLevel level_of(git_config_level_t level) {
    if (level == GIT_CONFIG_LEVEL_APP) return GIT_CONFIG_OTHER;
    if (level <= GIT_CONFIG_LEVEL_SYSTEM) return GIT_CONFIG_SYSTEM;
    if (level <= GIT_CONFIG_LEVEL_GLOBAL) return GIT_CONFIG_GLOBAL;
    if (level == GIT_CONFIG_LEVEL_LOCAL) return GIT_CONFIG_REPOSITORY;
    return level > GIT_CONFIG_LEVEL_LOCAL ? GIT_CONFIG_WORKTREE : GIT_CONFIG_OTHER;
}

gchar* git_mask_url(const gchar* url) {
    if (url == NULL) return g_strdup("");
    const gchar* scheme_end = strstr(url, "://");
    if (scheme_end == NULL) return g_strdup(url);
    const gchar* auth = scheme_end + 3;
    gsize auth_len = strcspn(auth, "/?#");
    const gchar* at = g_strrstr_len(auth, (gssize)auth_len, "@");
    if (at == NULL) return g_strdup(url);
    const gchar* colon = memchr(auth, ':', (gsize)(at - auth));
    GString* out = g_string_new_len(url, auth - url);
    if (colon != NULL) {
        g_string_append_len(out, auth, colon - auth);
        g_string_append(out, ":" MASK);
    } else if (at - auth >= TOKEN_MIN_LEN) {
        g_string_append(out, MASK);
    } else {
        g_string_append_len(out, auth, at - auth);
    }
    g_string_append(out, at);
    return g_string_free(out, FALSE);
}

gboolean git_config_key_secret(const gchar* key) {
    if (key == NULL) return FALSE;
    gchar* lower = g_ascii_strdown(key, -1);
    static const gchar* const words[] = { "password", "passwd", "token", "secret", "extraheader", NULL };
    gboolean secret = FALSE;
    for (gsize i = 0; words[i] != NULL && !secret; i++) secret = strstr(lower, words[i]) != NULL;
    g_free(lower);
    return secret;
}

static void config_item_free(gpointer data) {
    GitConfigItem* it = data;
    g_free(it->key);
    g_free(it->value);
    g_free(it);
}

static void remote_free(gpointer data) {
    GitRemoteInfo* r = data;
    g_free(r->name);
    g_free(r->url);
    g_free(r->push_url);
    g_free(r);
}

static void branch_free(gpointer data) {
    GitBranchState* b = data;
    g_free(b->name);
    g_free(b->upstream);
    g_free(b);
}

void git_repo_info_free(GitRepoInfo* info) {
    if (info == NULL) return;
    g_free(info->user_name);
    g_free(info->user_email);
    g_free(info->signing_key);
    g_ptr_array_unref(info->remotes);
    g_ptr_array_unref(info->branches);
    g_ptr_array_unref(info->config);
    g_free(info->repo_config);
    g_free(info->global_config);
    g_free(info);
}

/* The value shown for key: hidden when secret, URLs masked */
static gchar* shown_value(const gchar* key, const gchar* value) {
    if (git_config_key_secret(key)) return g_strdup(MASK);
    gchar* valid = g_utf8_make_valid(value != NULL ? value : "", -1);
    gchar* masked = git_mask_url(valid);
    g_free(valid);
    return masked;
}

/* An effective setting and its level; NULL when not set */
static gchar* effective(git_config* cfg, const gchar* key, GitConfigLevel* level) {
    git_config_entry* entry = NULL;
    if (git_config_get_entry(&entry, cfg, key) != 0 || entry == NULL) return NULL;
    gchar* value = shown_value(key, entry->value);
    if (level != NULL) *level = level_of(entry->level);
    git_config_entry_free(entry);
    return value;
}

static gint by_level(gconstpointer pa, gconstpointer pb) {
    const GitConfigItem* a = *(GitConfigItem* const*)pa;
    const GitConfigItem* b = *(GitConfigItem* const*)pb;
    return (gint)a->level - (gint)b->level;
}

/* Every entry with its level; an entry is overridden when the same key is
 * set at a closer level (several values at one level: a list, all kept) */
static GPtrArray* read_config(git_config* cfg) {
    GPtrArray* items = g_ptr_array_new_with_free_func(config_item_free);
    git_config_iterator* it = NULL;
    if (git_config_iterator_new(&it, cfg) != 0) return items;
    git_config_entry* entry = NULL;
    GHashTable* closest = g_hash_table_new(g_str_hash, g_str_equal); /* key -> closest level + 1 */
    while (git_config_next(&entry, it) == 0) {
        GitConfigItem* item = g_new0(GitConfigItem, 1);
        item->key = g_utf8_make_valid(entry->name, -1);
        item->value = shown_value(entry->name, entry->value);
        item->level = level_of(entry->level);
        g_ptr_array_add(items, item);
    }
    git_config_iterator_free(it);
    g_ptr_array_sort(items, by_level); /* stable: the order of each file kept */
    for (guint i = 0; i < items->len; i++) {
        GitConfigItem* item = g_ptr_array_index(items, i);
        guint level = GPOINTER_TO_UINT(g_hash_table_lookup(closest, item->key));
        if ((guint)item->level + 1 > level) g_hash_table_insert(closest, item->key, GUINT_TO_POINTER(item->level + 1));
    }
    for (guint i = 0; i < items->len; i++) {
        GitConfigItem* item = g_ptr_array_index(items, i);
        item->overridden = GPOINTER_TO_UINT(g_hash_table_lookup(closest, item->key)) > (guint)item->level + 1;
    }
    g_hash_table_unref(closest);
    return items;
}

static gint remote_by_name(gconstpointer pa, gconstpointer pb) {
    return g_strcmp0((*(GitRemoteInfo* const*)pa)->name, (*(GitRemoteInfo* const*)pb)->name);
}

static GPtrArray* read_remotes(git_repository* repo) {
    GPtrArray* remotes = g_ptr_array_new_with_free_func(remote_free);
    git_strarray names = { 0 };
    if (git_remote_list(&names, repo) != 0) return remotes;
    for (gsize i = 0; i < names.count; i++) {
        git_remote* remote = NULL;
        if (git_remote_lookup(&remote, repo, names.strings[i]) != 0) continue;
        GitRemoteInfo* r = g_new0(GitRemoteInfo, 1);
        r->name = g_utf8_make_valid(names.strings[i], -1);
        r->url = git_mask_url(git_remote_url(remote));
        const gchar* push = git_remote_pushurl(remote);
        if (push != NULL && g_strcmp0(push, git_remote_url(remote)) != 0) r->push_url = git_mask_url(push);
        g_ptr_array_add(remotes, r);
        git_remote_free(remote);
    }
    git_strarray_dispose(&names);
    g_ptr_array_sort(remotes, remote_by_name);
    return remotes;
}

static gint branch_by_name(gconstpointer pa, gconstpointer pb) {
    return g_strcmp0((*(GitBranchState* const*)pa)->name, (*(GitBranchState* const*)pb)->name);
}

/* The upstream of branch ref: its name, gone when set but missing, and the
 * commits on either side */
static void read_upstream(git_repository* repo, git_reference* ref, GitBranchState* b) {
    git_buf name = { 0 };
    if (git_branch_upstream_name(&name, repo, git_reference_name(ref)) != 0) return; /* none set */
    const gchar* full = name.ptr;
    const gchar* shortened = g_str_has_prefix(full, "refs/remotes/") ? full + strlen("refs/remotes/")
                             : g_str_has_prefix(full, "refs/heads/") ? full + strlen("refs/heads/")
                                                                       : full;
    b->upstream = g_utf8_make_valid(shortened, -1);
    git_reference* up = NULL;
    git_oid mine, theirs;
    if (git_reference_lookup(&up, repo, full) != 0) {
        b->gone = TRUE;
    } else if (git_reference_name_to_id(&mine, repo, git_reference_name(ref)) == 0 &&
               git_reference_name_to_id(&theirs, repo, full) == 0) {
        size_t ahead = 0, behind = 0;
        if (git_graph_ahead_behind(&ahead, &behind, repo, &mine, &theirs) == 0) {
            b->ahead = (guint)ahead;
            b->behind = (guint)behind;
        }
    }
    git_reference_free(up);
    git_buf_dispose(&name);
}

static GPtrArray* read_branches(git_repository* repo) {
    GPtrArray* branches = g_ptr_array_new_with_free_func(branch_free);
    git_branch_iterator* it = NULL;
    if (git_branch_iterator_new(&it, repo, GIT_BRANCH_LOCAL) != 0) return branches;
    git_reference* ref = NULL;
    git_branch_t type;
    while (git_branch_next(&ref, &type, it) == 0) {
        const char* name = NULL;
        if (git_branch_name(&name, ref) == 0 && name != NULL) {
            GitBranchState* b = g_new0(GitBranchState, 1);
            b->name = g_utf8_make_valid(name, -1);
            b->is_head = git_branch_is_head(ref) == 1;
            read_upstream(repo, ref, b);
            g_ptr_array_add(branches, b);
        }
        git_reference_free(ref);
    }
    git_branch_iterator_free(it);
    g_ptr_array_sort(branches, branch_by_name);
    return branches;
}

/* The global config file in use: ~/.gitconfig, else the XDG one */
static gchar* global_config_path(void) {
    git_buf buf = { 0 };
    gchar* path = NULL;
    if (git_config_find_global(&buf) == 0 && buf.ptr != NULL) {
        path = g_strdup(buf.ptr);
    } else if (git_config_find_xdg(&buf) == 0 && buf.ptr != NULL) {
        path = g_strdup(buf.ptr);
    }
    git_buf_dispose(&buf);
    return path;
}

GitRepoInfo* git_repo_info_read(const gchar* root) {
    git_repository* repo = git_lib_open(root);
    if (repo == NULL) return NULL;
    GitRepoInfo* info = g_new0(GitRepoInfo, 1);
    git_config* cfg = NULL;
    if (git_repository_config_snapshot(&cfg, repo) == 0) {
        info->user_name = effective(cfg, "user.name", &info->user_name_level);
        info->user_email = effective(cfg, "user.email", &info->user_email_level);
        int sign = 0;
        info->signs = git_config_get_bool(&sign, cfg, "commit.gpgsign") == 0 && sign;
        info->signing_key = effective(cfg, "user.signingkey", NULL);
        info->config = read_config(cfg);
        git_config_free(cfg);
    } else {
        info->config = g_ptr_array_new_with_free_func(config_item_free);
    }
    info->remotes = read_remotes(repo);
    info->branches = read_branches(repo);
    info->repo_config = g_build_filename(git_repository_path(repo), "config", NULL);
    info->global_config = global_config_path();
    git_repository_free(repo);
    return info;
}
