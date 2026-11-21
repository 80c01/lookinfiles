#ifndef PROC_QUERY_H
#define PROC_QUERY_H

/* The filter of the System Monitor's process list, as typed in its field:
 * words separated by spaces, and conditions on the values.
 *   - A word (any case) must be in the name, the user or the PID.
 *   - A condition is a field, an operator and a number, without spaces:
 *     cpu>10 (also cpu>10%), mem>100M, threads>=50; operators > >= < <=.
 *     Memory: K, M, G, T (powers of 1000, as shown; a trailing B or o is
 *     allowed: 100MB, 100Mo), no unit counts in MB. A comma may be the
 *     decimal point (1,5G).
 *   - A token that starts like a condition but is not one yet (cpu>,
 *     mem>1X) is left out, so that the list does not empty itself while the
 *     condition is being typed. */
#include <glib.h>

typedef struct ProcQuery ProcQuery;

/* The values of a process, as tested */
typedef struct {
    const gchar* name;
    const gchar* user;
    gint pid;
    gdouble cpu;    /* percent, 100 for one full core */
    guint64 rss;    /* bytes */
    guint threads;
} ProcQueryRow;

/* NULL when text holds no word and no condition (every process) */
ProcQuery* proc_query_parse(const gchar* text);
void proc_query_free(ProcQuery* q);

/* The query holds conditions on values (they change at each reading) */
gboolean proc_query_has_conditions(const ProcQuery* q);
/* Every word is in the name, user or PID (TRUE for a NULL query) */
gboolean proc_query_match_words(const ProcQuery* q, const ProcQueryRow* row);
/* Every condition holds (TRUE for a NULL query) */
gboolean proc_query_match_values(const ProcQuery* q, const ProcQueryRow* row);

#endif /* PROC_QUERY_H */
