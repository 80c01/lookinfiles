/* Git repository helpers: root/dir discovery, branch, working tree status,
 * history, file lists. Everything but the root and branch lookups goes
 * through libgit2 (no git program). No AppData and no widgets; safe to call
 * from worker threads (each call opens its own repository). */
#include "git_utils.h"
#include "palette.h"

#include <git2.h>
#include <string.h>

/* libgit2 is initialised once and stays so until the process exits */
static void libgit2_ready(void) {
    static gsize done = 0;
    if (g_once_init_enter(&done)) {
        git_libgit2_init();
        g_once_init_leave(&done, 1);
    }
}

/* The repository whose work tree starts at root, NULL if none (or bare) */
static git_repository* open_work_tree(const gchar* root) {
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
    return repo;
}

/* full relative to root with '/' separators, NULL when it is not below root */
static gchar* path_below(const gchar* root, const gchar* full) {
    gsize n = strlen(root);
    while (n > 0 && (root[n - 1] == '/' || root[n - 1] == '\\')) {
        n--;
    }
    if (strncmp(full, root, n) != 0 || (full[n] != '/' && full[n] != '\\') || full[n + 1] == '\0') {
        return NULL;
    }
    gchar* rel = g_strdup(full + n + 1);
    for (gchar* c = rel; *c != '\0'; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    return rel;
}

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

/* Explorer badge: the status letter in bold, in the role's colour */
static gchar* badge_markup(PaletteRole role, const gchar* letter) {
    return g_strdup_printf("<span foreground=\"%s\" weight=\"bold\">%s</span>", palette_color(role), letter);
}

/* Record the badge of git_root/rel and roll it up to its parent folders */
static void add_status(GHashTable* statuses, const gchar* git_root, const gchar* rel, PaletteRole role,
                       const gchar* letter, gboolean is_modified, gboolean is_untracked) {
    gsize root_len = strlen(git_root);
    gchar* full_raw = g_build_filename(git_root, rel, NULL);
    gchar* full_path = g_canonicalize_filename(full_raw, NULL);
    g_free(full_raw);

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
                               (parent[root_len] == '/' || parent[root_len] == '\\'));
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

/* Changes of the work tree, as `git status -uall` lists them */
#define WT_CHANGES (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED | GIT_STATUS_WT_TYPECHANGE | \
                    GIT_STATUS_WT_RENAMED | GIT_STATUS_WT_UNREADABLE)
#define INDEX_CHANGES (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_DELETED | \
                       GIT_STATUS_INDEX_RENAMED | GIT_STATUS_INDEX_TYPECHANGE)

/* Scan a single git repository root and populate full_path -> badge markup in statuses */
static void scan_git_repo_into(GHashTable* statuses, const gchar* raw_root) {
    if (statuses == NULL || raw_root == NULL) {
        return;
    }
    gchar* git_root = g_canonicalize_filename(raw_root, NULL);
    git_repository* repo = open_work_tree(git_root);
    git_status_list* list = NULL;
    git_status_options opts;
    git_status_options_init(&opts, GIT_STATUS_OPTIONS_VERSION);
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
                 GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;
    if (repo == NULL || git_status_list_new(&list, repo, &opts) != 0) {
        git_repository_free(repo);
        g_free(git_root);
        return;
    }

    gsize count = git_status_list_entrycount(list);
    for (gsize i = 0; i < count; i++) {
        const git_status_entry* e = git_status_byindex(list, i);
        unsigned int st = e->status;
        if (st == GIT_STATUS_CURRENT || (st & GIT_STATUS_IGNORED)) {
            continue;
        }
        /* The new name of a renamed file */
        const git_diff_delta* delta = e->index_to_workdir != NULL ? e->index_to_workdir : e->head_to_index;
        const gchar* rel = delta != NULL ? delta->new_file.path : NULL;
        if (rel == NULL || rel[0] == '\0') {
            continue;
        }

        PaletteRole role = PAL_GIT_MODIFIED;
        const gchar* letter = "M";
        gboolean is_untracked = FALSE;
        if (st & GIT_STATUS_CONFLICTED) {
            role = PAL_GIT_CONFLICT;
            letter = "!";
        } else if ((st & GIT_STATUS_WT_NEW) && !(st & INDEX_CHANGES)) {
            role = PAL_GIT_ADDED;
            letter = "U";
            is_untracked = TRUE;
        } else if ((st & GIT_STATUS_INDEX_NEW) && !(st & WT_CHANGES)) {
            role = PAL_GIT_ADDED; /* added to the index, unchanged since */
            letter = "A";
        } else if (st & (GIT_STATUS_INDEX_DELETED | GIT_STATUS_WT_DELETED)) {
            role = PAL_GIT_DELETED;
            letter = "D";
        } else if (st & (GIT_STATUS_INDEX_RENAMED | GIT_STATUS_WT_RENAMED)) {
            role = PAL_GIT_RENAMED;
            letter = "R";
        }
        add_status(statuses, git_root, rel, role, letter, !is_untracked, is_untracked);
    }

    git_status_list_free(list);
    git_repository_free(repo);
    g_free(git_root);
}

