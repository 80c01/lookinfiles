/* External file change detection and safe reload for open editor tabs.
 *
 * Watches each open file's FileStamp (mtime, size, inode) via per-file
 * GFileMonitors (debounced) plus filesystem, git, focus and workspace events.
 * Interaction stays faithful to each editor mode:
 * - Notepad: auto-reloads clean buffers; shows a non-blocking top banner on
 *   conflict and confirms before overwriting on save.
 * - Vim: auto-reloads clean buffers while :set autoread is on; reports W11/W12
 *   in the command line without a banner; :e! reloads, :w! forces overwrite.
 * - Emacs / Org-mode: auto-revert off by default (M-x auto-revert-mode); warns
 *   in the echo area; M-x revert-buffer reloads; C-x C-s prompts in minibuffer. */
#ifndef EDITOR_WATCH_H
#define EDITOR_WATCH_H

#include "core/app_types.h"

/* Mode-specific hooks registered once by editor_modes (avoids include cycles) */
typedef struct {
    /* TRUE when a clean buffer of tab should auto-reload on external change */
    gboolean (*auto_reload)(EditorTab* tab);
    /* Display a one-line message in the tab's own mode bar (is_error: red in Vim) */
    void (*notify)(EditorTab* tab, const gchar* msg, gboolean is_error);
} EditorWatchHooks;

typedef enum {
    EDITOR_WATCH_SAVE_OK = 0,   /* unchanged on disk, or user chose Overwrite */
    EDITOR_WATCH_SAVE_RELOADED, /* user chose Reload (buffer already reloaded) */
    EDITOR_WATCH_SAVE_CANCEL    /* user cancelled save */
} EditorWatchSaveAction;

/* Register mode hooks (called once at startup from main.c via editor_modes) */
void editor_watch_set_hooks(const EditorWatchHooks* hooks);

/* Record the current disk stamp of tab->path and arm its GFileMonitor.
 * Clears any pending conflict state and hides banners on all views of tab->buffer. */
void editor_watch_record(EditorTab* tab);

/* Retarget monitoring after Save As or a rename in the explorer */
void editor_watch_retarget(EditorTab* tab);

/* Sync the Notepad conflict banner on tab (call when adding a split view or
 * switching a tab's editor mode) */
void editor_watch_sync_view(EditorTab* tab);

/* TRUE when tab->path has changed (or been removed) on disk since it was last
 * recorded. If removed_out is non-NULL, sets *removed_out to TRUE when missing. */
gboolean editor_watch_disk_changed(EditorTab* tab, gboolean* removed_out);

/* Check one tab against disk now; when force_notify is TRUE (:checktime),
 * re-displays Vim/Emacs warnings even if already reported for this stamp. */
void editor_watch_check_tab(AppData* app, EditorTab* tab, gboolean force_notify);

/* Check every open editor tab in the current workspace (event subscriber) */
void editor_watch_check_all(AppData* app);

/* Reload tab->buffer from tab->path on disk, preserving cursor and scroll as a
 * single undoable action. Returns TRUE on success; on failure sets *error_out
 * (if non-NULL, caller frees). */
gboolean editor_watch_reload_tab(AppData* app, EditorTab* tab, gboolean show_notice,
                                 gchar** error_out);

/* Interactive Notepad check before saving: prompts Overwrite / Reload / Cancel
 * when the file changed on disk since it was last recorded. */
EditorWatchSaveAction editor_watch_before_save(AppData* app, EditorTab* tab);

/* Reload from Disk action (tab context menu): asks before discarding unsaved edits */
void editor_watch_reload_interactive(AppData* app, EditorTab* tab);

/* TRUE while editor_watch_reload_tab() is replacing buffer text (stops
 * on_editor_modified_changed() from auto-writing is_buffer files back to disk) */
gboolean editor_watch_is_reloading(GtkTextBuffer* buffer);

/* Cancel pending debounce timers and file monitors (main.c, at exit) */
void editor_watch_free(AppData* app);

#endif /* EDITOR_WATCH_H */
