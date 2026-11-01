/* Icon buttons and labels from the GTK icon theme (see icons.h).
 *
 * The icons are bundled in the binary (icons/, see icons/COPYING) so they look
 * the same, and render, on every system: some icon themes (e.g. elementary-xfce)
 * ship symbolic icons that GTK 4.22 draws invisible. If SVG icons cannot be
 * loaded at all (old GTK without librsvg), each name falls back to the icon
 * theme, then to an icon built into libgtk itself (pre-rendered PNG). */
#include "icons.h"
#include "file_type_icons.h"

typedef struct {
    const gchar* name;          /* name used by callers */
    const gchar* fallbacks[4];  /* tried in order; the last one is built into GTK */
} IconFallback;

/* Built into libgtk 4 (always available): folder, folder-new, bookmark-new,
 * edit-delete, user-trash, edit-clear-all, list-add, view-refresh, view-reveal,
 * pan-up, network-server, network-workgroup, window-close, go-next,
 * document-open-recent, text-x-generic, dialog-question, view-list, view-grid,
 * system-run. Icons used directly from that list need no entry here. */
static const IconFallback FALLBACKS[] = {
    {"user-bookmarks", {"bookmark-new", NULL}},
    {"document-new", {"text-x-generic", NULL}},
    {"document-edit", {"edit", "text-editor", "text-x-generic", NULL}},
    {"document-send", {"go-next", NULL}},
    {"network-transmit-receive", {"network-workgroup", NULL}},
    {"utilities-terminal", {"terminal", "system-run", NULL}},
    {"document-print-preview", {"text-x-generic", NULL}},
    {"view-dual", {"view-grid", NULL}},
    {"view-paged", {"view-list", NULL}},
    {"edit-find", {"system-search", NULL}},
    {"applications-system", {"emblem-system", "system-run", NULL}},
    {"go-previous", {"pan-start", "pan-up", NULL}},
    {"go-home", {"user-home", "folder", NULL}},
};

/* TRUE when gdk-pixbuf can load SVG (librsvg loader). Without it, the bundled
 * SVG icons are still "found" in the resource path but render as missing
 * images, so they must not be used. */
static gboolean svg_supported(void) {
    static gint cached = -1;
    if (cached < 0) {
        cached = 0;
        GSList* formats = gdk_pixbuf_get_formats();
        for (GSList* l = formats; l != NULL; l = l->next) {
            gchar* fmt = gdk_pixbuf_format_get_name((GdkPixbufFormat*)l->data);
            if (g_strcmp0(fmt, "svg") == 0) {
                cached = 1;
            }
            g_free(fmt);
        }
        g_slist_free(formats);
    }
    return cached == 1;
}

/* LOOKIN_DEBUG_ICONS=1: print how each icon is resolved (to diagnose
 * missing icons on a given system) */
static void debug_print_icon(GdkDisplay* display, const gchar* requested, GIcon* icon) {
    static gboolean header_done = FALSE;
    GtkIconTheme* theme = gtk_icon_theme_get_for_display(display);
    if (!header_done) {
        header_done = TRUE;
        gboolean svg = svg_supported();
        gchar* theme_name = gtk_icon_theme_get_theme_name(theme);
        g_printerr("[icons] GTK %u.%u.%u, icon theme \"%s\", SVG loader: %s\n",
                   gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
                   theme_name ? theme_name : "?", svg ? "yes" : "NO");
        g_free(theme_name);
    }
    GtkIconPaintable* p = gtk_icon_theme_lookup_by_gicon(theme, icon, 16, 1, GTK_TEXT_DIR_LTR, 0);
    GFile* file = p ? gtk_icon_paintable_get_file(p) : NULL;
    gchar* where = file ? g_file_get_uri(file) : g_strdup("(none)");
    g_printerr("[icons] %-26s -> %s\n", requested, where);
    g_free(where);
    if (file) g_object_unref(file);
    if (p) g_object_unref(p);
}

/* Make the icons bundled in the binary (icons/, prefix "lookin-")
 * visible to the icon theme of the display, once */
static void register_bundled_icons(GdkDisplay* display) {
    static GdkDisplay* registered = NULL;
    if (display != NULL && display != registered) {
        gtk_icon_theme_add_resource_path(gtk_icon_theme_get_for_display(display),
                                         "/org/lookin/icons");
        registered = display;
    }
}