/* Status of the repository of dir: full_path -> badge markup (see git_utils.h) */
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

/* Number of commits HEAD is ahead of and behind its upstream tracking branch.
 * Returns TRUE when dir is in a repository and HEAD is a branch with an upstream
 * configured; *ahead and *behind receive the counts. Returns FALSE otherwise.
 * Safe to call from worker threads. */
gboolean git_read_ahead_behind(const gchar* dir, guint* ahead, guint* behind) {
    if (ahead != NULL) {
        *ahead = 0;
    }
    if (behind != NULL) {
        *behind = 0;
    }
    if (dir == NULL || dir[0] == '\0') {
        return FALSE;
    }
    gchar* root = find_git_root(dir);
    if (root == NULL) {
        return FALSE;
    }
    git_repository* repo = open_work_tree(root);
    g_free(root);
    if (repo == NULL) {
        return FALSE;
    }

    git_reference* head = NULL;
    if (git_repository_head(&head, repo) != 0) {
        git_repository_free(repo);
        return FALSE;
    }

    if (!git_reference_is_branch(head)) {
        git_reference_free(head);
        git_repository_free(repo);
        return FALSE;
    }

    git_reference* upstream = NULL;
    if (git_branch_upstream(&upstream, head) != 0) {
        git_reference_free(head);
        git_repository_free(repo);
        return FALSE;
    }

    const git_oid* local_oid = git_reference_target(head);
    git_reference* resolved_head = NULL;
    if (local_oid == NULL) {
        if (git_reference_resolve(&resolved_head, head) == 0) {
            local_oid = git_reference_target(resolved_head);
        }
    }

    const git_oid* upstream_oid = git_reference_target(upstream);
    git_reference* resolved_upstream = NULL;
    if (upstream_oid == NULL) {
        if (git_reference_resolve(&resolved_upstream, upstream) == 0) {
            upstream_oid = git_reference_target(resolved_upstream);
        }
    }

    gboolean ok = FALSE;
    if (local_oid != NULL && upstream_oid != NULL) {
        size_t a = 0;
        size_t b = 0;
        if (git_graph_ahead_behind(&a, &b, repo, local_oid, upstream_oid) == 0) {
            if (ahead != NULL) {
                *ahead = (guint)a;
            }
            if (behind != NULL) {
                *behind = (guint)b;
            }
            ok = TRUE;
        }
    }

    if (resolved_upstream != NULL) {
        git_reference_free(resolved_upstream);
    }
    if (resolved_head != NULL) {
        git_reference_free(resolved_head);
    }
    git_reference_free(upstream);
    git_reference_free(head);
    git_repository_free(repo);
    return ok;
}

