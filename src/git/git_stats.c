/* Statistics of the commits of a repository (see git_stats.h) */
#include "git/git_stats.h"
#include "git/git_lib.h"

/* Commits looked at between two checks of the cancellable */
#define STATS_CANCEL_STEP 256

static void author_free(gpointer data) {
    GitAuthorStats* a = data;
    g_free(a->name);
    g_free(a);
}

GitStats* git_stats_new(void) {
    GitStats* s = g_new0(GitStats, 1);
    s->months = g_array_new(FALSE, TRUE, sizeof(guint));
    s->days = g_hash_table_new(g_direct_hash, g_direct_equal);
    s->authors = g_ptr_array_new_with_free_func(author_free);
    s->by_author = g_hash_table_new(g_str_hash, g_str_equal);
    return s;
}

void git_stats_free(GitStats* s) {
    if (s == NULL) return;
    g_array_unref(s->months);
    g_hash_table_unref(s->days);
    g_hash_table_unref(s->by_author);
    g_ptr_array_unref(s->authors);
    g_free(s);
}

gint git_stats_day_of(gint64 time, gint offset_minutes) {
    gint64 local = time + (gint64)offset_minutes * 60;
    gint64 day = local / 86400;
    if (local % 86400 < 0) day--; /* before 1970: rounded down */
    return (gint)day;
}

gint git_stats_weekday(gint day) {
    gint w = (day + 3) % 7; /* 1970-01-01 was a Thursday */
    return w < 0 ? w + 7 : w;
}

guint git_stats_on_day(const GitStats* s, gint day) {
    return GPOINTER_TO_UINT(g_hash_table_lookup(s->days, GINT_TO_POINTER(day)));
}

/* Count one commit in month (year * 12 + month - 1), growing the months
 * array on either side */
static void add_month(GitStats* s, gint month) {
    if (s->months->len == 0) {
        s->first_month = month;
        g_array_set_size(s->months, 1);
    } else if (month < s->first_month) {
        guint before = (guint)(s->first_month - month);
        guint zero = 0;
        for (guint i = 0; i < before; i++) g_array_prepend_val(s->months, zero);
        s->first_month = month;
    } else if ((guint)(month - s->first_month) >= s->months->len) {
        g_array_set_size(s->months, (guint)(month - s->first_month) + 1);
    }
    g_array_index(s->months, guint, (guint)(month - s->first_month))++;
}

void git_stats_add(GitStats* s, gint64 time, gint offset_minutes, const gchar* author) {
    GDateTime* t = g_date_time_new_from_unix_utc(time + (gint64)offset_minutes * 60);
    if (t == NULL) return;
    gint weekday = g_date_time_get_day_of_week(t) - 1; /* 1: Monday */
    gint hour = g_date_time_get_hour(t);
    gint month = g_date_time_get_year(t) * 12 + g_date_time_get_month(t) - 1;
    g_date_time_unref(t);

    s->commits++;
    if (s->commits == 1 || time < s->first) s->first = time;
    if (s->commits == 1 || time > s->last) s->last = time;
    s->punch[weekday][hour]++;
    add_month(s, month);
    gpointer day = GINT_TO_POINTER(git_stats_day_of(time, offset_minutes));
    g_hash_table_insert(s->days, day, GUINT_TO_POINTER(GPOINTER_TO_UINT(g_hash_table_lookup(s->days, day)) + 1));

    const gchar* name = author != NULL && author[0] != '\0' ? author : "?";
    GitAuthorStats* a = g_hash_table_lookup(s->by_author, name);
    if (a == NULL) {
        a = g_new0(GitAuthorStats, 1);
        a->name = g_strdup(name);
        a->first = time;
        a->last = time;
        g_ptr_array_add(s->authors, a);
        g_hash_table_insert(s->by_author, a->name, a);
    }
    a->commits++;
    a->first = MIN(a->first, time);
    a->last = MAX(a->last, time);
}

static gint by_commits(gconstpointer pa, gconstpointer pb) {
    const GitAuthorStats* a = *(GitAuthorStats* const*)pa;
    const GitAuthorStats* b = *(GitAuthorStats* const*)pb;
    if (a->commits != b->commits) return a->commits > b->commits ? -1 : 1;
    return g_utf8_collate(a->name, b->name);
}

void git_stats_finish(GitStats* s) {
    g_ptr_array_sort(s->authors, by_commits);
}

GitStats* git_stats_read(const gchar* root, GCancellable* cancellable) {
    git_repository* repo = git_lib_open(root);
    if (repo == NULL) return NULL;
    GitStats* s = git_stats_new();
    git_revwalk* walk = NULL;
    if (git_revwalk_new(&walk, repo) == 0) {
        git_oid oid;
        guint seen = 0;
        if (git_revwalk_push_head(walk) == 0) { /* fails without commits */
            while (git_revwalk_next(&oid, walk) == 0) {
                if (++seen % STATS_CANCEL_STEP == 0 && g_cancellable_is_cancelled(cancellable)) {
                    g_clear_pointer(&s, git_stats_free);
                    break;
                }
                git_commit* c = NULL;
                if (git_commit_lookup(&c, repo, &oid) != 0) continue;
                const git_signature* who = git_commit_author(c);
                if (who != NULL) {
                    gchar* name = g_utf8_make_valid(who->name != NULL ? who->name : "", -1);
                    git_stats_add(s, (gint64)who->when.time, who->when.offset, name);
                    g_free(name);
                }
                git_commit_free(c);
            }
        }
        git_revwalk_free(walk);
    }
    git_repository_free(repo);
    if (s != NULL && g_cancellable_is_cancelled(cancellable)) g_clear_pointer(&s, git_stats_free);
    if (s != NULL) git_stats_finish(s);
    return s;
}
