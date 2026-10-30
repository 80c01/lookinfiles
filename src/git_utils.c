/* Git repository helpers: root/dir discovery, branch, working tree status.
 * No AppData and no widgets; safe to call from worker threads. */
#include "git_utils.h"
#include "palette.h"

#include <string.h>

/* Find Git repository root directory from start_dir (or any parent), NULL if not in repo */
gchar* find_git_root(const gchar* start_dir) {
    if (start_dir == NULL || start_dir[0] == '\0') {
        return NULL;
    }
    if (strstr(start_dir, "/.git/") != NULL || g_str_has_suffix(start_dir, "/.git") ||
        g_strcmp0(start_dir, ".git") == 0) {
        return NULL;
    }
    gchar* curr = g_canonicalize_filename(start_dir, NULL);
    while (curr != NULL && curr[0] != '\0') {
        gchar* dot_git = g_build_filename(curr, ".git", NULL);
        gboolean exists = g_file_test(dot_git, G_FILE_TEST_EXISTS);
        g_free(dot_git);
        if (exists) {
            return curr; /* found git root */
        }
        gchar* parent = g_path_get_dirname(curr);
        if (g_strcmp0(curr, parent) == 0 || g_strcmp0(parent, ".") == 0) {
            g_free(parent);
            break;
        }
        g_free(curr);
        curr = parent;
    }
    g_free(curr);
    return NULL;
}

/* Scan a single git repository root and populate full_path -> badge markup in statuses */
/* Explorer badge: the status letter in bold, in the role's colour */
static gchar* badge_markup(PaletteRole role, const gchar* letter) {
    return g_strdup_printf("<span foreground=\"%s\" weight=\"bold\">%s</span>", palette_color(role), letter);
}

static void scan_git_repo_into(GHashTable* statuses, const gchar* raw_root) {
    if (statuses == NULL || raw_root == NULL) {
        return;
    }
    gchar* git_root = g_canonicalize_filename(raw_root, NULL);

    gchar* argv[] = {
        "git",
        "--no-optional-locks",
        "-c",
        "core.quotepath=false",
        "status",
        "--porcelain=v1",
        "-uall",
        NULL
    };

    gchar* stdout_buf = NULL;
    gchar* stderr_buf = NULL;
    gint exit_status = 0;
    GError* err = NULL;
    gboolean ok = g_spawn_sync(git_root,
                               argv,
                               NULL,
                               G_SPAWN_SEARCH_PATH,
                               NULL,
                               NULL,
                               &stdout_buf,
                               &stderr_buf,
                               &exit_status,
                               &err);
    if (err != NULL) {
        g_error_free(err);
    }
    g_free(stderr_buf);

    if (!ok || exit_status != 0 || stdout_buf == NULL) {
        g_free(stdout_buf);
        g_free(git_root);
        return;
    }

    gsize root_len = strlen(git_root);

    gchar** lines = g_strsplit(stdout_buf, "\n", -1);
    for (gint i = 0; lines != NULL && lines[i] != NULL; i++) {
        const gchar* line = lines[i];
        if (strlen(line) < 4) {
            continue;
        }
        gchar x = line[0];
        gchar y = line[1];
        const gchar* rel = line + 3;
        while (*rel == ' ') {
            rel++;
        }
        if (*rel == '\0') {
            continue;
        }

        /* If renamed: "R  old -> new", target is "new" */
        const gchar* arrow = strstr(rel, " -> ");
        if (arrow != NULL) {
            rel = arrow + 4;
        }

        /* Unquote if enclosed in double quotes */
        gchar* unquoted = NULL;
        if (rel[0] == '"') {
            gsize len = strlen(rel);
            if (len >= 2 && rel[len - 1] == '"') {
                unquoted = g_strndup(rel + 1, len - 2);
                rel = unquoted;
            }
        }

        PaletteRole role = PAL_GIT_MODIFIED;
        const gchar* letter = "M";
        gboolean is_modified = FALSE;
        gboolean is_untracked = FALSE;

        if (x == '?' && y == '?') {
            /* Untracked */
            role = PAL_GIT_ADDED;
            letter = "U";
            is_untracked = TRUE;
        } else if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) {
            /* Conflict / Unmerged */
            role = PAL_GIT_CONFLICT;
            letter = "!";
            is_modified = TRUE;
        } else if (x == 'A' && (y == ' ' || y == '\0')) {
            /* Added / Staged */
            role = PAL_GIT_ADDED;
            letter = "A";
            is_modified = TRUE;
        } else if (x == 'D' || y == 'D') {
            /* Deleted */
            role = PAL_GIT_DELETED;
            letter = "D";
            is_modified = TRUE;
        } else if (x == 'R' || y == 'R') {
            /* Renamed */
            role = PAL_GIT_RENAMED;
            letter = "R";
            is_modified = TRUE;
        } else if (x == 'M' || y == 'M' || x == 'A' || x == 'T' || y == 'T') {
            /* Modified */
            is_modified = TRUE;
        } else {
            /* Other working tree change */
            is_modified = TRUE;
        }

        gchar* full_raw = g_build_filename(git_root, rel, NULL);
        gchar* full_path = g_canonicalize_filename(full_raw, NULL);
        g_free(full_raw);
        if (unquoted != NULL) {
            g_free(unquoted);
        }

        /* Store file's status */
        g_hash_table_replace(statuses, g_strdup(full_path), badge_markup(role, letter));

        /* Rollup to parent directories up to git_root */
        gchar* parent = g_path_get_dirname(full_path);
        g_free(full_path);

        while (parent != NULL) {
            gsize parent_len = strlen(parent);
            gboolean under_root = (g_strcmp0(parent, git_root) == 0) ||
                                  (parent_len > root_len &&
                                   g_str_has_prefix(parent, git_root) &&
                                   parent[root_len] == '/');
            if (!under_root) {
                g_free(parent);
                break;
            }

            const gchar* existing = (const gchar*)g_hash_table_lookup(statuses, parent);
            if (is_modified) {
                /* Modification takes precedence over untracked */
                if (existing == NULL || strstr(existing, ">M<") == NULL) {
                    g_hash_table_replace(statuses, g_strdup(parent),
                                         badge_markup(PAL_GIT_MODIFIED, "M"));
                }
            } else if (is_untracked) {
                if (existing == NULL) {
                    g_hash_table_replace(statuses, g_strdup(parent),
                                         badge_markup(PAL_GIT_ADDED, "U"));
                }
            }

            if (g_strcmp0(parent, git_root) == 0) {
                g_free(parent);
                break;
            }

            gchar* next = g_path_get_dirname(parent);
            if (g_strcmp0(next, parent) == 0 || g_strcmp0(next, ".") == 0) {
                g_free(next);
                g_free(parent);
                break;
            }
            g_free(parent);
            parent = next;
        }
    }

    g_strfreev(lines);
    g_free(stdout_buf);
    g_free(git_root);
}