gchar* git_read_head_blob(const gchar* path, gsize* len) {
    *len = 0;
    if (path == NULL) {
        return NULL;
    }
    gchar* full = g_canonicalize_filename(path, NULL);
    gchar* dir = g_path_get_dirname(full);
    gchar* root = find_git_root(dir);
    g_free(dir);
    gchar* rel = root != NULL ? path_below(root, full) : NULL;
    g_free(full);
    git_repository* repo = rel != NULL ? open_work_tree(root) : NULL;
    g_free(root);

    gchar* out = NULL;
    git_object* tree = NULL;
    git_tree_entry* entry = NULL;
    git_blob* blob = NULL;
    if (repo != NULL && git_revparse_single(&tree, repo, "HEAD^{tree}") == 0 &&
        git_tree_entry_bypath(&entry, (git_tree*)tree, rel) == 0 &&
        git_tree_entry_type(entry) == GIT_OBJECT_BLOB &&
        git_blob_lookup(&blob, repo, git_tree_entry_id(entry)) == 0) {
        out = g_strndup(git_blob_rawcontent(blob), (gsize)git_blob_rawsize(blob));
        *len = strlen(out);
    }
    git_blob_free(blob);
    git_tree_entry_free(entry);
    git_object_free(tree);
    git_repository_free(repo);
    g_free(rel);
    return out;
}

void git_commit_info_free(gpointer data) {
    GitCommitInfo* c = (GitCommitInfo*)data;
    if (c == NULL) {
        return;
    }
    g_free(c->hash);
    g_free(c->short_hash);
    g_free(c->subject);
    g_free(c->author);
    g_free(c->date);
    g_free(c);
}

/* "1 day" or "3 days" */
static gchar* count_of(gint64 n, const gchar* unit) {
    return g_strdup_printf("%" G_GINT64_FORMAT " %s%s", n, unit, n == 1 ? "" : "s");
}

static gchar* units_ago(gint64 n, const gchar* unit) {
    gchar* count = count_of(n, unit);
    gchar* text = g_strconcat(count, " ago", NULL);
    g_free(count);
    return text;
}

/* Same steps and rounding as git (show_date_relative() in date.c) */
gchar* git_relative_date(gint64 seconds) {
    if (seconds < 0) {
        return g_strdup("in the future");
    }
    gint64 diff = seconds;
    if (diff < 90) {
        return units_ago(diff, "second");
    }
    diff = (diff + 30) / 60; /* minutes */
    if (diff < 90) {
        return units_ago(diff, "minute");
    }
    diff = (diff + 30) / 60; /* hours */
    if (diff < 36) {
        return units_ago(diff, "hour");
    }
    diff = (diff + 12) / 24; /* days */
    if (diff < 14) {
        return units_ago(diff, "day");
    }
    if (diff < 70) {
        return units_ago((diff + 3) / 7, "week");
    }
    if (diff < 365) {
        return units_ago((diff + 15) / 30, "month");
    }
    if (diff < 1825) {
        gint64 total_months = (diff * 12 * 2 + 365) / (365 * 2);
        gint64 years = total_months / 12;
        gint64 months = total_months % 12;
        if (months == 0) {
            return units_ago(years, "year");
        }
        gchar* y = count_of(years, "year");
        gchar* m = count_of(months, "month");
        gchar* text = g_strdup_printf("%s, %s ago", y, m);
        g_free(y);
        g_free(m);
        return text;
    }
    return units_ago((diff + 183) / 365, "year");
}

/* Shortest unique abbreviation of an object, like git's %h */
static gchar* short_id(const git_object* obj) {
    git_buf buf = {0};
    gchar* id = NULL;
    if (git_object_short_id(&buf, obj) == 0 && buf.ptr != NULL) {
        id = g_strndup(buf.ptr, buf.size);
    } else {
        id = g_strndup(git_oid_tostr_s(git_object_id(obj)), 7);
    }
    git_buf_dispose(&buf);
    return id;
}

GPtrArray* git_read_log(const gchar* root, guint max) {
    git_repository* repo = open_work_tree(root);
    if (repo == NULL) {
        return NULL;
    }
    GPtrArray* commits = g_ptr_array_new_with_free_func(git_commit_info_free);
    git_revwalk* walk = NULL;
    if (git_revwalk_new(&walk, repo) == 0) {
        git_revwalk_sorting(walk, GIT_SORT_TIME);
        gint64 now = g_get_real_time() / G_USEC_PER_SEC;
        git_oid oid;
        if (git_revwalk_push_head(walk) == 0) { /* fails without commits */
            while (commits->len < max && git_revwalk_next(&oid, walk) == 0) {
                git_commit* c = NULL;
                if (git_commit_lookup(&c, repo, &oid) != 0) {
                    continue;
                }
                const gchar* summary = git_commit_summary(c);
                const git_signature* author = git_commit_author(c);
                GitCommitInfo* ci = g_new0(GitCommitInfo, 1);
                ci->hash = g_strdup(git_oid_tostr_s(&oid));
                ci->short_hash = short_id((const git_object*)c);
                ci->subject = g_utf8_make_valid(summary != NULL ? summary : "", -1);
                ci->author = g_utf8_make_valid(author != NULL && author->name != NULL ? author->name : "", -1);
                ci->date = git_relative_date(now - (gint64)git_commit_time(c));
                g_ptr_array_add(commits, ci);
                git_commit_free(c);
            }
        }
        git_revwalk_free(walk);
    }
    git_repository_free(repo);
    return commits;
}

