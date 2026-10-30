/* Application fonts by role (see fonts.h). */
#include "fonts.h"

#include <string.h>

typedef struct {
    const gchar* family;
    gint size;
} FontDefault;

static const FontDefault DEFAULTS[FONT_COUNT] = {
    [FONT_UI] = {"Segoe UI, Ubuntu, Cantarell, Noto Sans, sans-serif", 13},
    [FONT_CODE] = {"Consolas, DejaVu Sans Mono, Liberation Mono, Menlo, Courier New, monospace", 13},
};

/* Current values; NULL / 0 means the default */
static gchar* families[FONT_COUNT];
static gint sizes[FONT_COUNT];

static gboolean valid_role(FontRole role) {
    return (gint)role >= 0 && role < FONT_COUNT;
}

const gchar* fonts_family(FontRole role) {
    g_return_val_if_fail(valid_role(role), "sans-serif");
    return families[role] != NULL ? families[role] : DEFAULTS[role].family;
}

gint fonts_size(FontRole role) {
    g_return_val_if_fail(valid_role(role), 13);
    return sizes[role] > 0 ? sizes[role] : DEFAULTS[role].size;
}

/* A family list: names of letters, digits, spaces, '-' and '_', separated
 * by commas, none empty (nothing that could break CSS or markup) */
static gboolean valid_family(const gchar* family) {
    gchar** names = g_strsplit(family, ",", -1);
    gboolean ok = names[0] != NULL;
    for (gint i = 0; ok && names[i] != NULL; i++) {
        gchar* name = g_strstrip(g_strdup(names[i]));
        ok = name[0] != '\0';
        for (const gchar* p = name; ok && *p != '\0'; p++) {
            ok = g_ascii_isalnum(*p) || *p == ' ' || *p == '-' || *p == '_';
        }
        g_free(name);
    }
    g_strfreev(names);
    return ok;
}

gboolean fonts_set(FontRole role, const gchar* family, gint size_px) {
    g_return_val_if_fail(valid_role(role), FALSE);
    if ((family != NULL && !valid_family(family)) || (size_px != 0 && (size_px < 6 || size_px > 72))) {
        return FALSE;
    }
    if (family != NULL) {
        g_free(families[role]);
        families[role] = g_strdup(family);
    }
    if (size_px != 0) {
        sizes[role] = size_px;
    }
    return TRUE;
}

void fonts_reset(void) {
    for (gint i = 0; i < FONT_COUNT; i++) {
        g_clear_pointer(&families[i], g_free);
        sizes[i] = 0;
    }
}

static gboolean is_generic(const gchar* name) {
    static const gchar* const GENERIC[] = {"sans-serif", "serif", "monospace", "cursive", "fantasy", "system-ui"};
    for (gsize i = 0; i < G_N_ELEMENTS(GENERIC); i++) {
        if (g_ascii_strcasecmp(name, GENERIC[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

gchar* fonts_css_family(FontRole role) {
    gchar** names = g_strsplit(fonts_family(role), ",", -1);
    GString* css = g_string_new(NULL);
    for (gint i = 0; names[i] != NULL; i++) {
        gchar* name = g_strstrip(names[i]);
        if (css->len > 0) {
            g_string_append(css, ", ");
        }
        if (is_generic(name)) {
            g_string_append(css, name);
        } else {
            g_string_append_printf(css, "'%s'", name);
        }
    }
    g_strfreev(names);
    return g_string_free(css, FALSE);
}

/* Value of a token, NULL if unknown */
static gchar* token_value(const gchar* name) {
    gint ui = fonts_size(FONT_UI);
    if (strcmp(name, "font_ui") == 0) return fonts_css_family(FONT_UI);
    if (strcmp(name, "font_code") == 0) return fonts_css_family(FONT_CODE);
    if (strcmp(name, "size_small") == 0) return g_strdup_printf("%dpx", MAX(ui - 2, 6));
    if (strcmp(name, "size_bar") == 0) return g_strdup_printf("%dpx", MAX(ui - 1, 6));
    if (strcmp(name, "size_normal") == 0) return g_strdup_printf("%dpx", ui);
    if (strcmp(name, "size_title") == 0) return g_strdup_printf("%dpx", (ui * 6 + 2) / 5);
    if (strcmp(name, "size_code") == 0) return g_strdup_printf("%dpx", fonts_size(FONT_CODE));
    return NULL;
}

gchar* fonts_expand(const gchar* css) {
    g_return_val_if_fail(css != NULL, NULL);
    GString* out = g_string_sized_new(strlen(css));
    const gchar* p = css;
    while (*p != '\0') {
        if (*p == '@') {
            const gchar* end = p + 1;
            while (g_ascii_isalnum(*end) || *end == '_') {
                end++;
            }
            gchar* name = g_strndup(p + 1, (gsize)(end - p - 1));
            gchar* value = token_value(name);
            g_free(name);
            if (value != NULL) {
                g_string_append(out, value);
                g_free(value);
                p = end;
                continue;
            }
        }
        g_string_append_c(out, *p);
        p++;
    }
    return g_string_free(out, FALSE);
}
