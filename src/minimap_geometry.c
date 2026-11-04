#include "minimap_geometry.h"

static gdouble max_value(gdouble upper, gdouble page) {
    return MAX(0.0, upper - page);
}

gboolean minimap_geometry_compute_fixed(gdouble upper, gdouble page, gdouble value,
                                        gdouble height, gdouble scale, MinimapGeometry* g) {
    *g = (MinimapGeometry){0};
    if (upper <= 0.0 || height <= 0.0 || scale <= 0.0) {
        return FALSE;
    }
    g->scale = scale;
    g->content = upper * g->scale;
    g->slider_height = MIN(page * g->scale, height);

    gdouble range = max_value(upper, page);
    gdouble ratio = (range > 0.0) ? CLAMP(value / range, 0.0, 1.0) : 0.0;
    /* Content taller than the minimap: scroll it with the view */
    g->offset = (g->content > height) ? ratio * (g->content - height) : 0.0;
    g->slider_top = CLAMP(value, 0.0, range) * g->scale - g->offset;
    return TRUE;
}

gboolean minimap_geometry_compute(gdouble upper, gdouble page, gdouble value,
                                  gdouble height, MinimapGeometry* g) {
    if (upper <= 0.0 || height <= 0.0) {
        *g = (MinimapGeometry){0};
        return FALSE;
    }
    gdouble scale = CLAMP(height / upper, MINIMAP_MIN_SCALE, MINIMAP_MAX_SCALE);
    return minimap_geometry_compute_fixed(upper, page, value, height, scale, g);
}

gdouble minimap_geometry_value_for_slider(const MinimapGeometry* g, gdouble upper,
                                          gdouble page, gdouble height, gdouble slider_top) {
    /* The slider top moves linearly over [0, track] while the view scrolls
     * over [0, upper - page] */
    gdouble track = MIN(g->content, height) - g->slider_height;
    if (track <= 0.0) {
        return 0.0;
    }
    return CLAMP(slider_top / track, 0.0, 1.0) * max_value(upper, page);
}

gdouble minimap_geometry_value_centered(const MinimapGeometry* g, gdouble upper,
                                        gdouble page, gdouble y) {
    if (g->scale <= 0.0) {
        return 0.0;
    }
    return CLAMP((y + g->offset) / g->scale - page / 2.0, 0.0, max_value(upper, page));
}