/* git's default date format, in the signature's time zone and in English
 * whatever the locale: "Thu Nov 5 14:03:12 2026 +0100" */
static gchar* signature_date(const git_time* when) {
    static const gchar* const DAYS[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const gchar* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    GDateTime* utc = g_date_time_new_from_unix_utc((gint64)when->time);
    if (utc == NULL) {
        return g_strdup("");
    }
    GTimeZone* tz = g_time_zone_new_offset(when->offset * 60);
    GDateTime* t = g_date_time_to_timezone(utc, tz);
    gint offset = when->offset < 0 ? -when->offset : when->offset;
    gchar* text = g_strdup_printf("%s %s %d %02d:%02d:%02d %d %c%02d%02d",
                                  DAYS[g_date_time_get_day_of_week(t) - 1],
                                  MONTHS[g_date_time_get_month(t) - 1],
                                  g_date_time_get_day_of_month(t), g_date_time_get_hour(t),
                                  g_date_time_get_minute(t), g_date_time_get_second(t),
                                  g_date_time_get_year(t), when->offset < 0 ? '-' : '+',
                                  offset / 60, offset % 60);
    g_date_time_unref(t);
    g_time_zone_unref(tz);
    g_date_time_unref(utc);
    return text;
}

/* "commit", "Merge", "Author" and "Date" lines, then the message indented
 * (blank lines too, as git does) */
static void append_commit_header(GString* out, git_commit* c) {
    g_string_append_printf(out, "commit %s\n", git_oid_tostr_s(git_commit_id(c)));
    if (git_commit_parentcount(c) > 1) {
        g_string_append(out, "Merge:");
        for (guint i = 0; i < git_commit_parentcount(c); i++) {
            git_commit* p = NULL;
            if (git_commit_parent(&p, c, i) == 0) {
                gchar* id = short_id((const git_object*)p);
                g_string_append_printf(out, " %s", id);
                g_free(id);
                git_commit_free(p);
            }
        }
        g_string_append_c(out, '\n');
    }
    const git_signature* author = git_commit_author(c);
    if (author != NULL) {
        gchar* date = signature_date(&author->when);
        g_string_append_printf(out, "Author: %s <%s>\n", author->name, author->email);
        g_string_append_printf(out, "Date:   %s\n", date);
        g_free(date);
    }
    g_string_append_c(out, '\n');

    gchar* message = g_strdup(git_commit_message(c) != NULL ? git_commit_message(c) : "");
    g_strchomp(message);
    gchar** lines = g_strsplit(message, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        g_string_append_printf(out, "    %s\n", g_strchomp(lines[i]));
    }
    g_strfreev(lines);
    g_free(message);
}

/* Name of a file in the diffstat: "a/{old => new}/c" for a rename, as git
 * prints it (pprint_rename() in diff.c) */
static gchar* stat_name(const git_diff_delta* d) {
    const gchar* a = d->old_file.path;
    const gchar* b = d->new_file.path;
    if (d->status != GIT_DELTA_RENAMED && d->status != GIT_DELTA_COPIED) {
        return g_strdup(b != NULL ? b : a);
    }
    gint len_a = (gint)strlen(a), len_b = (gint)strlen(b);
    gint pfx = 0;
    for (gint k = 0; a[k] != '\0' && a[k] == b[k]; k++) {
        if (a[k] == '/') {
            pfx = k + 1;
        }
    }
    /* A common suffix starts at a slash; it may reach back to the prefix's slash */
    gint sfx = 0;
    gint adjust = pfx > 0 ? 1 : 0;
    for (gint i = len_a, j = len_b; pfx - adjust <= i && pfx - adjust <= j && a[i] == b[j]; i--, j--) {
        if (a[i] == '/') {
            sfx = len_a - i;
        }
    }
    gint a_mid = MAX(len_a - pfx - sfx, 0), b_mid = MAX(len_b - pfx - sfx, 0);
    GString* name = g_string_new(NULL);
    if (pfx + sfx > 0) {
        g_string_append_len(name, a, pfx);
        g_string_append_c(name, '{');
    }
    g_string_append_len(name, a + pfx, a_mid);
    g_string_append(name, " => ");
    g_string_append_len(name, b + pfx, b_mid);
    if (pfx + sfx > 0) {
        g_string_append_c(name, '}');
        g_string_append_len(name, a + len_a - sfx, sfx);
    }
    return g_string_free(name, FALSE);
}

/* One line of the diffstat */
typedef struct {
    gchar* name;
    gboolean binary;
    gsize added, deleted; /* lines; for a binary file, the new and old sizes */
} StatLine;

static gint decimal_width(gsize n) {
    gint w = 1;
    for (; n >= 10; n /= 10) {
        w++;
    }
    return w;
}

static gsize scale_linear(gsize it, gint width, gsize max_change) {
    return it == 0 ? 0 : 1 + it * (gsize)(width - 1) / max_change;
}

static void append_count(GString* out, gsize n, const gchar* unit, const gchar* sign) {
    g_string_append_printf(out, ", %" G_GSIZE_FORMAT " %s%s(%s)", n, unit, n == 1 ? "" : "s", sign);
}

/* The diffstat of `git show --stat` (80 columns), same layout and scaling as
 * git (show_stats() in diff.c) */
static void append_diffstat(GString* out, git_diff* diff) {
    gsize n = git_diff_num_deltas(diff);
    StatLine* lines = g_new0(StatLine, n);
    gint max_len = 0, bin_width = 0;
    gsize max_change = 0, insertions = 0, deletions = 0;
    for (gsize i = 0; i < n; i++) {
        git_patch* patch = NULL;
        const git_diff_delta* d = git_diff_get_delta(diff, i);
        lines[i].name = stat_name(d);
        max_len = MAX(max_len, (gint)g_utf8_strlen(lines[i].name, -1));
        if (git_patch_from_diff(&patch, diff, i) == 0) {
            d = git_patch_get_delta(patch);
            lines[i].binary = (d->flags & GIT_DIFF_FLAG_BINARY) != 0;
            if (lines[i].binary) {
                lines[i].added = (gsize)d->new_file.size;
                lines[i].deleted = (gsize)d->old_file.size;
                gchar* bin = g_strdup_printf("Bin %" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT " bytes",
                                             lines[i].deleted, lines[i].added);
                bin_width = MAX(bin_width, (gint)strlen(bin));
                g_free(bin);
            } else {
                size_t adds = 0, dels = 0;
                git_patch_line_stats(NULL, &adds, &dels, patch);
                lines[i].added = adds;
                lines[i].deleted = dels;
                insertions += adds;
                deletions += dels;
                max_change = MAX(max_change, adds + dels);
            }
            git_patch_free(patch);
        }
    }

    gint number_width = decimal_width(max_change);
    if (bin_width > 0) {
        number_width = MAX(number_width, 3); /* "Bin" */
    }
    gint width = MAX(80, 16 + 6 + number_width);
    gint graph_width = (gint)max_change + 4 > bin_width ? (gint)max_change : bin_width - 4;
    gint name_width = max_len;
    if (name_width + number_width + 6 + graph_width > width) {
        if (graph_width > width * 3 / 8 - number_width - 6) {
            graph_width = MAX(width * 3 / 8 - number_width - 6, 6);
        }
        if (name_width > width - number_width - 6 - graph_width) {
            name_width = width - number_width - 6 - graph_width;
        } else {
            graph_width = width - number_width - 6 - name_width;
        }
    }

    for (gsize i = 0; i < n; i++) {
        const StatLine* l = &lines[i];
        /* A name too long keeps its end, from a slash: ".../dir/file.c" */
        const gchar* name = l->name;
        const gchar* prefix = "";
        gint len = name_width;
        glong name_len = g_utf8_strlen(name, -1);
        if (name_width < name_len) {
            prefix = "...";
            len = MAX(len - 3, 0);
            while (name_len > len) {
                name = g_utf8_next_char(name);
                name_len--;
            }
            const gchar* slash = strchr(name, '/');
            if (slash != NULL) {
                name = slash;
            }
        }
        gint padding = MAX(len - (gint)g_utf8_strlen(name, -1), 0);
        g_string_append_printf(out, " %s%s%*s |", prefix, name, padding, "");
        if (l->binary) {
            g_string_append_printf(out, " %*s", number_width, "Bin");
            if (l->added != 0 || l->deleted != 0) {
                g_string_append_printf(out, " %" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT " bytes",
                                       l->deleted, l->added);
            }
            g_string_append_c(out, '\n');
            continue;
        }
        gsize add = l->added, del = l->deleted;
        if (graph_width <= (gint)max_change) {
            gsize total = scale_linear(add + del, graph_width, max_change);
            if (total < 2 && add > 0 && del > 0) {
                total = 2;
            }
            if (add < del) {
                add = scale_linear(add, graph_width, max_change);
                del = total - add;
            } else {
                del = scale_linear(del, graph_width, max_change);
                add = total - del;
            }
        }
        g_string_append_printf(out, " %*" G_GSIZE_FORMAT "%s", number_width, l->added + l->deleted,
                               l->added + l->deleted > 0 ? " " : "");
        for (gsize k = 0; k < add; k++) {
            g_string_append_c(out, '+');
        }
        for (gsize k = 0; k < del; k++) {
            g_string_append_c(out, '-');
        }
        g_string_append_c(out, '\n');
    }

    g_string_append_printf(out, " %" G_GSIZE_FORMAT " file%s changed", n, n == 1 ? "" : "s");
    if (insertions > 0 || deletions == 0) {
        append_count(out, insertions, "insertion", "+");
    }
    if (deletions > 0 || insertions == 0) {
        append_count(out, deletions, "deletion", "-");
    }
    g_string_append_c(out, '\n');

    for (gsize i = 0; i < n; i++) {
        g_free(lines[i].name);
    }
    g_free(lines);
}

/* git_diff_print() callback: the patch text as git prints it (hunk headers
 * without trailing blanks, no "---" / "+++" for files empty on both sides) */
static int append_patch_line(const git_diff_delta* delta, const git_diff_hunk* hunk, const git_diff_line* line,
                             void* data) {
    (void)hunk;
    GString* out = data;
    switch (line->origin) {
    case GIT_DIFF_LINE_CONTEXT:
    case GIT_DIFF_LINE_ADDITION:
    case GIT_DIFF_LINE_DELETION:
        g_string_append_c(out, line->origin);
        g_string_append_len(out, line->content, (gssize)line->content_len);
        break;
    case GIT_DIFF_LINE_HUNK_HDR: {
        gsize len = line->content_len;
        while (len > 0 && g_ascii_isspace(line->content[len - 1])) {
            len--;
        }
        g_string_append_len(out, line->content, (gssize)len);
        g_string_append_c(out, '\n');
        break;
    }
    case GIT_DIFF_LINE_FILE_HDR:
        if (delta->old_file.size == 0 && delta->new_file.size == 0) {
            /* No content on either side: git prints no "---" / "+++" lines */
            gchar* hdr = g_strndup(line->content, line->content_len);
            gchar** rows = g_strsplit(hdr, "\n", -1);
            for (gint i = 0; rows[i] != NULL && rows[i][0] != '\0'; i++) {
                if (!g_str_has_prefix(rows[i], "--- ") && !g_str_has_prefix(rows[i], "+++ ")) {
                    g_string_append_printf(out, "%s\n", rows[i]);
                }
            }
            g_strfreev(rows);
            g_free(hdr);
            break;
        }
        g_string_append_len(out, line->content, (gssize)line->content_len);
        break;
    default:
        g_string_append_len(out, line->content, (gssize)line->content_len);
        break;
    }
    return 0;
}

/* "---", diffstat then patch of commit c against its first parent (the empty
 * tree for a root commit) */
static void append_commit_diff(GString* out, git_repository* repo, git_commit* c) {
    git_tree* tree = NULL;
    git_tree* parent_tree = NULL;
    git_commit* parent = NULL;
    git_diff* diff = NULL;

    if (git_commit_tree(&tree, c) != 0) {
        return;
    }
    if (git_commit_parentcount(c) > 0 && git_commit_parent(&parent, c, 0) == 0) {
        git_commit_tree(&parent_tree, parent);
    }
    git_diff_options opts;
    git_diff_options_init(&opts, GIT_DIFF_OPTIONS_VERSION);
    opts.flags |= GIT_DIFF_INDENT_HEURISTIC; /* git's default since 2.14 */
    if (git_diff_tree_to_tree(&diff, repo, parent_tree, tree, &opts) == 0) {
        git_diff_find_options find;
        git_diff_find_options_init(&find, GIT_DIFF_FIND_OPTIONS_VERSION);
        find.flags = GIT_DIFF_FIND_RENAMES;
        git_diff_find_similar(diff, &find);

        if (git_diff_num_deltas(diff) > 0) {
            g_string_append(out, "---\n");
            append_diffstat(out, diff);
            g_string_append_c(out, '\n');
            git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, append_patch_line, out);
        }
    }
    git_diff_free(diff);
    git_tree_free(parent_tree);
    git_commit_free(parent);
    git_tree_free(tree);
}

