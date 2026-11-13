/* Icon buttons and labels from the GTK icon theme (see icons.h).
 *
 * The icons are bundled in the binary (icons/, see icons/COPYING) so they look
 * the same, and render, on every system: some icon themes (e.g. elementary-xfce)
 * ship symbolic icons that GTK 4.22 draws invisible. If SVG icons cannot be
 * loaded at all (old GTK without librsvg), each name falls back to the icon
 * theme, then to an icon built into libgtk itself (pre-rendered PNG). */
#include "theme/icons.h"
#include "theme/file_type_icons.h"
#include "theme/palette.h"

#include <string.h>

typedef struct {
    const gchar* name;          /* name used by callers */
    const gchar* fallbacks[4];  /* tried in order; the last one is built into GTK */
} IconFallback;

/* Built into libgtk 4 (always available): folder, folder-new, bookmark-new,
 * edit-delete, user-trash, edit-clear-all, list-add, view-refresh, view-reveal,
 * pan-up, pan-down, network-server, network-workgroup, window-close, go-next,
 * document-open-recent, text-x-generic, dialog-question, view-list, view-grid,
 * system-run. Icons used directly from that list need no entry here. */
static const IconFallback FALLBACKS[] = {
    {"user-bookmarks", {"bookmark-new", NULL}},
    {"document-new", {"text-x-generic", NULL}},
    {"document-edit", {"edit", "text-editor", "text-x-generic", NULL}},
    {"network-transmit-receive", {"network-workgroup", NULL}},
    {"utilities-terminal", {"terminal", "system-run", NULL}},
    {"utilities-system-monitor", {"power-profile-performance", "system-run", NULL}},
    {"document-print-preview", {"text-x-generic", NULL}},
    {"view-dual", {"view-grid", NULL}},
    {"view-paged", {"view-list", NULL}},
    {"edit-find", {"system-search", NULL}},
    {"applications-system", {"emblem-system", "system-run", NULL}},
    {"go-previous", {"pan-start", "pan-up", NULL}},
    {"go-home", {"user-home", "folder", NULL}},
    {"process-stop", {"window-close", NULL}},
    {"input-keyboard", {"preferences-desktop-keyboard", "text-x-generic", NULL}},
    /* Canvas tools and text formatting */
    {"canvas-select", {"edit-select", "object-select", NULL}},
    {"canvas-rectangle", {"media-playback-stop", NULL}},
    {"canvas-diamond", {"media-playback-stop", NULL}},
    {"canvas-ellipse", {"media-record", NULL}},
    {"canvas-arrow", {"go-next", NULL}},
    {"canvas-line", {"list-remove", NULL}},
    {"canvas-table", {"view-grid", NULL}},
    {"canvas-align-left", {"view-grid", NULL}},
    {"canvas-align-center-h", {"view-grid", NULL}},
    {"canvas-align-right", {"view-grid", NULL}},
    {"canvas-align-top", {"view-grid", NULL}},
    {"canvas-align-middle-v", {"view-grid", NULL}},
    {"canvas-align-bottom", {"view-grid", NULL}},
    {"canvas-distribute-h", {"view-grid", NULL}},
    {"canvas-distribute-v", {"view-grid", NULL}},
    {"canvas-bring-front", {"pan-up", NULL}},
    {"canvas-bring-forward", {"pan-up", NULL}},
    {"canvas-send-backward", {"pan-down", NULL}},
    {"canvas-send-back", {"pan-down", NULL}},
    {"canvas-lock", {"view-grid", NULL}},
    {"canvas-unlock", {"view-grid", NULL}},
    {"canvas-group", {"view-grid", NULL}},
    {"canvas-ungroup", {"view-grid", NULL}},
    {"canvas-crop", {"view-grid", NULL}},
    {"cell-add-above", {"list-add", NULL}},
    {"cell-add-below", {"list-add", NULL}},
    {"canvas-row-add-above", {"list-add", NULL}},
    {"canvas-row-add-below", {"list-add", NULL}},
    {"canvas-row-delete", {"list-remove", NULL}},
    {"canvas-column-add-left", {"list-add", NULL}},
    {"canvas-column-add-right", {"list-add", NULL}},
    {"canvas-column-delete", {"list-remove", NULL}},
    {"insert-text", {"text-x-generic", NULL}},
    {"format-text-bold", {"text-x-generic", NULL}},
    {"format-text-italic", {"text-x-generic", NULL}},
    {"format-text-underline", {"text-x-generic", NULL}},
    {"format-text-strikethrough", {"text-x-generic", NULL}},
    {"format-text-rich", {"text-x-generic", NULL}},
    {"view-list-bullet", {"view-list", NULL}},
    {"view-list-ordered", {"view-list", NULL}},
    {"format-indent-less", {"go-previous", NULL}},
    {"format-indent-more", {"go-next", NULL}},
    {"format-justify-center", {"text-x-generic", NULL}},
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

/* GThemedIcon for the bundled icon lookin-<name>-symbolic: the icon of the
 * icon set in use first (lookin-set-<name>-symbolic, drawn in the text
 * colour, or lookin-set-<name>, drawn as is, see icon_sets.h), then the
 * bundled one. Names the set does not provide are skipped by GTK, so a change
 * of set only needs the icon theme to notify its images. The symbolic name
 * must come first: GTK prefers a later symbolic name to an earlier plain one
 * the theme lacks. */
static GIcon* bundled_themed_icon(const gchar* name) {
    gchar* names[4] = {g_strconcat("lookin-set-", name, "-symbolic", NULL),
                       g_strconcat("lookin-set-", name, NULL),
                       g_strconcat("lookin-", name, "-symbolic", NULL), NULL};
    GIcon* icon = g_themed_icon_new_from_names(names, 3);
    for (gint i = 0; i < 3; i++) {
        g_free(names[i]);
    }
    return icon;
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
        GIcon* icon = bundled_themed_icon(icon_name);
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

GtkWidget* icon_image_new(const gchar* icon_name) {
    return symbolic_image(icon_name);
}

/* The content is updated in place when it has the same shape: a new child
 * under a still pointer would get the next click, which the button would
 * then miss */
void icon_button_set_content(GtkWidget* button, const gchar* icon_name, const gchar* text) {
    GtkWidget* child = gtk_button_get_child(GTK_BUTTON(button));
    GtkWidget* image = NULL;
    GtkWidget* label = NULL;
    if (text == NULL && GTK_IS_IMAGE(child)) {
        image = child;
    } else if (text != NULL && GTK_IS_BOX(child)) {
        image = gtk_widget_get_first_child(child);
        label = gtk_widget_get_last_child(child);
        if (!GTK_IS_IMAGE(image) || !GTK_IS_LABEL(label)) {
            image = NULL;
        }
    }
    if (image == NULL) {
        gtk_button_set_child(GTK_BUTTON(button), text == NULL ? symbolic_image(icon_name)
                                                              : icon_label_new(icon_name, text));
        return;
    }
    GIcon* icon = themed_icon_with_fallbacks(icon_name);
    gtk_image_set_from_gicon(GTK_IMAGE(image), icon);
    g_object_unref(icon);
    if (label != NULL && g_strcmp0(gtk_label_get_text(GTK_LABEL(label)), text) != 0) {
        gtk_label_set_text(GTK_LABEL(label), text);
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

/* Bundled icon lookin-<bundled>-symbolic, cached by name. Without SVG
 * support, fallback-symbolic (an icon built into GTK). */
static GIcon* bundled_gicon(const gchar* bundled, const gchar* fallback) {
    static GHashTable* cache = NULL;
    if (cache == NULL) {
        cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    }
    GIcon* gicon = g_hash_table_lookup(cache, bundled);
    if (gicon != NULL) {
        return gicon;
    }
    GdkDisplay* display = gdk_display_get_default();
    register_bundled_icons(display);
    gchar* name = g_strconcat("lookin-", bundled, "-symbolic", NULL);
    if (display != NULL && svg_supported() &&
        gtk_icon_theme_has_icon(gtk_icon_theme_get_for_display(display), name)) {
        gicon = bundled_themed_icon(bundled);
    } else {
        gchar* fb = g_strconcat(fallback, "-symbolic", NULL);
        gicon = g_themed_icon_new(fb);
        g_free(fb);
    }
    g_free(name);
    g_hash_table_insert(cache, g_strdup(bundled), gicon);
    return gicon;
}

/* Glyph of a file-type icon (lookin-ft-<icon>) */
static GIcon* file_type_gicon(const gchar* icon, gboolean is_dir) {
    gchar* bundled = g_strconcat("ft-", icon, NULL);
    GIcon* gicon = bundled_gicon(bundled, is_dir ? "folder" : "text-x-generic");
    g_free(bundled);
    return gicon;
}

/* Show gicon in image, coloured by the CSS class ft-<color> (theme.c) */
static void set_coloured_image(GtkWidget* image, GIcon* gicon, const gchar* color) {
    if (gtk_image_get_gicon(GTK_IMAGE(image)) != gicon) {
        gtk_image_set_from_gicon(GTK_IMAGE(image), gicon);
    }
    gchar* cls = g_strconcat("ft-", color, NULL);
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

GtkWidget* file_type_image_new(void) {
    GtkWidget* image = gtk_image_new();
    gtk_widget_add_css_class(image, "file-type-icon");
    return image;
}

void file_type_image_set(GtkWidget* image, const gchar* name, gboolean is_dir) {
    FileTypeIcon ft = file_type_icon(name, is_dir);
    set_coloured_image(image, file_type_gicon(ft.icon, is_dir), ft.color);
}

typedef struct {
    const gchar* glyph;
    const gchar* bundled;  /* lookin-<bundled>-symbolic */
    const gchar* color;    /* file-type icon colour (palette role icon_<color>) */
    const gchar* fallback; /* built into GTK */
} RowGlyph;

/* Seti glyphs where Seti has one, current Adwaita symbolic icons otherwise */
static const RowGlyph ROW_GLYPHS[] = {
    {"folder", "ft-folder", "grey_light", "folder"},
    {"notebook", "ft-notebook", "grey_light", "folder"},
    {"trash", "user-trash", "grey_light", "user-trash"},
    {"remote", "network-server", "grey_light", "network-server"},
    {"remote-connected", "network-server", "green", "network-server"},
    {"link", "ft-html", "blue", "text-x-generic"},
    {"missing", "dialog-warning", "yellow", "dialog-question"},
    {"home", "go-home", "grey_light", "folder"},
    {"root", "drive-harddisk", "grey_light", "folder"},
    {"canvas", "applications-graphics", "purple", "text-x-generic"},
    {"image", "ft-image", "purple", "text-x-generic"},
    {"note", "dialog-information", "blue", "dialog-question"},
    {"tip", "dialog-information", "green", "dialog-question"},
    {"important", "starred", "purple", "dialog-question"},
    {"warning", "dialog-warning", "yellow", "dialog-question"},
    {"caution", "dialog-error", "red", "dialog-question"},
    {"author", "avatar-default", "grey_light", "text-x-generic"},
    {"description", "document-edit", "grey_light", "text-x-generic"},
    {"attachment", "mail-attachment", "grey_light", "text-x-generic"},
    {"unknown", "ft-default", "white", "text-x-generic"},
};

/* Bundled icon and colour of a glyph: one of ROW_GLYPHS, else a bundled
 * Adwaita icon by its name (notebook icons) in grey, else "unknown" */
static GIcon* glyph_gicon(const gchar* glyph, const gchar** color) {
    for (gsize i = 0; i < G_N_ELEMENTS(ROW_GLYPHS); i++) {
        if (g_strcmp0(ROW_GLYPHS[i].glyph, glyph) == 0) {
            *color = ROW_GLYPHS[i].color;
            return bundled_gicon(ROW_GLYPHS[i].bundled, ROW_GLYPHS[i].fallback);
        }
    }
    if (glyph != NULL && glyph_exists(glyph)) {
        *color = "grey_light";
        return bundled_gicon(glyph, "text-x-generic");
    }
    const RowGlyph* g = &ROW_GLYPHS[G_N_ELEMENTS(ROW_GLYPHS) - 1];
    *color = g->color;
    return bundled_gicon(g->bundled, g->fallback);
}

gboolean glyph_exists(const gchar* glyph) {
    for (gsize i = 0; i < G_N_ELEMENTS(ROW_GLYPHS); i++) {
        if (g_strcmp0(ROW_GLYPHS[i].glyph, glyph) == 0) return TRUE;
    }
    if (glyph == NULL || glyph[0] == '\0' || strchr(glyph, '/') != NULL) return FALSE;
    gchar* path = g_strdup_printf("/org/lookin/icons/scalable/actions/lookin-%s-symbolic.svg", glyph);
    gboolean found = g_resources_get_info(path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL, NULL, NULL);
    g_free(path);
    return found;
}

void glyph_image_set(GtkWidget* image, const gchar* glyph) {
    const gchar* color = NULL;
    GIcon* gicon = glyph_gicon(glyph, &color);
    set_coloured_image(image, gicon, color);
}

GtkWidget* glyph_image_new(const gchar* glyph) {
    GtkWidget* image = file_type_image_new();
    glyph_image_set(image, glyph);
    return image;
}

GtkWidget* glyph_label_new(const gchar* glyph, const gchar* text) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), glyph_image_new(glyph));
    if (text != NULL) {
        gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    }
    return box;
}

GtkWidget* file_type_label_new(const gchar* name, const gchar* text) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* image = file_type_image_new();
    file_type_image_set(image, name, FALSE);
    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    return box;
}

/* GtkIconPaintable drawn with one colour: a text view draws paintables
 * without CSS, so the symbolic glyph gets its colour here */
#define FILE_TYPE_PAINTABLE_SIZE 16

typedef struct {
    GObject parent_instance;
    GtkIconPaintable* icon;
    GdkRGBA color;
} FileTypePaintable;

typedef GObjectClass FileTypePaintableClass;

static void file_type_paintable_iface_init(GdkPaintableInterface* iface);

G_DEFINE_TYPE_WITH_CODE(FileTypePaintable, file_type_paintable_obj, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE, file_type_paintable_iface_init))

