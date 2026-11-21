/* The stashes and tags of a repository (see git_stash_tags.h) */
#include "git/git_stash_tags.h"
#include "git/git_lib.h"
#include "git/git_utils.h"
#include <string.h>

void git_stash_info_free(gpointer info) {
    GitStashInfo* s = info;
    if (s == NULL) return;
    g_free(s->name);
    g_free(s->message);
    g_free(s->hash);
    g_free(s->untracked_hash);
    g_free(s);
}

void git_tag_info_free(gpointer info) {
    GitTagInfo* t = info;
    if (t == NULL) return;
    g_free(t->name);
    g_free(t->hash);
    g_free(t->short_hash);
    g_free(t->subject);
    g_free(t->message);
    g_free(t);
}

/* text as valid UTF-8 without its trailing blanks (NULL: "") */
static gchar* clean_text(const gchar* text) {
    gchar* valid = g_utf8_make_valid(text != NULL ? text : "", -1);
    return g_strchomp(valid);
}

/* First line of message that is not blank */
static gchar* first_line(const gchar* message) {
    const gchar* p = message;
    while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
    const gchar* end = strpbrk(p, "\r\n");
    gchar* line = end != NULL ? g_strndup(p, (gsize)(end - p)) : g_strdup(p);
    return g_strchomp(line);
}

/* --- Stashes */

typedef struct {
    git_repository* repo;
    GPtrArray* list;
} StashScan;

static int on_stash(size_t index, const char* message, const git_oid* id, void* data) {
    StashScan* scan = data;
    GitStashInfo* s = g_new0(GitStashInfo, 1);
    s->index = (guint)index;
    s->name = g_strdup_printf("stash@{%u}", s->index);
    s->message = clean_text(message);
    s->hash = g_strdup(git_oid_tostr_s(id));
    git_commit* commit = NULL;
    if (git_commit_lookup(&commit, scan->repo, id) == 0) {
        s->time = (gint64)git_commit_time(commit);
        /* The third parent of a stash holds its untracked files */
        if (git_commit_parentcount(commit) > 2) s->untracked_hash = g_strdup(git_oid_tostr_s(git_commit_parent_id(commit, 2)));
        git_commit_free(commit);
    }
    g_ptr_array_add(scan->list, s);
    return 0;
}

GPtrArray* git_read_stashes(const gchar* root) {
    git_repository* repo = git_lib_open(root);
    if (repo == NULL) return NULL;
    StashScan scan = { repo, g_ptr_array_new_with_free_func(git_stash_info_free) };
    /* No stash: no refs/stash, which libgit2 reports as an error */
    git_stash_foreach(repo, on_stash, &scan);
    git_repository_free(repo);
    return scan.list;
}

/* --- Tags */

typedef struct {
    git_repository* repo;
    GPtrArray* list;
} TagScan;

static int on_tag(const char* ref_name, git_oid* id, void* data) {
    TagScan* scan = data;
    git_object* obj = NULL;
    if (git_object_lookup(&obj, scan->repo, id, GIT_OBJECT_ANY) != 0) return 0;
    GitTagInfo* t = g_new0(GitTagInfo, 1);
    const gchar* name = g_str_has_prefix(ref_name, "refs/tags/") ? ref_name + strlen("refs/tags/") : ref_name;
    t->name = g_utf8_make_valid(name, -1);
    if (git_object_type(obj) == GIT_OBJECT_TAG) {
        git_tag* tag = (git_tag*)obj;
        t->annotated = TRUE;
        t->message = clean_text(git_tag_message(tag));
        const git_signature* tagger = git_tag_tagger(tag);
        if (tagger != NULL) t->time = (gint64)tagger->when.time;
    }
    git_object* target = NULL;
    if (git_object_peel(&target, obj, GIT_OBJECT_COMMIT) == 0) {
        git_commit* commit = (git_commit*)target;
        t->hash = g_strdup(git_oid_tostr_s(git_commit_id(commit)));
        t->short_hash = git_lib_short_id(target);
        if (t->message == NULL) t->message = clean_text(git_commit_message(commit));
        if (t->time == 0) t->time = (gint64)git_commit_time(commit);
        git_object_free(target);
    }
    if (t->message == NULL) t->message = g_strdup("");
    t->subject = first_line(t->message);
    g_ptr_array_add(scan->list, t);
    git_object_free(obj);
    return 0;
}

