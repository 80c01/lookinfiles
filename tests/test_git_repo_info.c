/* Unit tests for src/git/git_repo_info.c. The repository and a global
 * config are made in a temporary folder with libgit2 (no git program);
 * libgit2 looks for the system, XDG and global files there only. */
#include "git/git_repo_info.h"

#include <git2.h>
#include <glib/gstdio.h>
#include <string.h>

static void test_mask_url(void) {
    struct { const gchar* in; const gchar* out; } cases[] = {
        { "https://github.com/a/b.git", "https://github.com/a/b.git" },
        { "https://me:secret@host/x.git", "https://me:•••@host/x.git" },
        { "https://x-access-token:ghp_abc@github.com/a", "https://x-access-token:•••@github.com/a" },
        { "https://ghp_0123456789abcdefghij@github.com/a", "https://•••@github.com/a" },
        { "https://me@host/x", "https://me@host/x" },
        { "ssh://git@host:22/x", "ssh://git@host:22/x" },
        { "git@github.com:a/b.git", "git@github.com:a/b.git" },
        { "https://host/path@v2", "https://host/path@v2" }, /* '@' after the host */
        { "/srv/repo.git", "/srv/repo.git" },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        gchar* masked = git_mask_url(cases[i].in);
        g_assert_cmpstr(masked, ==, cases[i].out);
        g_free(masked);
    }
    gchar* none = git_mask_url(NULL);
    g_assert_cmpstr(none, ==, "");
    g_free(none);
}

static void test_secret_keys(void) {
    g_assert_true(git_config_key_secret("http.https://host/.extraheader"));
    g_assert_true(git_config_key_secret("sendemail.smtpPass" "word"));
    g_assert_true(git_config_key_secret("github.token"));
    g_assert_true(git_config_key_secret("foo.clientSecret"));
    g_assert_false(git_config_key_secret("user.email"));
    g_assert_false(git_config_key_secret("credential.helper"));
    g_assert_false(git_config_key_secret(NULL));
}

static gboolean remove_tree(const gchar* path) {
    if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
        GDir* d = g_dir_open(path, 0, NULL);
        const gchar* name;
        while (d != NULL && (name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            remove_tree(child);
            g_free(child);
        }
        if (d != NULL) g_dir_close(d);
    }
    return g_remove(path) == 0;
}

/* An empty commit on HEAD; its id in *id */
static void commit(git_repository* repo, git_oid* id) {
    git_index* index = NULL;
    git_oid tree_id;
    git_tree* tree = NULL;
    git_signature* sig = NULL;
    git_object* parent = NULL;
    g_assert_cmpint(git_repository_index(&index, repo), ==, 0);
    g_assert_cmpint(git_index_write_tree(&tree_id, index), ==, 0);
    g_assert_cmpint(git_tree_lookup(&tree, repo, &tree_id), ==, 0);
    g_assert_cmpint(git_signature_new(&sig, "T", "t@example.org", 1700000000, 0), ==, 0);
    gint n = git_revparse_single(&parent, repo, "HEAD") == 0 ? 1 : 0;
    const git_commit* parents[1] = { (const git_commit*)parent };
    g_assert_cmpint(git_commit_create(id, repo, "HEAD", sig, sig, NULL, "c", tree, (size_t)n, parents), ==, 0);
    git_object_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
}

static const GitConfigItem* item(const GitRepoInfo* info, const gchar* key, GitConfigLevel level) {
    for (guint i = 0; i < info->config->len; i++) {
        const GitConfigItem* it = g_ptr_array_index(info->config, i);
        if (g_strcmp0(it->key, key) == 0 && it->level == level) return it;
    }
    return NULL;
}

static const GitBranchState* branch(const GitRepoInfo* info, const gchar* name) {
    for (guint i = 0; i < info->branches->len; i++) {
        const GitBranchState* b = g_ptr_array_index(info->branches, i);
        if (g_strcmp0(b->name, name) == 0) return b;
    }
    return NULL;
}