static void file_type_paintable_snapshot(GdkPaintable* paintable, GdkSnapshot* snapshot,
                                         double width, double height) {
    FileTypePaintable* self = (FileTypePaintable*)paintable;
    if (self->icon == NULL) return;
    gtk_symbolic_paintable_snapshot_symbolic(GTK_SYMBOLIC_PAINTABLE(self->icon), snapshot,
                                             width, height, &self->color, 1);
}

static int file_type_paintable_size(GdkPaintable* paintable) {
    (void)paintable;
    return FILE_TYPE_PAINTABLE_SIZE;
}

static GdkPaintableFlags file_type_paintable_flags(GdkPaintable* paintable) {
    (void)paintable;
    return GDK_PAINTABLE_STATIC_SIZE | GDK_PAINTABLE_STATIC_CONTENTS;
}

static void file_type_paintable_iface_init(GdkPaintableInterface* iface) {
    iface->snapshot = file_type_paintable_snapshot;
    iface->get_intrinsic_width = file_type_paintable_size;
    iface->get_intrinsic_height = file_type_paintable_size;
    iface->get_flags = file_type_paintable_flags;
}

static void file_type_paintable_obj_finalize(GObject* object) {
    g_clear_object(&((FileTypePaintable*)object)->icon);
    G_OBJECT_CLASS(file_type_paintable_obj_parent_class)->finalize(object);
}

