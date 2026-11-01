#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <pango/pangocairo.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>
#include <ctype.h>

#include "explorer_tree.h"
#include "file_types.h"
#include "note_types.h"

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

/* Offscreen render of the part of the document around what the minimap
 * shows (the minimap scrolls over long documents) */
typedef struct {
    cairo_surface_t* surface;
    gint width;
    gint height;
    gdouble top;   /* minimap content y of the surface's first row */
    gdouble scale; /* scale it was drawn at */
} MinimapCache;

/* Text viewer padding in pixels for text/code preview */
#define PREVIEW_TEXT_PADDING_X 10
#define PREVIEW_TEXT_PADDING_Y 6
/* Extra right / bottom margin so the overlay scrollbars (about 12 px wide when
 * hovered) never cover the end of a line or the last line of the file */
#define TEXT_SCROLLBAR_CLEARANCE 14

/* Sidebar explorer view mode */
enum {
    SIDEBAR_VIEW_FILES = 0,
    SIDEBAR_VIEW_BOOKMARKS,
    SIDEBAR_VIEW_NOTES,
    SIDEBAR_VIEW_REMOTE
};

/* Editor modes */
typedef enum {
    EDITOR_MODE_NOTEPAD = 0, /* Default simple editor */
    EDITOR_MODE_VIM,         /* Vim-like modal editor */
    EDITOR_MODE_EMACS,       /* Emacs-style editor */
    EDITOR_MODE_ORGMODE      /* Org-mode focused emacs style */
} EditorMode;

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
    gboolean is_buffer;   /* TRUE if backing file is a buffer in ~/.config */
    GtkWidget* container; /* notebook page container (scroll + minimap) */
    GtkWidget* scroll;    /* scrolled window holding view */
    GtkWidget* view;      /* the editable text view */
    GtkTextBuffer* buffer;
    GtkWidget* tab_label; /* tab title, with a bullet when unsaved */
    GtkWidget* minimap;   /* minimap drawing area */
    MinimapCache mm_cache;
    gboolean mm_dragging;
    gdouble mm_grab;      /* slider drag: pointer y minus slider top */
    guint mm_vis_source;
    GtkWidget* nb;        /* GtkNotebook containing this tab */
    gpointer app;         /* pointer to AppData */
    EditorMode mode;      /* current editor mode */
    gboolean text_mode;   /* M-x text-mode: no Org-mode, even for a .org file */
    GtkWidget* status_bar; /* status line for this editor tab */
    gchar* status_text;   /* status message */
    gpointer mode_state;  /* mode-specific state (EditorModeState*) */
    const gchar* line_ending; /* "Unix (LF)"... detected at load (static) */
    /* Cursor and selection of the view while another view of its buffer (a
     * split view) has them: the buffer has one cursor, given to the view in use */
    GtkTextMark* view_insert;
    GtkTextMark* view_bound;
    gboolean view_passive; /* another view of the buffer has its cursor */
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
    gint sidebar_view;   /* SIDEBAR_VIEW_FILES / BOOKMARKS / NOTES */
    gint sort_mode;
    gint notebook_page;
    GList* editors;
    GtkWidget* editor_panel; /* dedicated editor panel content while another workspace is shown (owned ref) */
    gboolean render_raw;
    gchar* remote_id;    /* ID of remote machine if workspace is connected to remote */
    ExplorerTreeState* tree_state; /* explorer rows while another workspace is shown (owned) */
    GHashTable* git_statuses;      /* git badges while another workspace is shown (owned) */
    gpointer dir_history;          /* folders shown, for back / forward (private to nav_history.c) */
    gpointer search_state;         /* last search in files and its Search tab (project_search.c) */
    GtkWidget* help_page;          /* its Help tab while another workspace is shown (owned ref, help_guide.c) */
#ifdef HAVE_VTE
    GtkWidget* term_box;
    GtkWidget* last_focused_vte;
#endif
} VirtualLayout;

/* Search bar of the explorer or the preview (search.c), drawn like the search
 * of an editor tab in the global mode: Notepad find bar, Emacs I-search
 * minibuffer, Vim "/" command line */
typedef struct {
    GtkWidget* bar;
    GtkWidget* prompt;  /* "Find:", "I-search:", "/" */
    GtkWidget* entry;
    GtkWidget* echo;    /* match count, wrap or failure message */
    GtkWidget* buttons; /* Notepad: Find Next, Find Prev, close */
} AppSearchBar;