/* Newest first; the same date (tags made together): the greatest name first */
static gint tag_newest_first(gconstpointer pa, gconstpointer pb) {
    const GitTagInfo* a = *(GitTagInfo* const*)pa;
    const GitTagInfo* b = *(GitTagInfo* const*)pb;
    if (a->time != b->time) return a->time > b->time ? -1 : 1;
    return -g_utf8_collate(a->name, b->name);
}

GPtrArray* git_read_tags(const gchar* root) {
    git_repository* repo = git_lib_open(root);
    if (repo == NULL) return NULL;
    TagScan scan = { repo, g_ptr_array_new_with_free_func(git_tag_info_free) };
    git_tag_foreach(repo, on_tag, &scan);
    g_ptr_array_sort(scan.list, tag_newest_first);
    git_repository_free(repo);
    return scan.list;
}

/* --- Their content */

gchar* git_show_stash(const gchar* root, const gchar* hash, const gchar* untracked_hash) {
    gchar* text = git_show_commit(root, hash);
    if (text == NULL || untracked_hash == NULL) return text;
    gchar* untracked = git_show_commit(root, untracked_hash);
    if (untracked == NULL) return text;
    gchar* both = g_strconcat(text, "\n", untracked, NULL);
    g_free(untracked);
    g_free(text);
    return both;
}

/* "tag", "Tagger" and "Date" lines, then the message indented, as git shows
 * an annotated tag */
static void append_tag_header(GString* out, git_tag* tag) {
    g_string_append_printf(out, "tag %s\n", git_tag_name(tag));
    const git_signature* tagger = git_tag_tagger(tag);
    if (tagger != NULL) {
        gchar* date = git_lib_signature_date(&tagger->when);
        g_string_append_printf(out, "Tagger: %s <%s>\n", tagger->name, tagger->email);
        g_string_append_printf(out, "Date:   %s\n", date);
        g_free(date);
    }
    g_string_append_c(out, '\n');
    gchar* message = g_strdup(git_tag_message(tag) != NULL ? git_tag_message(tag) : "");
    g_strchomp(message);
    gchar** lines = g_strsplit(message, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) g_string_append_printf(out, "    %s\n", g_strchomp(lines[i]));
    g_strfreev(lines);
    g_free(message);
    g_string_append_c(out, '\n');
}

gchar* git_show_tag(const gchar* root, const gchar* name) {
    if (name == NULL || name[0] == '\0') return NULL;
    git_repository* repo = git_lib_open(root);
    if (repo == NULL) return NULL;
    gchar* ref_name = g_strconcat("refs/tags/", name, NULL);
    git_object* obj = NULL;
    GString* out = NULL;
    gchar* commit_hash = NULL;
    if (git_revparse_single(&obj, repo, ref_name) == 0) {
        out = g_string_new(NULL);
        if (git_object_type(obj) == GIT_OBJECT_TAG) append_tag_header(out, (git_tag*)obj);
        git_object* target = NULL;
        if (git_object_peel(&target, obj, GIT_OBJECT_COMMIT) == 0) {
            commit_hash = g_strdup(git_oid_tostr_s(git_object_id(target)));
            git_object_free(target);
        }
    }
    git_object_free(obj);
    git_repository_free(repo);
    g_free(ref_name);
    if (out == NULL) return NULL;

    gchar* commit = commit_hash != NULL ? git_show_commit(root, commit_hash) : NULL;
    if (commit != NULL) g_string_append(out, commit);
    g_free(commit);
    g_free(commit_hash);
    gchar* text = g_utf8_make_valid(out->str, (gssize)out->len);
    g_string_free(out, TRUE);
    return text;
}
