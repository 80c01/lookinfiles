#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <pango/pangocairo.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>
#include <ctype.h>

#include "explorer/explorer_tree.h"
#include "core/file_types.h"
#include "git/git_state.h"
#include "notes/note_types.h"
#include "core/settings_store.h"

#ifdef HAVE_BUILTIN_TERM
#include "term_widget.h" /* VTE API on the built-in terminal (Windows) */
#elif defined(HAVE_VTE)
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
    SIDEBAR_VIEW_REMOTE,
    SIDEBAR_VIEW_GAMES
};

/* Editor modes */
typedef enum {
    EDITOR_MODE_NOTEPAD = 0, /* Default simple editor */
    EDITOR_MODE_VIM,         /* Vim-like modal editor */
    EDITOR_MODE_EMACS,       /* Emacs-style editor */
    EDITOR_MODE_ORGMODE      /* Org-mode focused emacs style */
} EditorMode;

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
    GtkWidget* minimap;   /* minimap_new_for_view() of view */
    struct Tagbar* tagbar; /* symbols beside the view (editor_tagbar.c), or NULL */
    GtkWidget* nb;        /* GtkNotebook containing this tab */
    struct AppData* app;  /* the application */
    EditorMode mode;      /* current editor mode */
    gboolean text_mode;   /* M-x text-mode: no Org-mode, even for a .org file */
    GtkWidget* status_bar; /* status line for this editor tab */
    gchar* status_text;   /* status message */
    struct EditorModeState* mode_state; /* mode-specific state (editor_modes_private.h) */
    const gchar* line_ending; /* "Unix (LF)"... detected at load (static) */
    const gchar* final_break; /* break the file ended with, kept out of the
                               * buffer and written back at save (static) */
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
    gint sidebar_view;   /* SIDEBAR_VIEW_FILES / BOOKMARKS / NOTES */
    gint sort_mode;
    gint notebook_page;
    GList* editors;
    GtkWidget* editor_panel; /* dedicated editor panel content while another workspace is shown (owned ref) */
    gboolean render_raw;
    gchar* remote_id;    /* ID of remote machine if workspace is connected to remote */
    ExplorerTreeState* tree_state; /* explorer rows while another workspace is shown (owned) */
    GitState* git_state;           /* git badges and counts while another workspace is shown (owned) */
    struct FolderHistory* dir_history; /* folders shown, for back / forward (private to nav_history.c) */
    struct WsSearch* search_state; /* last search in files and its Search tab (project_search.c) */
    GtkWidget* help_page;          /* its Help tab while another workspace is shown (owned ref, help_guide.c) */
    GPtrArray* browser_pages;      /* its browser tabs while another workspace is shown (owned refs, browser.c) */
    GPtrArray* canvas_pages;       /* its Canvas tabs of the preview notebook while another workspace is shown (owned refs, canvas_view.c) */
    GPtrArray* video_pages;        /* its Video tabs of the preview notebook while another workspace is shown (owned refs, video_tab.c) */
    GPtrArray* game_pages;         /* its game tabs of the preview notebook while another workspace is shown (owned refs, games.c) */
    gboolean games_unlocked;       /* Game Mode unlocked in this workspace (Games view and its button) */
#ifdef HAVE_VTE
    struct TermWorkspace* term; /* its terminal panes, last focused pane and set-aside tabs (terminal.c) */
#endif
} VirtualLayout;

