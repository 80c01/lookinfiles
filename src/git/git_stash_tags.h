#ifndef GIT_STASH_TAGS_H
#define GIT_STASH_TAGS_H

/* The stashes and the tags of a repository, read through libgit2 (no git
 * program), and what each holds as text (its patch, its commit). No AppData
 * and no widgets; worker threads only (each call opens its own repository). */
#include <glib.h>

/* One stash (git_read_stashes()) */
typedef struct {
    guint index;          /* 0 for the newest */
    gchar* name;          /* "stash@{0}" */
    gchar* message;       /* "WIP on main: 1a2b3c4 Subject", "On main: try" */
    gchar* hash;          /* full SHA of the stash commit */
    gchar* untracked_hash; /* full SHA of its untracked files (`git stash -u`), or NULL */
    gint64 time;          /* when it was made, seconds since the epoch */
} GitStashInfo;

void git_stash_info_free(gpointer info);

/* One tag (git_read_tags()) */
typedef struct {
    gchar* name;          /* "v1.0" */
    gboolean annotated;   /* a tag object (git tag -a), not a plain ref */
    gchar* hash;          /* full SHA of the commit it points to, NULL if it points to no commit */
    gchar* short_hash;    /* its shortest unique abbreviation, NULL with hash */
    gchar* subject;       /* first line of the tag's message, else of the commit's */
    gchar* message;       /* whole message of the tag, else of the commit, without trailing breaks */
    gint64 time;          /* tagger date, else committer date, seconds since the epoch; 0 if none */
} GitTagInfo;

void git_tag_info_free(gpointer info);

/* The stashes of the repository at root, newest first; empty when there are
 * none, NULL when root is not a work tree */
GPtrArray* git_read_stashes(const gchar* root);

/* The tags of the repository at root, newest first (by date, then name);
 * empty when there are none, NULL when root is not a work tree */
GPtrArray* git_read_tags(const gchar* root);

/* What the stash with commit hash changes against the commit it was made on
 * (`git stash show --patch`), laid out like git_show_commit(); with
 * untracked_hash (or NULL), then the untracked files it holds. NULL when
 * the stash is not found. Valid UTF-8. */
gchar* git_show_stash(const gchar* root, const gchar* hash, const gchar* untracked_hash);

/* Tag name: for an annotated tag its "tag", "Tagger" and "Date" lines and
 * its message, then the commit it points to as git_show_commit() lays it
 * out (`git show <tag>`). NULL when the tag is not found. Valid UTF-8. */
gchar* git_show_tag(const gchar* root, const gchar* name);

#endif /* GIT_STASH_TAGS_H */