/* Shared application state */
typedef struct {
    GtkTextBuffer* buffer;
    gchar* base_dir;
    gchar* start_dir;       /* directory opened at launch: new workspaces start there */
    ExplorerTree* tree;     /* explorer rows, selection and expansion (explorer_tree.h) */
    GtkWidget* tree_view;   /* the explorer GtkListView (explorer_tree_get_widget(tree)) */
    GtkWidget* text_view; /* right pane viewer (Alt+Left/Right cycling) */
    gulong selection_changed_id; /* preview handler on tree "selection-changed" (main.c), 0 = not connected */
    AppSearchBar explorer_search; /* search bar at the bottom of the explorer (search.c) */
    AppSearchBar viewer_search;   /* search bar at the bottom of the preview */
    GtkWidget* path_label;
    GtkWidget* breadcrumb_box;
    GtkWidget* history_back_btn;  /* top bar arrows: folder history (topbar.c) */
    GtkWidget* history_fwd_btn;
    GtkWidget* files_btn;
    GtkWidget* preview_btn;
    GtkWidget* split_btn;         /* tab action button to split/dock editor panel */
    GtkWidget* term_btn;
    GtkWidget* sort_btn;
    GtkWidget* hidden_btn;
    GtkWidget* help_btn;
    GtkWidget* left_vbox;
    gchar* search_term;
    gboolean show_hidden; /* hidden files (dot-prefixed) */
    gboolean show_bookmarks; /* bookmarks view active in explorer panel */
    gint sidebar_view;       /* SIDEBAR_VIEW_FILES / BOOKMARKS / NOTES */
    GNode* bookmarks;        /* bookmark tree root (see bookmarks.c): named folders and links */
    guint bm_next_id;        /* last assigned bookmark item id */
    guint bm_last_folder_id; /* folder last used when adding a bookmark (0 = top level) */
    GPtrArray* buffers;      /* temporary/custom buffer names or paths */
    gint next_buffer_id;     /* incremental counter for Untitled buffers */
    gpointer notes_mgr;      /* NotesManager* instance (see notes.c) */
    gpointer events;         /* AppEvents* subscription table (see app_events.c) */
    gint sort_mode;       /* SORT_NAME / DATE / SIZE / EXT */
    gint nav_prefix;      /* NavPrefix (nav_keys.h): explorer / viewer prefix key waiting (gg, zz, C-x o) */
    gint nav_recenter_step; /* next Emacs C-l position: 0 center, 1 top, 2 bottom */
    gboolean search_in_viewer; /* '/' opened from viewer, not list */
    gboolean search_backward;  /* search bar opened backward (Emacs C-r) */
    gint search_origin_row;    /* explorer row position when the bar opened (C-g goes back) */
    gdouble search_origin_scroll; /* viewer scroll when the bar opened */
    gint viewer_match;    /* last viewer match (char offset), -1 = none */
    gboolean render_raw;   /* FALSE = rendered preview (default), TRUE = raw text */
    gdouble preview_zoom;  /* image and PDF zoom, relative to the fitted size (1.0) */
    const gchar* viewed_encoding; /* text encoding of the previewed file (static string), NULL if not text */
    gpointer text_chunks;  /* large text loaded on scroll (text_chunks.c) */
    GHashTable* expanded_paths; /* set of full_path strings for expanded directories */

    /* File monitors & async I/O cancellation */
    GHashTable* dir_monitors;          /* active directory monitors: full_path -> GFileMonitor* */
    GHashTable* git_statuses;          /* Git status badges: full_path -> markup string */
    GFileMonitor* file_monitor;        /* viewed file in right pane */
    GFileMonitor* git_monitor;         /* .git monitor for git status & commits updates */
    gchar* git_monitored_path;         /* path to current monitored .git dir */
    GCancellable* dir_cancellable;     /* active left-pane directory scan GTask */
    GCancellable* git_status_cancellable; /* active git status badge scan GTask */
    GCancellable* preview_cancellable; /* active right-pane preview GTask */
    gchar* viewed_path;          /* path of displayed file, NULL otherwise */
    gchar* viewed_name;          /* name of displayed file */
    FileKind viewed_kind;        /* kind of displayed file, detected from its content */
    gchar* viewed_content_type;  /* GIO content type of displayed text, NULL until read */
    gboolean dir_dirty;          /* pending directory event */
    gboolean file_dirty;         /* pending file event */
    gboolean git_dirty;          /* pending git status update */
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
    EditorTab* last_focused_editor; /* last focused editor tab */
    gpointer code_nav;              /* code navigation state (code_nav.c), created on first use */
    gpointer code_intel;            /* language servers (code_intel.c), created on first use */
    guint preview_page;             /* page of a paged preview (mailbox message), 0: its first view */
    guint preview_pages;            /* pages of the shown preview after its first view */
    GtkWidget* refs_page;           /* References page of the preview notebook (code_refs.c), or NULL */
    GtkWidget* search_page;         /* Search page (text search results, code_refs.c), or NULL */
    GtkWidget* canvas_page;         /* Canvas page (Canvas note being edited, canvas_view.c), or NULL */
    GtkWidget* help_page;           /* Help page (the user guide, help_guide.c), or NULL */
    guint status_idle_id;           /* pending status bar refresh after a page switch */

    /* Video preview */
    GtkWidget* preview_stack;   /* GtkStack: "text" (text_view), "video" (video_player), or "audio" (audio_player) */
    GtkWidget* video_scroll;    /* Scrolled window wrapping video_box */
    GtkWidget* video_box;       /* Box container for video preview */
    GtkWidget* video_widget;    /* GtkVideo widget */
    GtkWidget* video_title;     /* Video title label */
    GtkWidget* video_meta;      /* Video metadata info label */
    GtkWidget* video_status;    /* Video warning/error status label */
    gchar* video_current_path;  /* Canonical path of currently loaded video */

    /* Audio preview */
    GtkWidget* audio_scroll;    /* Scrolled window wrapping audio_box */
    GtkWidget* audio_box;       /* Box container for audio preview */
    GtkWidget* audio_card;      /* Centered player card inside audio_box */
    GtkWidget* audio_controls;  /* GtkMediaControls widget */
    GtkWidget* audio_title;     /* Audio title label */
    GtkWidget* audio_meta;      /* Audio metadata info label */
    GtkWidget* audio_status;    /* Audio warning/error status label */
    GtkWidget* audio_icon;      /* Note icon, hidden when there is a cover */
    GtkWidget* audio_cover;     /* GtkImage: embedded cover art */
    GtkWidget* audio_tags;      /* Album / year / track / genre line */
    gchar* audio_details;       /* Technical line from the tags (codec, rate...), or NULL */
    gchar* video_details;       /* Technical line of the video (size, codecs, duration), or NULL */
    GtkMediaStream* audio_stream; /* Active audio GtkMediaStream */
    gulong audio_duration_sig;  /* Signal ID for notify::duration */
    gulong audio_prepared_sig;  /* Signal ID for notify::prepared */
    gulong audio_error_sig;     /* Signal ID for notify::error */
    gchar* audio_current_path;  /* Canonical path of currently loaded audio */


    /* Git commits tab in preview panel */
    GtkWidget* git_tab_box;       /* Box widget container for the Git tab */
    GtkWidget* git_tab_label;     /* Label widget for Git tab header */
    GtkWidget* git_paned;         /* Paned separating commit list and commit details */
    GListStore* git_store;        /* commit list (GitCommit items, see git_tab.c) */
    GtkSingleSelection* git_selection; /* selection over git_store */
    GtkWidget* git_list_view;     /* GtkColumnView displaying commits */
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
    MinimapCache mm_cache;
    gboolean mm_dragging;
    gdouble mm_grab;             /* slider drag: pointer y minus slider top */
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
    GtkWidget* sidebar_files_btn;   /* mini icon button: switch to Explorer view */
    GtkWidget* sidebar_bm_btn;      /* mini icon button: switch to Bookmarks view */
    GtkWidget* sidebar_notes_btn;   /* mini icon button: switch to Notes view */
    GtkWidget* sidebar_remote_btn;  /* mini icon button: switch to Remote (SSH) view */
    GtkWidget* sidebar_notes_actions; /* mini icon bar for note actions (new, folder, collapse, expand) */
    GtkWidget* sidebar_files_toolbar; /* mini action bar for files tree */
    GtkWidget* sidebar_bm_toolbar;    /* mini action bar for bookmarks */
    GtkWidget* sidebar_remote_toolbar; /* mini action bar for remote machines */
    GtkWidget* sidebar_files_hidden_btn; /* toggle hidden button in files action bar */
    GList* remotes;                   /* list of RemoteMachine* */
    gchar* current_remote_id;         /* active remote machine ID for the current workspace */
    gboolean bookmarks_sort_desc;        /* sort order for bookmarks view */
    GHashTable* org_folded_lines;     /* set of line numbers folded in currently viewed org file */
    gint org_global_fold;             /* 0 = all expanded, 1 = overview, 2 = contents */
    GtkWidget* status_branch;     /* bottom bar: Git branch of base_dir */
    GtkWidget* status_selection;  /* bottom bar: selected entry name */
    GtkWidget* status_size;       /* bottom bar: selected file size */
    GtkWidget* status_type;       /* bottom bar: selected file type / language */
    GtkWidget* status_code;       /* bottom bar: code navigation backend of the editor tab */
    GtkWidget* status_encoding;   /* bottom bar: encoding */
    GtkWidget* status_mode;       /* bottom bar: editor mode label */
    GtkWidget* status_mode_btn;   /* bottom bar: editor mode button */
    GtkWidget* status_notice_label; /* bottom bar: app_notice() message */
    GtkWidget* status_undo_btn;   /* bottom bar: Undo after the message */
    EditorMode default_editor_mode; /* mode of new non-Org editor tabs (-e / --editor) */
    gboolean editor_line_numbers; /* new editor tabs show line numbers (Preferences) */
    gboolean editor_wrap;         /* new editor tabs wrap long lines (Preferences) */
    gchar* status_notice;         /* bottom bar message (app_notice()), NULL if none */
    gboolean status_notice_undo;  /* the message offers Undo */
    guint status_notice_serial;   /* bumped by each app_notice() (restarts its timeout) */
    gpointer file_undo;           /* last trash or move, for undo (file_moves.c) */
} AppData;

#endif /* APP_TYPES_H */