static void test_read(void) {
    git_libgit2_init();
    gchar* tmp = g_dir_make_tmp("git_repo_info_XXXXXX", NULL);
    gchar* home = g_build_filename(tmp, "home", NULL);
    gchar* dir = g_build_filename(tmp, "repo", NULL);
    g_assert_cmpint(g_mkdir(home, 0700), ==, 0);
    /* libgit2 looks for the system, XDG and global configs in home only */
    g_assert_cmpint(git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_SYSTEM, home), ==, 0);
    g_assert_cmpint(git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_XDG, home), ==, 0);
    g_assert_cmpint(git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, home), ==, 0);
    gchar* global = g_build_filename(home, ".gitconfig", NULL);
    g_assert_true(g_file_set_contents(global,
                                      "[user]\n\tname = Global Name\n\temail = me@global.org\n"
                                      "[github]\n\ttoken = abc123\n", -1, NULL));

    git_repository* repo = NULL;
    g_assert_cmpint(git_repository_init(&repo, dir, 0), ==, 0);
    git_config* cfg = NULL;
    g_assert_cmpint(git_repository_config(&cfg, repo), ==, 0);
    g_assert_cmpint(git_config_set_string(cfg, "user.email", "me@work.org"), ==, 0);
    g_assert_cmpint(git_config_set_bool(cfg, "commit.gpgsign", 1), ==, 0);
    git_config_free(cfg);

    git_remote* remote = NULL;
    g_assert_cmpint(git_remote_create(&remote, repo, "origin", "https://me:pw@host/x.git"), ==, 0);
    g_assert_cmpint(git_remote_set_pushurl(repo, "origin", "ssh://git@host/x.git"), ==, 0);
    git_remote_free(remote);

    /* main: one commit ahead of origin/main; old: upstream gone; solo: none */
    git_oid first, second;
    commit(repo, &first);
    git_reference* ref = NULL;
    g_assert_cmpint(git_reference_create(&ref, repo, "refs/remotes/origin/main", &first, 0, NULL), ==, 0);
    git_reference_free(ref);
    commit(repo, &second);
    git_reference* head = NULL;
    g_assert_cmpint(git_repository_head(&head, repo), ==, 0);
    const char* head_name = NULL;
    g_assert_cmpint(git_branch_name(&head_name, head), ==, 0);
    gchar* main_name = g_strdup(head_name);
    g_assert_cmpint(git_branch_set_upstream(head, "origin/main"), ==, 0);
    git_reference_free(head);
    git_commit* c = NULL;
    g_assert_cmpint(git_commit_lookup(&c, repo, &first), ==, 0);
    g_assert_cmpint(git_branch_create(&ref, repo, "old", c, 0), ==, 0);
    git_reference_free(ref);
    g_assert_cmpint(git_branch_create(&ref, repo, "solo", c, 0), ==, 0);
    git_reference_free(ref);
    git_commit_free(c);
    g_assert_cmpint(git_repository_config(&cfg, repo), ==, 0);
    g_assert_cmpint(git_config_set_string(cfg, "branch.old.remote", "origin"), ==, 0);
    g_assert_cmpint(git_config_set_string(cfg, "branch.old.merge", "refs/heads/old"), ==, 0);
    git_config_free(cfg);

    GitRepoInfo* info = git_repo_info_read(dir);
    g_assert_nonnull(info);
    /* Identity: the name from the global file, the email from the repository's */
    g_assert_cmpstr(info->user_name, ==, "Global Name");
    g_assert_cmpint(info->user_name_level, ==, GIT_CONFIG_GLOBAL);
    g_assert_cmpstr(info->user_email, ==, "me@work.org");
    g_assert_cmpint(info->user_email_level, ==, GIT_CONFIG_REPOSITORY);
    g_assert_true(info->signs);
    g_assert_null(info->signing_key);

    /* Config: the global email overridden, the token hidden */
    const GitConfigItem* it = item(info, "user.email", GIT_CONFIG_GLOBAL);
    g_assert_nonnull(it);
    g_assert_true(it->overridden);
    it = item(info, "user.email", GIT_CONFIG_REPOSITORY);
    g_assert_false(it->overridden);
    it = item(info, "user.name", GIT_CONFIG_GLOBAL);
    g_assert_false(it->overridden);
    it = item(info, "github.token", GIT_CONFIG_GLOBAL);
    g_assert_cmpstr(it->value, ==, "•••");
    it = item(info, "remote.origin.url", GIT_CONFIG_REPOSITORY);
    g_assert_cmpstr(it->value, ==, "https://me:•••@host/x.git");
    /* By level: the global entries before the repository's */
    for (guint i = 1; i < info->config->len; i++) {
        g_assert_cmpint(((GitConfigItem*)info->config->pdata[i - 1])->level, <=,
                        ((GitConfigItem*)info->config->pdata[i])->level);
    }

    /* Remotes */
    g_assert_cmpuint(info->remotes->len, ==, 1);
    const GitRemoteInfo* r = g_ptr_array_index(info->remotes, 0);
    g_assert_cmpstr(r->name, ==, "origin");
    g_assert_cmpstr(r->url, ==, "https://me:•••@host/x.git");
    g_assert_cmpstr(r->push_url, ==, "ssh://git@host/x.git");

    /* Branches */
    g_assert_cmpuint(info->branches->len, ==, 3);
    const GitBranchState* b = branch(info, main_name);
    g_assert_nonnull(b);
    g_assert_true(b->is_head);
    g_assert_cmpstr(b->upstream, ==, "origin/main");
    g_assert_false(b->gone);
    g_assert_cmpuint(b->ahead, ==, 1);
    g_assert_cmpuint(b->behind, ==, 0);
    b = branch(info, "old");
    g_assert_cmpstr(b->upstream, ==, "origin/old");
    g_assert_true(b->gone);
    b = branch(info, "solo");
    g_assert_null(b->upstream);
    g_assert_false(b->is_head);

    g_assert_true(g_str_has_suffix(info->repo_config, "config"));
    g_assert_cmpstr(info->global_config, ==, global);
    git_repo_info_free(info);

    git_repository_free(repo);
    g_assert_null(git_repo_info_read(home));
    remove_tree(tmp);
    g_free(main_name);
    g_free(global);
    g_free(dir);
    g_free(home);
    g_free(tmp);
    git_libgit2_shutdown();
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/git_repo_info/mask_url", test_mask_url);
    g_test_add_func("/git_repo_info/secret_keys", test_secret_keys);
    g_test_add_func("/git_repo_info/read", test_read);
    return g_test_run();
}