/* Shared application state */
typedef struct AppData {
    GtkTextBuffer* buffer;
    gchar* base_dir;
    gchar* start_dir;       /* directory opened at launch: new workspaces start there */
    ExplorerTree* tree;     /* explorer rows, selection and expansion (explorer_tree.h) */
    GtkWidget* tree_view;   /* the explorer GtkListView (explorer_tree_get_widget(tree)) */
    GtkWidget* text_view; /* right pane viewer (Alt+Left/Right cycling) */
    gulong selection_changed_id; /* preview handler on tree "selection-changed" (main.c), 0 = not connected */
    struct Search* search;  /* search bars and the open or last search (search.c), NULL before search_init() */
    struct TopBar* topbar;  /* top bar widgets (topbar.c), NULL before topbar_new() */
    GtkWidget* left_vbox;
    gboolean show_hidden; /* hidden files (dot-prefixed) */ /* bookmarks view active in explorer panel */
    gint sidebar_view;       /* SIDEBAR_VIEW_FILES / BOOKMARKS / NOTES */
    struct Bookmarks* bm;    /* bookmark tree and counters (bookmarks.c), NULL outside bookmarks_init() / bookmarks_free() */
    gint session_save_ws;       /* workspace to save on APP_EVENT_SAVE_WORKSPACE_BOOKMARK */
    gchar* session_open_target; /* saved-workspace target to open on APP_EVENT_OPEN_WORKSPACE_BOOKMARK */
    struct NotesManager* notes_mgr; /* notes and notebooks (notes.c) */
    struct AppEvents* events; /* subscriptions and app_notice() message (app_events.c) */
    gint sort_mode;       /* SORT_NAME / DATE / SIZE / EXT */
    gint nav_prefix;      /* NavPrefix (nav_keys.h): explorer / viewer prefix key waiting (gg, zz, C-x o) */
    gint nav_recenter_step; /* next Emacs C-l position: 0 center, 1 top, 2 bottom */
    struct PreviewState* preview_state; /* file shown, kind, raw view, zoom, page (preview_state.c), NULL until first set */
    struct TextChunks* text_chunks; /* large text loaded on scroll (text_chunks.c) */
    struct PreviewFull* preview_full; /* "Load Whole File" bar of the preview (preview_full.c) */
    GHashTable* expanded_paths; /* set of full_path strings for expanded directories */

    /* File monitors & async I/O cancellation */
    struct FsWatch* fs_watch;          /* file monitors and pending refresh (fs_watch.c), NULL until first used */
    GitState* git_state;               /* git badges and upstream counts of base_dir (git_state.h, owned), or NULL */
    GCancellable* dir_cancellable;     /* active left-pane directory scan GTask */
    GCancellable* preview_cancellable; /* active right-pane preview GTask */
    gboolean file_dirty;         /* the preview must be read again (file changed, raw/rendered toggled) */

    /* Embedded terminal panel (Ctrl+T) */
    GtkWidget* term_scroll;
#ifdef HAVE_VTE
    struct Terminal* terminal; /* the panel's stack of per-workspace panes (terminal.c), NULL before terminal_init() */
#endif

    /* Editor tabs (double-click / Enter on a file in the list) */
    GtkWidget* notebook;        /* preview notebook (page 0 = read-only preview; page 1+ = docked tabs) */
    GtkWidget* editor_notebook; /* dedicated editor notebook (when editor tabs moved to dedicated panel) */
    GtkWidget* editor_box;      /* dedicated editor panel container right of preview */
    GtkWidget* editor_paned;    /* resizable paned divider between preview_box and editor_box */
    gboolean editor_dedicated;  /* TRUE when editor tabs are in dedicated panel right of preview */
    GList* editors;             /* EditorTab*, order matching tab order */
    EditorTab* last_focused_editor; /* last focused editor tab */
    struct CodeNav* code_nav;       /* code navigation state (code_nav.c), created on first use */
    struct CodeIntel* code_intel;   /* language servers (code_intel.c), created on first use */
    GtkWidget* refs_page;           /* References page of the preview notebook (code_refs.c), or NULL */
    GtkWidget* search_page;         /* Search page (text search results, code_refs.c), or NULL */
    GPtrArray* canvas_pages;        /* Canvas tabs of every workspace (roots, not referenced; canvas_view.c), or NULL */
    GPtrArray* video_pages;         /* Video tabs of every workspace (roots, not referenced; video_tab.c), or NULL */
    GtkWidget* help_page;           /* Help page (the user guide, help_guide.c), or NULL */

    /* Media previews */
    GtkWidget* preview_stack;   /* GtkStack: "text" (text_view), "video" (video_player), or "audio" (audio_player) */
    struct VideoPreview* video; /* video player state (video.c), NULL before video_preview_init() */
    struct AudioPreview* audio; /* audio player state (audio.c), NULL before audio_preview_init() */


    struct GitTab* git;           /* Git commits tab state (git_tab.c), NULL before git_tab_init() */
    /* Floating key-binding cheat sheet (toggled by '?') */
    GtkWidget* help_window;
    GtkWidget* workspace_hbox;

    /* Main window: 'f' toggles fullscreen */
    GtkWindow* window;
    GMainLoop* main_loop;

    /* Focused panel and split-handle drag (panels.c), NULL until first used */
    struct Panels* panels;

    /* Minimap right of the preview (minimap_viewer_new(), minimap.c), or NULL */
    GtkWidget* viewer_minimap;

    /* Layout of the viewer/terminal pair (Ctrl+Shift+L) */
    GtkWidget* viewer_box;   /* right pane content: viewer + minimap */
    GtkWidget* main_paned;   /* list | rest (fixed split) */
    GtkWidget* right_paned;  /* viewer | terminal (rebuilt on toggle) */

    /* Virtual workspace layouts & top-bar workspace minimap */
    VirtualLayout workspaces[MAX_VIRTUAL_LAYOUTS];
    gint num_workspaces;
    gint current_ws;
    gboolean switching_ws;
    gboolean dir_scan_pending;
    struct WorkspaceView* workspace_view; /* minimap and layout being applied (workspace.c), or NULL */
    GtkWidget* layouts_btn;       /* top-bar button: anchor of the Alt+L layout switcher */

    /* VS Code-style chrome: explorer panel header and bottom status bar */
    struct SidebarHeader* sidebar_header; /* explorer panel header widgets (sidebar.c), NULL before create_sidebar_header() */
    struct StatusBars* status_bars;       /* explorer, top-bar notice and preview bar widgets (status_bar.c), or NULL */
    gboolean game_mode_unlocked;      /* current workspace's Game Mode, unlocked via secret sequence in help panel */
    gboolean game_mode_just_unlocked; /* TRUE right after unlocking until next key, consumes trailing Enter */
    struct RemoteStore* remote_store; /* remote machines and their folders (remote_store.c), or NULL */
    gchar* current_remote_id;         /* active remote machine ID for the current workspace */
    struct OrgFold* org_fold;         /* folding of the Org preview (org_fold.c), NULL until first used */
    EditorMode default_editor_mode; /* mode of new non-Org editor tabs (-e / --editor) */
    Settings settings;            /* settings.ini as loaded or saved (settings_store.h): line numbers,
                                   * wrap, color column, tagbars; the live mode is default_editor_mode */
    gint editor_tagbar_width;     /* tagbar width, shared by the tabs and the preview */
    struct PreviewTagbar* preview_tagbar_state; /* headings beside the preview (preview_tagbar.c), or NULL */
    struct FileUndo* file_undo;   /* last trash or move, for undo (file_moves.c) */
} AppData;

#endif /* APP_TYPES_H */