gchar* git_show_commit(const gchar* root, const gchar* hash) {
    if (hash == NULL || hash[0] == '\0') {
        return NULL;
    }
    git_repository* repo = open_work_tree(root);
    if (repo == NULL) {
        return NULL;
    }
    git_object* obj = NULL;
    git_object* commit = NULL;
    gchar* text = NULL;
    if (git_revparse_single(&obj, repo, hash) == 0 && git_object_peel(&commit, obj, GIT_OBJECT_COMMIT) == 0) {
        GString* out = g_string_new(NULL);
        append_commit_header(out, (git_commit*)commit);
        append_commit_diff(out, repo, (git_commit*)commit);
        text = g_utf8_make_valid(out->str, (gssize)out->len);
        g_string_free(out, TRUE);
    }
    git_object_free(commit);
    git_object_free(obj);
    git_repository_free(repo);
    return text;
}

gboolean git_list_files(const gchar* root, gboolean with_ignored, GitFileFunc func, gpointer data) {
    git_repository* repo = open_work_tree(root);
    if (repo == NULL) {
        return FALSE;
    }
    git_index* index = NULL;
    if (git_repository_index(&index, repo) != 0) {
        git_repository_free(repo);
        return FALSE;
    }

    /* A file in conflict has an index entry per side: listed once */
    GHashTable* seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gboolean go_on = TRUE;
    gsize count = git_index_entrycount(index);
    for (gsize i = 0; go_on && i < count; i++) {
        const git_index_entry* e = git_index_get_byindex(index, i);
        if (e != NULL && g_hash_table_add(seen, g_strdup(e->path))) {
            go_on = func(e->path, data);
        }
    }
    git_index_free(index);

    git_status_list* list = NULL;
    git_status_options opts;
    git_status_options_init(&opts, GIT_STATUS_OPTIONS_VERSION);
    opts.show = GIT_STATUS_SHOW_WORKDIR_ONLY;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
                 GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
    if (with_ignored) {
        opts.flags |= GIT_STATUS_OPT_INCLUDE_IGNORED | GIT_STATUS_OPT_RECURSE_IGNORED_DIRS;
    }
    if (go_on && git_status_list_new(&list, repo, &opts) == 0) {
        count = git_status_list_entrycount(list);
        for (gsize i = 0; go_on && i < count; i++) {
            const git_status_entry* e = git_status_byindex(list, i);
            if (!(e->status & (GIT_STATUS_WT_NEW | GIT_STATUS_IGNORED)) || e->index_to_workdir == NULL) {
                continue;
            }
            const gchar* rel = e->index_to_workdir->new_file.path;
            if (rel != NULL && g_hash_table_add(seen, g_strdup(rel))) {
                go_on = func(rel, data);
            }
        }
        git_status_list_free(list);
    }
    g_hash_table_destroy(seen);
    git_repository_free(repo);
    return TRUE;
}
