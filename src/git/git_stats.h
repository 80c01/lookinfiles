#ifndef GIT_STATS_H
#define GIT_STATS_H

/* Statistics of the commits of a repository: how many per day, per day of
 * the week and hour, per month and per author. Counted from each commit's
 * author date in the author's own time zone (the hour they worked at).
 * Read through libgit2 (no git program); no AppData and no widgets. */
#include <gio/gio.h>

typedef struct {
    gchar* name;    /* author name as written in the commits */
    guint commits;
    gint64 first;   /* date of the first and the last commit, seconds since the epoch */
    gint64 last;
} GitAuthorStats;

typedef struct {
    guint commits;
    gint64 first;          /* oldest and newest author dates (0 without commits) */
    gint64 last;
    guint punch[7][24];    /* [day of the week, 0: Monday][hour] */
    gint first_month;      /* year * 12 + month - 1 of months[0] */
    GArray* months;        /* guint commits per month, from first_month to the newest */
    GHashTable* days;      /* day number (git_stats_day_of()) -> commits */
    GPtrArray* authors;    /* GitAuthorStats*, most commits first after git_stats_finish() */
    GHashTable* by_author; /* name -> GitAuthorStats* (in authors) */
} GitStats;

GitStats* git_stats_new(void);
void git_stats_free(GitStats* stats);

/* Count a commit by author made at time (seconds since the epoch) in a time
 * zone offset_minutes east of UTC */
void git_stats_add(GitStats* stats, gint64 time, gint offset_minutes, const gchar* author);

/* After the last git_stats_add(): the authors sorted, most commits first
 * (then by name) */
void git_stats_finish(GitStats* stats);

/* Day number (days since 1970-01-01) of time in a time zone offset_minutes
 * east of UTC */
gint git_stats_day_of(gint64 time, gint offset_minutes);

/* Day of the week of a day number, 0: Monday ... 6: Sunday */
gint git_stats_weekday(gint day);

/* Commits on day (a day number) */
guint git_stats_on_day(const GitStats* stats, gint day);

/* The commits reachable from HEAD of the repository at root, counted; an
 * empty GitStats without commits, NULL when root is not a repository or
 * cancellable was cancelled. Worker thread. */
GitStats* git_stats_read(const gchar* root, GCancellable* cancellable);

#endif /* GIT_STATS_H */
