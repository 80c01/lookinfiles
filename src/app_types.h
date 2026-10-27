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
    COL_NAME,       /* raw file or directory name */
    COL_TYPE,       /* "dir", "file", or "-" */
    COL_LABEL,      /* display label with icon (emoji + space + name) */
    COL_PATH,       /* full absolute path on disk */
    COL_LOADED,     /* boolean: whether sub-items have been loaded */
    COL_GIT_STATUS, /* markup for git status badge */
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
    GtkWidget* container; /* notebook page container (scroll + minimap) */
    GtkWidget* scroll;    /* scrolled window holding view */
    GtkWidget* view;      /* the editable text view */
    GtkTextBuffer* buffer;
    GtkWidget* tab_label; /* tab title, with a bullet when unsaved */
    GtkWidget* minimap;   /* minimap drawing area */
    cairo_surface_t* mm_cache;
    gint mm_cache_w;
    gint mm_cache_h;
    gboolean mm_dragging;
    guint mm_vis_source;
} EditorTab;

/* Virtual workspace layout state (dynamic workspaces like Ubuntu) */
#define MAX_VIRTUAL_LAYOUTS 8

typedef struct {
    gboolean files_vis;
    gboolean preview_vis;
    gboolean term_vis;
    gboolean editor_dedicated;
    GtkOrientation right_orient;
    gdouble main_frac;
    gdouble right_frac;
    gdouble editor_frac;
    gchar* base_dir;
    GHashTable* expanded_paths;
    gchar* selected_path;
    gboolean show_hidden;
    gboolean show_bookmarks;
    gint sort_mode;
    gint notebook_page;
    GList* editors;
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
    GtkWidget* editor_btn;
    GtkWidget* term_btn;
    GtkWidget* sort_btn;
    GtkWidget* hidden_btn;
    GtkWidget* help_btn;
    GtkWidget* left_vbox;
    gchar* search_term;
    gboolean show_hidden; /* hidden files (dot-prefixed) */
    gboolean show_bookmarks; /* bookmarks view active in explorer panel */
    GPtrArray* bookmarks; /* bookmarked directory paths */
    gint sort_mode;       /* SORT_NAME / DATE / SIZE / EXT */
    gboolean pending_g;   /* for the 'gg' key sequence */
    gboolean pending_z;   /* for the 'zz', 'zt', 'zb' key sequence */
    gboolean search_in_viewer; /* '/' opened from viewer, not list */
    gint viewer_match;    /* last viewer match (char offset), -1 = none */
    gboolean render_raw;   /* FALSE = rendered preview (default), TRUE = raw text */
    GHashTable* expanded_paths; /* set of full_path strings for expanded directories */

    /* File monitors & async I/O cancellation */
    GHashTable* dir_monitors;          /* active directory monitors: full_path -> GFileMonitor* */
    GHashTable* git_statuses;          /* Git status badges: full_path -> markup string */
    GFileMonitor* file_monitor;        /* viewed file in right pane */
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
    GtkWidget* notebook;        /* preview notebook (page 0 = read-only preview; page 1+ = docked tabs) */
    GtkWidget* editor_notebook; /* dedicated editor notebook (when editor tabs moved to dedicated panel) */
    GtkWidget* editor_box;      /* dedicated editor panel container right of preview */
    GtkWidget* editor_paned;    /* resizable paned divider between preview_box and editor_box */
    gboolean editor_dedicated;  /* TRUE when editor tabs are in dedicated panel right of preview */
    gdouble editor_frac;        /* split ratio between preview panel and editor panel */
    GList* editors;             /* EditorTab*, order matching tab order */

    /* Video preview */
    GtkWidget* preview_stack;   /* GtkStack: "text" (text_view), "video" (video_player), or "audio" (audio_player) */
    GtkWidget* video_scroll;    /* Scrolled window wrapping video_box */
    GtkWidget* video_box;       /* Box container for video preview */
    GtkWidget* video_widget;    /* GtkVideo widget */
    GtkWidget* video_title;     /* Video title label */
    GtkWidget* video_meta;      /* Video metadata info label */
    GtkWidget* video_status;    /* Video warning/error status label */
    gulong video_duration_sig;  /* Signal ID for notify::duration */
    gulong video_prepared_sig;  /* Signal ID for notify::prepared */
    gulong video_error_sig;     /* Signal ID for notify::error */
    gchar* video_current_path;  /* Canonical path of currently loaded video */

    /* Audio preview */
    GtkWidget* audio_scroll;    /* Scrolled window wrapping audio_box */
    GtkWidget* audio_box;       /* Box container for audio preview */
    GtkWidget* audio_card;      /* Centered player card inside audio_box */
    GtkWidget* audio_controls;  /* GtkMediaControls widget */
    GtkWidget* audio_title;     /* Audio title label */
    GtkWidget* audio_meta;      /* Audio metadata info label */
    GtkWidget* audio_status;    /* Audio warning/error status label */
    GtkMediaStream* audio_stream; /* Active audio GtkMediaStream */
    gulong audio_duration_sig;  /* Signal ID for notify::duration */
    gulong audio_prepared_sig;  /* Signal ID for notify::prepared */
    gulong audio_error_sig;     /* Signal ID for notify::error */
    gchar* audio_current_path;  /* Canonical path of currently loaded audio */


    /* Git commits tab in preview panel */
    GtkWidget* git_tab_box;       /* Box widget container for the Git tab */
    GtkWidget* git_tab_label;     /* Label widget for Git tab header */
    GtkWidget* git_paned;         /* Paned separating commit list and commit details */
    GtkListStore* git_store;      /* Tree model for commit list */
    GtkWidget* git_tree_view;     /* Tree view displaying commits */
    GtkWidget* git_header_label;  /* Summary header of the selected commit */
    GtkWidget* git_detail_view;   /* Text view for git diff / commit details */
    GtkTextBuffer* git_detail_buf;/* Buffer for git diff / commit details */
    gchar* git_current_root;      /* Currently scanned git root path */
    gchar* git_selected_hash;     /* Currently selected commit hash */
    GCancellable* git_cancellable;/* Cancellable for background git log tasks */
    /* Floating key-binding cheat sheet (toggled by '?') */
    GtkWidget* help_window;
    GtkWidget* workspace_hbox;

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

    /* Active focused panel tracking and visual highlight */
    GtkWidget* active_panel;     /* currently focused panel container widget */
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

    /* VS Code-style chrome: explorer panel header and bottom status bar */
    GtkWidget* sidebar_title;     /* "EXPLORER" / "BOOKMARKS" header label */
    GtkWidget* sidebar_section;   /* current folder section label (files view) */
    GtkWidget* sidebar_files_btn; /* mini icon button: switch to Explorer view */
    GtkWidget* sidebar_bm_btn;    /* mini icon button: switch to Bookmarks view */
    GtkWidget* status_branch;     /* bottom bar: Git branch of base_dir */
    GtkWidget* status_selection;  /* bottom bar: selected entry name */
    GtkWidget* status_size;       /* bottom bar: selected file size */
    GtkWidget* status_type;       /* bottom bar: selected file type / language */
} AppData;

#endif /* APP_TYPES_H */