static GIcon* themed_icon_with_fallbacks(const gchar* icon_name) {
    GdkDisplay* display = gdk_display_get_default();
    register_bundled_icons(display);

    /* 1. Bundled copy, requested alone: GTK searches every installed theme for
     *    all names of a GThemedIcon before the bundled resource path, so mixing
     *    it with theme names would let the theme win. Its unique "lookin-"
     *    name cannot match any theme. Only when SVG can be loaded. */
    gchar* bundled = g_strconcat("lookin-", icon_name, "-symbolic", NULL);
    if (display != NULL && svg_supported() &&
        gtk_icon_theme_has_icon(gtk_icon_theme_get_for_display(display), bundled)) {
        GIcon* icon = g_themed_icon_new(bundled);
        g_free(bundled);
        if (g_getenv("LOOKIN_DEBUG_ICONS") != NULL) {
            debug_print_icon(display, icon_name, icon);
        }
        return icon;
    }
    g_free(bundled);

    /* 2. Same name in the icon theme, then fallbacks ending with a GTK built-in */
    GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(names, g_strconcat(icon_name, "-symbolic", NULL));
    for (gsize i = 0; i < G_N_ELEMENTS(FALLBACKS); i++) {
        if (g_strcmp0(FALLBACKS[i].name, icon_name) == 0) {
            for (gsize j = 0; j < G_N_ELEMENTS(FALLBACKS[i].fallbacks) && FALLBACKS[i].fallbacks[j]; j++) {
                g_ptr_array_add(names, g_strconcat(FALLBACKS[i].fallbacks[j], "-symbolic", NULL));
            }
            break;
        }
    }
    GIcon* icon = g_themed_icon_new_from_names((gchar**)names->pdata, (gint)names->len);

    /* Report an icon that no name of the chain can provide (caught by make ui-test) */
    if (display != NULL &&
        !gtk_icon_theme_has_gicon(gtk_icon_theme_get_for_display(display), icon)) {
        g_warning("icons: no icon available for \"%s\" in the current icon theme", icon_name);
    }
    if (display != NULL && g_getenv("LOOKIN_DEBUG_ICONS") != NULL) {
        debug_print_icon(display, icon_name, icon);
    }
    g_ptr_array_free(names, TRUE);
    return icon;
}

static GtkWidget* symbolic_image(const gchar* icon_name) {
    GIcon* icon = themed_icon_with_fallbacks(icon_name);
    GtkWidget* image = gtk_image_new_from_gicon(icon);
    g_object_unref(icon);
    return image;
}

GtkWidget* icon_label_new(const gchar* icon_name, const gchar* text) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), symbolic_image(icon_name));
    if (text != NULL) {
        gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    }
    return box;
}

void icon_button_set_content(GtkWidget* button, const gchar* icon_name, const gchar* text) {
    if (text == NULL) {
        gtk_button_set_child(GTK_BUTTON(button), symbolic_image(icon_name));
    } else {
        gtk_button_set_child(GTK_BUTTON(button), icon_label_new(icon_name, text));
    }
}

GtkWidget* icon_button_new(const gchar* icon_name, const gchar* tooltip) {
    GtkWidget* button = gtk_button_new();
    icon_button_set_content(button, icon_name, NULL);
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_set_focusable(button, FALSE);
    if (tooltip != NULL) {
        gtk_widget_set_tooltip_text(button, tooltip);
    }
    return button;
}

/* Bundled glyph of a file-type icon, cached by icon name. Without SVG
 * support, the generic file or folder icon built into GTK. */
static GIcon* file_type_gicon(const gchar* icon, gboolean is_dir) {
    static GHashTable* cache = NULL;
    if (cache == NULL) {
        cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    }
    GIcon* gicon = g_hash_table_lookup(cache, icon);
    if (gicon != NULL) {
        return gicon;
    }
    GdkDisplay* display = gdk_display_get_default();
    register_bundled_icons(display);
    gchar* bundled = g_strconcat("lookin-ft-", icon, "-symbolic", NULL);
    if (display != NULL && svg_supported() &&
        gtk_icon_theme_has_icon(gtk_icon_theme_get_for_display(display), bundled)) {
        gicon = g_themed_icon_new(bundled);
    } else {
        gicon = g_themed_icon_new(is_dir ? "folder-symbolic" : "text-x-generic-symbolic");
    }
    g_free(bundled);
    g_hash_table_insert(cache, g_strdup(icon), gicon);
    return gicon;
}

GtkWidget* file_type_image_new(void) {
    GtkWidget* image = gtk_image_new();
    gtk_widget_add_css_class(image, "file-type-icon");
    return image;
}

void file_type_image_set(GtkWidget* image, const gchar* name, gboolean is_dir) {
    FileTypeIcon ft = file_type_icon(name, is_dir);
    GIcon* gicon = file_type_gicon(ft.icon, is_dir);
    if (gtk_image_get_gicon(GTK_IMAGE(image)) != gicon) {
        gtk_image_set_from_gicon(GTK_IMAGE(image), gicon);
    }
    gchar* cls = g_strconcat("ft-", ft.color, NULL);
    const gchar* old = g_object_get_data(G_OBJECT(image), "ft-class");
    if (g_strcmp0(old, cls) == 0) {
        g_free(cls);
        return;
    }
    if (old != NULL) {
        gtk_widget_remove_css_class(image, old);
    }
    gtk_widget_add_css_class(image, cls);
    g_object_set_data_full(G_OBJECT(image), "ft-class", cls, g_free);
}

GtkWidget* file_type_label_new(const gchar* name, const gchar* text) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* image = file_type_image_new();
    file_type_image_set(image, name, FALSE);
    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    return box;
}
