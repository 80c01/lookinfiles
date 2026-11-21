#ifndef GIT_REPO_INFO_H
#define GIT_REPO_INFO_H

/* What a repository is set up with, read through libgit2 (no git program):
 * the identity commits are made with, the remotes, the local branches and
 * their upstreams, the whole configuration with the level of each entry.
 * Passwords and tokens are masked (git_mask_url(), git_config_key_secret()).
 * No AppData and no widgets; git_repo_info_read() on a worker thread. */
#include <glib.h>

typedef enum {
    GIT_CONFIG_SYSTEM,     /* /etc/gitconfig (and the program data one) */
    GIT_CONFIG_GLOBAL,     /* ~/.gitconfig, ~/.config/git/config */
    GIT_CONFIG_REPOSITORY, /* .git/config */
    GIT_CONFIG_WORKTREE,   /* .git/config.worktree */
    GIT_CONFIG_OTHER
} GitConfigLevel;

/* "System", "Global", "Repository", "Worktree", "Other" (translated) */
const gchar* git_config_level_title(GitConfigLevel level);

typedef struct {
    gchar* key;          /* "user.email", "remote.origin.url"... */
    gchar* value;        /* masked when secret */
    GitConfigLevel level;
    gboolean overridden; /* set again at a closer level: not in effect */
} GitConfigItem;

typedef struct {
    gchar* name;
    gchar* url;          /* fetch URL (masked), "" when none */
    gchar* push_url;     /* push URL (masked) when it differs, else NULL */
} GitRemoteInfo;

typedef struct {
    gchar* name;
    gboolean is_head;    /* checked out */
    gchar* upstream;     /* "origin/main", NULL without one */
    gboolean gone;       /* an upstream is set but its branch no longer exists */
    guint ahead;         /* commits not in the upstream / only in it */
    guint behind;
} GitBranchState;

typedef struct {
    gchar* user_name;    /* effective user.name, NULL when not set */
    GitConfigLevel user_name_level;
    gchar* user_email;
    GitConfigLevel user_email_level;
    gboolean signs;      /* commit.gpgsign */
    gchar* signing_key;  /* user.signingkey, or NULL */
    GPtrArray* remotes;  /* GitRemoteInfo*, by name */
    GPtrArray* branches; /* GitBranchState*, by name */
    GPtrArray* config;   /* GitConfigItem*, by level then as written */
    gchar* repo_config;  /* path of .git/config */
    gchar* global_config; /* path of the global config file, NULL when there is none */
} GitRepoInfo;

void git_repo_info_free(GitRepoInfo* info);

/* The set-up of the repository at root, NULL when it is not one */
GitRepoInfo* git_repo_info_read(const gchar* root);

/* url with its password hidden ("https://me:•••@host/..."), and a user name
 * that looks like a token (20 characters or more, as GitHub's) too;
 * other URLs (ssh "git@host:path", paths) as they are. g_free() */
gchar* git_mask_url(const gchar* url);

/* key names a secret (a password, a token, extra HTTP headers): its value
 * is not shown */
gboolean git_config_key_secret(const gchar* key);

#endif /* GIT_REPO_INFO_H */