/* Run 'git status --porcelain=v1 -uall' and return a mapping of full_path -> badge markup */
GHashTable* scan_git_statuses(const gchar* dir) {
    if (dir == NULL || dir[0] == '\0') {
        return NULL;
    }
    GHashTable* statuses = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    gchar* raw_root = find_git_root(dir);
    if (raw_root != NULL) {
        scan_git_repo_into(statuses, raw_root);
        g_free(raw_root);
    } else if (g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        /* If dir is not itself in a git repo, check if any immediate child directory is a git repo */
        GDir* d = g_dir_open(dir, 0, NULL);
        if (d != NULL) {
            const gchar* entry_name = NULL;
            while ((entry_name = g_dir_read_name(d)) != NULL) {
                gchar* sub = g_build_filename(dir, entry_name, NULL);
                gchar* dot_git = g_build_filename(sub, ".git", NULL);
                if (g_file_test(dot_git, G_FILE_TEST_EXISTS)) {
                    scan_git_repo_into(statuses, sub);
                }
                g_free(dot_git);
                g_free(sub);
            }
            g_dir_close(d);
        }
    }

    if (g_hash_table_size(statuses) == 0) {
        g_hash_table_destroy(statuses);
        return NULL;
    }

    return statuses;
}

/* Actual git directory of a repository root: "<root>/.git", or the target of a
 * "gitdir: <path>" file (worktrees and submodules). NULL if none. */
gchar* git_resolve_dir(const gchar* git_root) {
    if (git_root == NULL) {
        return NULL;
    }
    gchar* dot_git = g_build_filename(git_root, ".git", NULL);
    gchar* git_dir = NULL;
    if (g_file_test(dot_git, G_FILE_TEST_IS_DIR)) {
        git_dir = g_strdup(dot_git);
    } else {
        gchar* contents = NULL;
        if (g_file_get_contents(dot_git, &contents, NULL, NULL) &&
            g_str_has_prefix(contents, "gitdir:")) {
            gchar* p = g_strstrip(contents + strlen("gitdir:"));
            git_dir = g_path_is_absolute(p) ? g_strdup(p) : g_build_filename(git_root, p, NULL);
        }
        g_free(contents);
    }
    g_free(dot_git);
    return git_dir;
}

/* Current branch of the repository containing dir (short SHA when detached HEAD).
 * Reads HEAD directly: no subprocess. */
gchar* git_read_branch(const gchar* dir) {
    gchar* root = find_git_root(dir);
    gchar* git_dir = git_resolve_dir(root);
    g_free(root);
    if (git_dir == NULL) {
        return NULL;
    }

    gchar* head_path = g_build_filename(git_dir, "HEAD", NULL);
    g_free(git_dir);
    gchar* head = NULL;
    gchar* branch = NULL;
    if (g_file_get_contents(head_path, &head, NULL, NULL)) {
        g_strstrip(head);
        if (g_str_has_prefix(head, "ref: refs/heads/")) {
            branch = g_strdup(head + strlen("ref: refs/heads/"));
        } else if (g_str_has_prefix(head, "ref: ")) {
            branch = g_strdup(head + strlen("ref: "));
        } else if (head[0] != '\0') {
            branch = g_strndup(head, 7);
        }
    }
    g_free(head);
    g_free(head_path);
    return branch;
}

gchar* git_read_head_blob(const gchar* path, gsize* len) {
    *len = 0;
    if (path == NULL) {
        return NULL;
    }
    gchar* dir = g_path_get_dirname(path);
    gchar* root = find_git_root(dir);
    if (root == NULL) {
        g_free(dir);
        return NULL;
    }
    g_free(root);

    /* "HEAD:./name" is resolved relative to the working directory */
    gchar* base = g_path_get_basename(path);
    gchar* spec = g_strconcat("HEAD:./", base, NULL);
    gchar* argv[] = {"git", "--no-pager", "show", "--no-textconv", spec, NULL};
    gchar* out = NULL;
    gint exit_status = 0;
    gsize out_len = 0;
    GError* err = NULL;
    gboolean ok = g_spawn_sync(dir, argv, NULL,
                               G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                               NULL, NULL, &out, NULL, &exit_status, &err);
    g_clear_error(&err);
    g_free(spec);
    g_free(base);
    g_free(dir);
    if (!ok || !g_spawn_check_wait_status(exit_status, NULL) || out == NULL) {
        g_free(out);
        return NULL;
    }
    out_len = strlen(out); /* binary blobs are cut at the first NUL: not shown as text anyway */
    *len = out_len;
    return out;
}