static void file_type_paintable_obj_class_init(FileTypePaintableClass* klass) {
    klass->finalize = file_type_paintable_obj_finalize;
}

static void file_type_paintable_obj_init(FileTypePaintable* self) {
    (void)self;
}

/* gicon drawn in the palette's current icon_<color> */
static GdkPaintable* coloured_paintable(GIcon* gicon, const gchar* color) {
    FileTypePaintable* self = g_object_new(file_type_paintable_obj_get_type(), NULL);
    GdkDisplay* display = gdk_display_get_default();
    if (display != NULL) {
        /* Scale 2: sharp on HiDPI screens, scaled down elsewhere */
        self->icon = gtk_icon_theme_lookup_by_gicon(gtk_icon_theme_get_for_display(display),
                                                    gicon, FILE_TYPE_PAINTABLE_SIZE, 2,
                                                    GTK_TEXT_DIR_NONE, 0);
    }
    gchar* role_name = g_strconcat("icon_", color, NULL);
    PaletteRole role = PAL_FG;
    palette_lookup(role_name, &role);
    g_free(role_name);
    if (!gdk_rgba_parse(&self->color, palette_color(role))) {
        self->color = (GdkRGBA){0.5f, 0.5f, 0.5f, 1.0f};
    }
    return GDK_PAINTABLE(self);
}

