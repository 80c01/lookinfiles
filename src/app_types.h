#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <gtk/gtk.h>
#include <gio/gio.h>
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

/* Tree model columns */
enum {
    COL_NAME,    /* raw file or directory name */
    COL_TYPE,    /* "dir", "file", or "-" */
    COL_LABEL,   /* display label with icon (emoji + space + name) */
    COL_PATH,    /* full absolute path on disk */
    COL_LOADED,  /* boolean: whether sub-items have been loaded */
    COL_COUNT
};

/* Sort modes cycled with 'o' */
enum { SORT_NAME, SORT_DATE, SORT_SIZE, SORT_EXT, SORT_COUNT };

/* Large file preview thresholds */
#define PREVIEW_THRESHOLD (512 * 1024)  /* above 512 KB: preview only */
#define PREVIEW_BYTES     (64 * 1024)   /* first 64 KB loaded */

/* Minimap width in pixels */
#define MINIMAP_WIDTH 110

/* Text viewer padding in pixels for text/code preview */
#define PREVIEW_TEXT_PADDING_X 10
#define PREVIEW_TEXT_PADDING_Y 6

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

/* Virtual workspace layout state (dynamic workspaces like Ubuntu) */
#define MAX_VIRTUAL_LAYOUTS 8

typedef struct {
    gboolean files_vis;
    gboolean preview_vis;
    gboolean term_vis;
    GtkOrientation right_orient;
    gdouble main_frac;
    gdouble right_frac;
    gchar* base_dir;
    GHashTable* expanded_paths;
    gchar* selected_path;
    gboolean show_hidden;
    gint sort_mode;
    gint notebook_page;
    gboolean render_raw;
#ifdef HAVE_VTE
    GtkWidget* term_box;
    GtkWidget* last_focused_vte;
#endif
} VirtualLayout;

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
    GtkWidget* breadcrumb_box;
    GtkWidget* files_btn;
    GtkWidget* preview_btn;
    GtkWidget* term_btn;
    GtkWidget* sort_btn;
    GtkWidget* hidden_btn;
    GtkWidget* help_btn;
    GtkWidget* left_vbox;
    gchar* search_term;
    gboolean show_hidden; /* hidden files (dot-prefixed) */
    gint sort_mode;       /* SORT_NAME / DATE / SIZE / EXT */
    gboolean pending_g;   /* for the 'gg' key sequence */
    gboolean pending_z;   /* for the 'zz' key sequence */
    gboolean search_in_viewer; /* '/' opened from viewer, not list */
    gint viewer_match;    /* last viewer match (char offset), -1 = none */
    gboolean render_raw;   /* FALSE = rendered preview (default), TRUE = raw text */
    GHashTable* expanded_paths; /* set of full_path strings for expanded directories */

    /* File monitors & async I/O cancellation */
    GFileMonitor* dir_monitor;   /* current directory */
    GFileMonitor* file_monitor;  /* viewed file in right pane */
    GCancellable* dir_cancellable;     /* active left-pane directory scan GTask */
    GCancellable* preview_cancellable; /* active right-pane preview GTask */
    gchar* viewed_path;          /* path of displayed file, NULL otherwise */
    gchar* viewed_name;          /* name of displayed file */
    gboolean dir_dirty;          /* pending directory event */
    gboolean file_dirty;         /* pending file event */
    guint refresh_source;        /* debounce timeout id, 0 = idle */

    /* Embedded terminal panel (Ctrl+T) */
    GtkWidget* term_scroll;
#ifdef HAVE_VTE
    GtkWidget* term_stack; /* stack holding each workspace's independent term_box */
    GtkWidget* term_box; /* active workspace's root container of terminal panes */
    GtkWidget* last_focused_vte; /* last focused terminal pane for splitting */
#endif

    /* Editor tabs (double-click / Enter on a file in the list) */
    GtkWidget* notebook; /* viewer zone: page 0 = read-only preview */
    GList* editors;      /* EditorTab*, order matching tab order minus 1 */

    /* Floating key-binding cheat sheet (toggled by '?') */
    GtkWidget* help_window;

    /* Main window: 'f' toggles fullscreen */
    GtkWindow* window;
    GMainLoop* main_loop;

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
    guint mm_vis_source;         /* idle source for safe visibility toggle */

    /* Layout of the viewer/terminal pair (Ctrl+Shift+L) */
    GtkWidget* viewer_box;   /* right pane content: viewer + minimap */
    GtkWidget* main_paned;   /* list | rest (fixed split) */
    GtkWidget* right_paned;  /* viewer | terminal (rebuilt on toggle) */
    gdouble right_frac;      /* viewer share of viewer/terminal pair */

    /* Virtual workspace layouts & top-bar workspace minimap */
    VirtualLayout workspaces[MAX_VIRTUAL_LAYOUTS];
    gint num_workspaces;
    gint current_ws;
    gint hover_ws;
    gboolean switching_ws;
    gboolean applying_layout;
    gboolean dir_scan_pending;
    guint layout_tick_id;
    gint layout_apply_frames;
    gint layout_retry_count;
    GtkWidget* ws_minimap;
} AppData;

#endif /* APP_TYPES_H */
