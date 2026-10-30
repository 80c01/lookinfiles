/* Application colours by role (see palette.h). */
#include "palette.h"

#include <string.h>

typedef struct {
    const gchar* name;
    const gchar* value;
    const gchar* description;
} RoleInfo;

#define PALETTE_INFO_(id, name, value, desc) {name, value, desc},
static const RoleInfo ROLES[PAL_COUNT] = {PALETTE_ROLES(PALETTE_INFO_)};
#undef PALETTE_INFO_

/* Current values; an empty entry means the default */
static gchar current[PAL_COUNT][8];

static gboolean valid_role(PaletteRole role) {
    return (gint)role >= 0 && role < PAL_COUNT;
}

const gchar* palette_color(PaletteRole role) {
    g_return_val_if_fail(valid_role(role), "#ff00ff");
    return current[role][0] != '\0' ? current[role] : ROLES[role].value;
}

void palette_rgb(PaletteRole role, double* r, double* g, double* b) {
    guint v = (guint)g_ascii_strtoull(palette_color(role) + 1, NULL, 16);
    *r = ((v >> 16) & 0xff) / 255.0;
    *g = ((v >> 8) & 0xff) / 255.0;
    *b = (v & 0xff) / 255.0;
}

const gchar* palette_name(PaletteRole role) {
    g_return_val_if_fail(valid_role(role), NULL);
    return ROLES[role].name;
}

const gchar* palette_default(PaletteRole role) {
    g_return_val_if_fail(valid_role(role), NULL);
    return ROLES[role].value;
}

const gchar* palette_description(PaletteRole role) {
    g_return_val_if_fail(valid_role(role), NULL);
    return ROLES[role].description;
}

gboolean palette_lookup(const gchar* name, PaletteRole* role) {
    if (name == NULL) {
        return FALSE;
    }
    for (gint i = 0; i < PAL_COUNT; i++) {
        if (strcmp(ROLES[i].name, name) == 0) {
            if (role != NULL) {
                *role = (PaletteRole)i;
            }
            return TRUE;
        }
    }
    return FALSE;
}

gboolean palette_parse_color(const gchar* value, gchar out[8]) {
    if (value == NULL || value[0] != '#') {
        return FALSE;
    }
    gsize len = strlen(value + 1);
    if (len != 3 && len != 6) {
        return FALSE;
    }
    for (gsize i = 1; i <= len; i++) {
        if (!g_ascii_isxdigit(value[i])) {
            return FALSE;
        }
    }
    out[0] = '#';
    for (gsize i = 0; i < 6; i++) {
        /* #abc is #aabbcc */
        gchar c = (len == 3) ? value[1 + i / 2] : value[1 + i];
        out[1 + i] = g_ascii_tolower(c);
    }
    out[7] = '\0';
    return TRUE;
}

gboolean palette_set(PaletteRole role, const gchar* value) {
    g_return_val_if_fail(valid_role(role), FALSE);
    return palette_parse_color(value, current[role]);
}

void palette_reset(void) {
    memset(current, 0, sizeof current);
}

gchar* palette_css_defines(void) {
    GString* css = g_string_new(NULL);
    for (gint i = 0; i < PAL_COUNT; i++) {
        g_string_append_printf(css, "@define-color %s %s;\n", ROLES[i].name, palette_color((PaletteRole)i));
    }
    return g_string_free(css, FALSE);
}

static gboolean name_char(gchar c) {
    return g_ascii_isalnum(c) || c == '_';
}

gchar* palette_expand(const gchar* text) {
    g_return_val_if_fail(text != NULL, NULL);
    GString* out = g_string_sized_new(strlen(text));
    const gchar* p = text;
    while (*p != '\0') {
        if (*p == '@') {
            const gchar* end = p + 1;
            while (name_char(*end)) {
                end++;
            }
            gchar* name = g_strndup(p + 1, (gsize)(end - p - 1));
            PaletteRole role;
            gboolean known = palette_lookup(name, &role);
            g_free(name);
            if (known) {
                g_string_append(out, palette_color(role));
                p = end;
                continue;
            }
        }
        g_string_append_c(out, *p);
        p++;
    }
    return g_string_free(out, FALSE);
}
