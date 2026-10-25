#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>
#include <ctype.h>

#ifdef HAVE_VTE
#include <vte/vte.h>
#endif

#ifdef HAVE_GSV
/* Silence deprecated GTimeVal warnings inside library headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtksourceview/gtksource.h>
#pragma GCC diagnostic pop
#include <gio/gio.h>
#endif

/* Sort modes cycled with 'o' */
enum { SORT_NAME, SORT_DATE, SORT_SIZE, SORT_EXT, SORT_COUNT };

/* Large file preview thresholds */
#define PREVIEW_THRESHOLD (512 * 1024)  /* above 512 KB: preview only */
#define PREVIEW_BYTES     (64 * 1024)   /* first 64 KB loaded */

/* Minimap width in pixels */
#define MINIMAP_WIDTH 110

/* Entry collected for directory sorting */
typedef struct {
    gchar* name;
    gboolean is_dir;
    gint64 mtime;
    gint64 size;
} FileEntry;

/* One open-for-editing file: a notebook tab in the viewer zone */
typedef struct {
    gchar* path;          /* absolute file path */
    gchar* name;          /* display name (file basename) */
    GtkWidget* scroll;    /* notebook page widget */
    GtkWidget* view;      /* the editable text view */
    GtkTextBuffer* buffer;
    GtkWidget* tab_label; /* tab title, with a bullet when unsaved */
} EditorTab;

/* Shared application state */
typedef struct {
    GtkTextBuffer* buffer;
    gchar* base_dir;
    GtkTreeModel* model;
    GtkTreeView* tree_view;
    GtkWidget* text_view; /* right pane viewer (Alt+Left/Right cycling) */
    GtkTreeSelection* selection;
    GtkWidget* search_entry;
    GtkWidget* path_label;
    gchar* search_term;
    gboolean show_hidden; /* hidden files (dot-prefixed) */
    gint sort_mode;       /* SORT_NAME / DATE / SIZE / EXT */
    gboolean pending_g;   /* for the 'gg' key sequence */
    gboolean search_in_viewer; /* '/' opened from viewer, not list */
    gint viewer_match;    /* last viewer match (char offset), -1 = none */
    gboolean markdown_raw; /* FALSE = rendered preview (default), TRUE = raw text */

    /* File monitors */
    GFileMonitor* dir_monitor;   /* current directory */
    GFileMonitor* file_monitor;  /* viewed file in right pane */
    gchar* viewed_path;          /* path of displayed file, NULL otherwise */
    gchar* viewed_name;          /* name of displayed file */
    gboolean dir_dirty;          /* pending directory event */
    gboolean file_dirty;         /* pending file event */
    guint refresh_source;        /* debounce timeout id, 0 = idle */

    /* Embedded terminal panel (Ctrl+T) */
    GtkWidget* term_scroll;
#ifdef HAVE_VTE
    GtkWidget* term_box; /* row of side-by-side terminal panes */
#endif

    /* Editor tabs (double-click / Enter on a file in the list) */
    GtkWidget* notebook; /* viewer zone: page 0 = read-only preview */
    GList* editors;      /* EditorTab*, order matching tab order minus 1 */

    /* Floating key-binding cheat sheet (toggled by 'h') */
    GtkWidget* help_window;

    /* Main window: 'f' toggles fullscreen */
    GtkWindow* window;

    /* Pane-resize feedback popup */
    GtkWidget* dim_window;
    GtkWidget* dim_label;
    GtkWidget* dims_paned; /* paned whose handle is being dragged */
    gboolean panning;      /* split handle being dragged */

    /* Minimap: scaled file overview right of viewer */
    GtkWidget* minimap;
    GtkAdjustment* viewer_vadj;  /* vertical scroll adjustment */
    cairo_surface_t* mm_cache;   /* offscreen render of mini file */
    gint mm_cache_w;
    gint mm_cache_h;
    gboolean mm_dragging;

    /* Layout of the viewer/terminal pair (Ctrl+Shift+L) */
    GtkWidget* viewer_box;   /* right pane content: viewer + minimap */
    GtkWidget* main_paned;   /* list | rest (fixed split) */
    GtkWidget* right_paned;  /* viewer | terminal (rebuilt on toggle) */
    gdouble right_frac;      /* viewer share of viewer/terminal pair */
} AppData;

#endif /* APP_TYPES_H */
