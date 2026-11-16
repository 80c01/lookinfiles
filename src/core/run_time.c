#include "core/run_time.h"

gchar* run_time_format(gint64 usec) {
    if (usec < 0) usec = 0;
    gint64 tenths = usec / (G_USEC_PER_SEC / 10);
    if (tenths < 600) return g_strdup_printf("%" G_GINT64_FORMAT ".%" G_GINT64_FORMAT "s", tenths / 10, tenths % 10);
    gint64 seconds = usec / G_USEC_PER_SEC;
    if (seconds < 3600) {
        return g_strdup_printf("%" G_GINT64_FORMAT "m %02" G_GINT64_FORMAT "s", seconds / 60, seconds % 60);
    }
    gint64 minutes = seconds / 60;
    return g_strdup_printf("%" G_GINT64_FORMAT "h %02" G_GINT64_FORMAT "m", minutes / 60, minutes % 60);
}