GdkPaintable* file_type_paintable(const gchar* name, gboolean is_dir) {
    FileTypeIcon ft = file_type_icon(name, is_dir);
    return coloured_paintable(file_type_gicon(ft.icon, is_dir), ft.color);
}

GdkPaintable* glyph_paintable(const gchar* glyph) {
    const gchar* color = NULL;
    GIcon* gicon = glyph_gicon(glyph, &color);
    return coloured_paintable(gicon, color);
}

/* paintable at it, then a space, all with tag (NULL for none): a tag that
 * sets paragraph properties must cover the paragraph's first character */
static void insert_paintable(GtkTextBuffer* buffer, GtkTextIter* it, GdkPaintable* paintable,
                             const gchar* tag) {
    GtkTextMark* start = gtk_text_buffer_create_mark(buffer, NULL, it, TRUE);
    gtk_text_buffer_insert_paintable(buffer, it, paintable);
    gtk_text_buffer_insert(buffer, it, " ", -1);
    if (tag != NULL) {
        GtkTextIter from;
        gtk_text_buffer_get_iter_at_mark(buffer, &from, start);
        gtk_text_buffer_apply_tag_by_name(buffer, tag, &from, it);
    }
    gtk_text_buffer_delete_mark(buffer, start);
    g_object_unref(paintable);
}

void text_insert_glyph(GtkTextBuffer* buffer, GtkTextIter* it, const gchar* glyph, const gchar* tag) {
    insert_paintable(buffer, it, glyph_paintable(glyph), tag);
}

void text_insert_file_icon(GtkTextBuffer* buffer, GtkTextIter* it, const gchar* name,
                           gboolean is_dir, const gchar* tag) {
    insert_paintable(buffer, it, file_type_paintable(name, is_dir), tag);
}
