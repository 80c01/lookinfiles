/* Text tags that follow the theme (see text_tags.h). */
#include "theme/text_tags.h"
#include "theme/fonts.h"
#include "theme/palette.h"

#include <gobject/gvaluecollector.h>
#include <string.h>

#define BINDINGS_KEY "text-tags-bindings"

/* Property set from a role */
typedef struct {
    gchar* property;
    gchar* role; /* "@accent" */
} Binding;

static void binding_free(gpointer data) {
    Binding* b = data;
    g_free(b->property);
    g_free(b->role);
    g_free(b);
}

/* Tags with bindings (weak references: dropped when finalized) */
static GPtrArray* bound_tags = NULL;

static void on_tag_finalized(gpointer data, GObject* where) {
    (void)data;
    if (bound_tags != NULL) {
        g_ptr_array_remove_fast(bound_tags, where);
    }
}

const gchar* text_tags_resolve(const gchar* s) {
    if (s == NULL || s[0] != '@') {
        return NULL;
    }
    if (strcmp(s, "@font_code") == 0) return fonts_family(FONT_CODE);
    if (strcmp(s, "@font_ui") == 0) return fonts_family(FONT_UI);
    if (strcmp(s, "@font_serif") == 0) return "serif";
    PaletteRole role;
    if (palette_lookup(s + 1, &role)) {
        return palette_color(role);
    }
    return NULL;
}

/* Set a string-valued property (string or GdkRGBA) from a value */
static void set_string_property(GtkTextTag* tag, GParamSpec* pspec, const gchar* value) {
    if (G_PARAM_SPEC_VALUE_TYPE(pspec) == GDK_TYPE_RGBA) {
        GdkRGBA rgba;
        if (value != NULL && gdk_rgba_parse(&rgba, value)) {
            g_object_set(tag, pspec->name, &rgba, NULL);
        }
    } else {
        g_object_set(tag, pspec->name, value, NULL);
    }
}

static void bind(GtkTextTag* tag, const gchar* property, const gchar* role) {
    GPtrArray* bindings = g_object_get_data(G_OBJECT(tag), BINDINGS_KEY);
    if (bindings == NULL) {
        bindings = g_ptr_array_new_with_free_func(binding_free);
        g_object_set_data_full(G_OBJECT(tag), BINDINGS_KEY, bindings, (GDestroyNotify)g_ptr_array_unref);
        if (bound_tags == NULL) {
            bound_tags = g_ptr_array_new();
        }
        g_ptr_array_add(bound_tags, tag);
        g_object_weak_ref(G_OBJECT(tag), on_tag_finalized, NULL);
    }
    for (guint i = 0; i < bindings->len; i++) {
        Binding* b = g_ptr_array_index(bindings, i);
        if (strcmp(b->property, property) == 0) {
            g_free(b->role);
            b->role = g_strdup(role);
            return;
        }
    }
    Binding* b = g_new0(Binding, 1);
    b->property = g_strdup(property);
    b->role = g_strdup(role);
    g_ptr_array_add(bindings, b);
}

/* Set the properties of a va_list (like g_object_set_valist), resolving
 * and remembering role values */
static void set_valist(GtkTextTag* tag, const gchar* first_property, va_list args) {
    GObjectClass* klass = G_OBJECT_GET_CLASS(tag);
    for (const gchar* name = first_property; name != NULL; name = va_arg(args, const gchar*)) {
        GParamSpec* pspec = g_object_class_find_property(klass, name);
        if (pspec == NULL) {
            g_warning("text_tag_create: no property \"%s\"", name);
            return;
        }
        GType type = G_PARAM_SPEC_VALUE_TYPE(pspec);
        if (type == G_TYPE_STRING || type == GDK_TYPE_RGBA) {
            const gchar* value = va_arg(args, const gchar*);
            const gchar* resolved = text_tags_resolve(value);
            if (resolved != NULL) {
                bind(tag, name, value);
            }
            set_string_property(tag, pspec, resolved != NULL ? resolved : value);
            continue;
        }
        GValue value = G_VALUE_INIT;
        gchar* error = NULL;
        G_VALUE_COLLECT_INIT(&value, type, args, 0, &error);
        if (error != NULL) {
            g_warning("text_tag_create: %s", error);
            g_free(error);
            return;
        }
        g_object_set_property(G_OBJECT(tag), name, &value);
        g_value_unset(&value);
    }
}

GtkTextTag* text_tag_create(GtkTextBuffer* buffer, const gchar* name, const gchar* first_property, ...) {
    GtkTextTag* tag = gtk_text_buffer_create_tag(buffer, name, NULL);
    va_list args;
    va_start(args, first_property);
    set_valist(tag, first_property, args);
    va_end(args);
    return tag;
}

GtkTextTag* text_tag_ensure(GtkTextBuffer* buffer, const gchar* name, const gchar* first_property, ...) {
    GtkTextTag* tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name);
    if (tag != NULL) {
        return tag;
    }
    tag = gtk_text_buffer_create_tag(buffer, name, NULL);
    va_list args;
    va_start(args, first_property);
    set_valist(tag, first_property, args);
    va_end(args);
    return tag;
}

void text_tags_refresh_all(void) {
    for (guint i = 0; bound_tags != NULL && i < bound_tags->len; i++) {
        GtkTextTag* tag = g_ptr_array_index(bound_tags, i);
        GPtrArray* bindings = g_object_get_data(G_OBJECT(tag), BINDINGS_KEY);
        GObjectClass* klass = G_OBJECT_GET_CLASS(tag);
        for (guint j = 0; bindings != NULL && j < bindings->len; j++) {
            Binding* b = g_ptr_array_index(bindings, j);
            GParamSpec* pspec = g_object_class_find_property(klass, b->property);
            const gchar* value = text_tags_resolve(b->role);
            if (pspec != NULL && value != NULL) {
                set_string_property(tag, pspec, value);
            }
        }
    }
}
