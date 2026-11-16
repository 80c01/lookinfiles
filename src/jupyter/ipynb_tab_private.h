#ifndef IPYNB_TAB_PRIVATE_H
#define IPYNB_TAB_PRIVATE_H

/* Shared by the files of the Jupyter notebook tab: ipynb_tab.c (the tab,
 * its cells), ipynb_save.c (saving), ipynb_run.c (running cells in a
 * kernel), ipynb_bar.c (the bar at the top), ipynb_tagbar.c (the
 * tagbar), ipynb_fold.c (folding), ipynb_select.c (cells selected
 * together) and ipynb_menu.c (the cells' menus). The public API is ipynb_tab.h. */
#include "core/app_types.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_kernel.h"

typedef struct IpynbTagbar IpynbTagbar;

typedef enum {
    IPYNB_RUN_NONE,    /* not run since the tab opened (or skipped after an error) */
    IPYNB_RUN_WAITING, /* queued: another cell runs first */
    IPYNB_RUN_RUNNING,
    IPYNB_RUN_OK,
    IPYNB_RUN_ERROR,   /* ended with an error, interrupted, or the kernel stopped */
} IpynbRunState;

#define IPYNB_TAB_MAX_CHARS 24 /* of the name in the tab label */

typedef struct {
    AppData* app;
    GtkWidget* root;        /* the notebook page */
    GtkWidget* tab_label;   /* owned ref: kept while the page moves */
    GtkWidget* label_text;  /* the name in tab_label */
    GtkWidget* cells_box;   /* one child per cell */
    GtkWidget* scroll;      /* the GtkScrolledWindow of cells_box */
    GtkWidget* info;        /* cell count (status bar) */
    GtkWidget* status;      /* saved, or the error of the last save or run (status bar) */
    GtkWidget* undo_button; /* puts the cell deleted last back */
    GPtrArray* views;       /* GtkTextView* of each cell (not referenced) */
    GHashTable* running;    /* keys (ipynb_doc_cell_key()) of the cells run or waiting to run */
    GHashTable* editing;    /* keys of the Markdown cells shown as source, not rendered */
    IpynbDoc* doc;
    gchar* path;            /* NULL: made in memory, no file yet */
    gchar* name;
    gchar* dir;             /* the notebook's folder (kernel, Save As) */
    GPtrArray* deleted;     /* IpynbCell*: the cells deleted, cut or merged last (owned), and where they were */
    guint deleted_at;
    guint deleted_merged;   /* key of the cell they were merged into (Undo takes it out), 0: deleted */
    guint current;          /* the cell with the focus, or used last */
    guint anchor;           /* key of the cell where the selected run starts (it ends at current), 0: none */
    gboolean extending;     /* moving the focus to extend the selection */
    guint save_source;
    guint rebuild_source;
    guint focus_after;      /* cell focused once rebuilt */
    gboolean dirty;
    gboolean building;      /* filling the buffers: not a change */
    gboolean closed;

    /* Running cells (ipynb_run.c) */
    IpynbKernel* kernel;     /* NULL until a cell runs */
    GtkWidget* kernel_button; /* the notebook's kernel; its menu chooses the kernel */
    GtkWidget* kernel_state;  /* the kernel's state (status bar) */
    GtkWidget* kernel_menu;   /* the box of that menu */
    GCancellable* list_cancel; /* listing the installed kernels, or NULL */
    GtkWidget* interrupt_button;
    GtkWidget* restart_button;
    GtkWidget* input_row;    /* shown while the code asks for a line */
    GtkWidget* input_prompt;
    GtkWidget* input_entry;
    GtkWidget* input_password;
    guint clear_wait;        /* cell whose outputs go at its next output */
    GHashTable* stale;       /* keys of the cells to show again */
    guint refresh_source;
    GHashTable* timings;     /* cell key -> IpynbTiming*: the cells run since the tab opened */
    guint timing_source;     /* ticks the time of the cells running */

    /* Tagbar (ipynb_tagbar.c) */
    IpynbTagbar* tagbar;     /* NULL once released */
    gboolean chord_k;        /* Ctrl+K typed: Ctrl+O shows / hides the tagbar */
} IpynbTab;

/* ipynb_tab.c */
/* The tab of a notebook page, or NULL; the open tab of the notebook at path
 * (any workspace), or NULL */
IpynbTab* ipynb_tab_of(GtkWidget* root);
IpynbTab* ipynb_tab_find(AppData* app, const gchar* path);
void ipynb_tab_set_status(IpynbTab* t, const gchar* text, gboolean error);
void ipynb_tab_update_info(IpynbTab* t);
void ipynb_tab_focus_cell(IpynbTab* t, guint index);
/* widget (of the cell at index) focused: that cell is the current one */
void ipynb_tab_track_focus(IpynbTab* t, GtkWidget* widget, guint index);
/* Insert a cell (the cells are built again, then it has the focus) */
void ipynb_tab_insert_cell(IpynbTab* t, guint at, IpynbCellType type);
/* Show the prompt and outputs of one cell again */
void ipynb_tab_refresh_cell(IpynbTab* t, guint index);
/* Show the run state of the code cell at index again (its prompt's colour,
 * the time in its head) */
void ipynb_tab_refresh_time(IpynbTab* t, guint index);
/* Show the Markdown cell at index rendered (it was run) */
void ipynb_tab_render_cell(IpynbTab* t, guint index);
/* Build every cell again (the kernel's language changed) */
void ipynb_tab_rebuild_cells(IpynbTab* t);
/* Build the cells again once the signal being handled is over (after a
 * change of their number, order or type), then focus the cell at focus */
void ipynb_tab_queue_rebuild(IpynbTab* t, guint focus);

/* ipynb_save.c */
/* The notebook changed: saved shortly */
void ipynb_tab_mark_changed(IpynbTab* t);
/* Write the changes now (a notebook without a file: only says so) */
void ipynb_tab_save_now(IpynbTab* t);
/* Ask for a file name and write the notebook there; the tab edits that file
 * from then on. FALSE when cancelled. */
gboolean ipynb_save_as(IpynbTab* t);
/* Before closing: a notebook without a file and with changes asks to save
 * them (Save As), drop them, or stay open (FALSE) */
gboolean ipynb_save_may_close(IpynbTab* t);

/* ipynb_run.c */
/* Add the run buttons to the tab's bar; returns the kernel button (the
 * notebook's kernel, and a menu of the installed kernels), placed by the
 * caller; also creates t->kernel_state, the state label of the status bar */
GtkWidget* ipynb_run_bar(IpynbTab* t, GtkWidget* bar);
/* The row that asks for a line (input()), hidden */
GtkWidget* ipynb_run_input_row_new(IpynbTab* t);
/* Run the cell at index: a code cell in the kernel (started first if
 * needed); a Markdown cell is rendered */
void ipynb_run_cell(IpynbTab* t, guint index);
/* Run the cells of index's range (ipynb_select_range()) */
void ipynb_run_range(IpynbTab* t, guint index);
/* Run the cell at index and every cell after it, in order (folded ones too) */
void ipynb_run_from(IpynbTab* t, guint index);
/* Run the current cell (the selected run) and go to the next one (a new
 * code cell after the last) */
void ipynb_run_advance(IpynbTab* t);
/* Where the code cell at index is in its last run since the tab opened,
 * and the text shown in its head (NULL for IPYNB_RUN_NONE, caller frees):
 * "waiting", the time since it started while it runs ("● 3.2s"), the time
 * it took ("✓ 1.4s", "✗ 0.3s") */
IpynbRunState ipynb_run_state(IpynbTab* t, guint index, gchar** text);
/* Stop the kernel (tab closed) */
void ipynb_run_stop(IpynbTab* t);

/* ipynb_bar.c */
/* The bar at the top of the tab: run buttons, new cells, Undo
 * (creates t->undo_button), kernel menu, headings and a menu of the rarer
 * actions */
GtkWidget* ipynb_bar_new(IpynbTab* t);

/* ipynb_tagbar.c */
/* Put content (the scrolled cells) and the tagbar side by side; returns
 * the GtkPaned to add to the tab. Shown when the tab opens if
 * Preferences → Editor shows the tagbar. */
GtkWidget* ipynb_tagbar_wrap(IpynbTab* t, GtkWidget* content);
/* Stop following the tab (it is closing) */
void ipynb_tagbar_release(IpynbTab* t);
/* The notebook changed: the headings are read again after a pause */
void ipynb_tagbar_changed(IpynbTab* t);
/* The current cell changed: its heading is selected after a pause */
void ipynb_tagbar_cursor_moved(IpynbTab* t);
/* Show the tagbar, the focus in it, or hide it, the focus back in the
 * current cell (this tab only, not saved) */
void ipynb_tagbar_toggle(IpynbTab* t);
/* Show or hide the tagbar (focus unchanged) / whether it is shown */
void ipynb_tagbar_set_shown(IpynbTab* t, gboolean shown);
gboolean ipynb_tagbar_shown(IpynbTab* t);
/* The tagbar's list while shown, else NULL; whether the focus is in the
 * tagbar (its list or search bar) */
GtkWidget* ipynb_tagbar_list(IpynbTab* t);
gboolean ipynb_tagbar_has_focus(IpynbTab* t);

/* ipynb_fold.c */
/* The fold arrow of a Markdown cell (shown while it has headings), first in
 * its head, and the note of the cells its folded section hides, at the
 * bottom of the cell; view is the cell's source view */
void ipynb_fold_add_heading(IpynbTab* t, guint index, GtkWidget* view, GtkWidget* head, GtkWidget* cell);
/* The arrow folding the code of a code cell, first in its head, and the
 * note shown in place of the folded code (its first line), after view */
void ipynb_fold_add_source(IpynbTab* t, guint index, GtkWidget* view, GtkWidget* head, GtkWidget* cell);
/* Show the code of the code cell at index, or its note when folded */
void ipynb_fold_refresh_source(IpynbTab* t, guint index);
/* The note of the code cell at index while its code is folded (it takes
 * the focus in place of the code), else NULL */
GtkWidget* ipynb_fold_source_note(IpynbTab* t, guint index);
/* Put the outputs box of a code cell beside an arrow that hides them (a
 * note in their place); returns the widget to add to the cell */
GtkWidget* ipynb_fold_outputs_wrap(IpynbTab* t, guint index, GtkWidget* view, GtkWidget* outputs);
/* The code cell at index shows n_outputs outputs: shown, folded, or
 * nothing at all without outputs */
void ipynb_fold_refresh_outputs(IpynbTab* t, guint index, guint n_outputs);
/* Show or hide the cells of the folded sections, the arrows and notes
 * (once every cell is built) */
void ipynb_fold_apply(IpynbTab* t);
/* Fold or unfold the section of the Markdown cell at index (Tab) / fold
 * every section, or unfold them all when all are folded (Shift+Tab) */
void ipynb_fold_toggle_section(IpynbTab* t, guint index);
void ipynb_fold_cycle_all(IpynbTab* t);
/* Unfold the sections hiding the cell at index; TRUE if it was hidden */
gboolean ipynb_fold_reveal(IpynbTab* t, guint index);
/* The next cell shown after index (step 1) or before it (step -1), or
 * index when there is none */
guint ipynb_fold_step(IpynbTab* t, guint index, gint step);

/* ipynb_select.c */
/* The cells an action on the cell at index takes: the selected run when
 * the cell is in it, else that cell alone (the selection dropped) */
void ipynb_select_range(IpynbTab* t, guint index, guint* first, guint* last);
/* How many cells are selected (1 without a selection) / how many an action
 * on the cell at index takes */
guint ipynb_select_count(IpynbTab* t);
guint ipynb_select_count_at(IpynbTab* t, guint index);
/* Show which cells are selected (and their count in the status bar) */
void ipynb_select_show(IpynbTab* t);
void ipynb_select_clear(IpynbTab* t);
/* Select from the current cell (or the start of the selected run) to the
 * next cell shown after it (step 1) or before it (step -1) / to index; the
 * focus goes there */
void ipynb_select_extend(IpynbTab* t, gint step);
void ipynb_select_extend_to(IpynbTab* t, guint index);
/* A click on head (of the cell at index) focuses its cell, Shift+click
 * selects up to it */
void ipynb_select_add_head(IpynbTab* t, guint index, GtkWidget* head);
/* Move the cells of index's range up (by -1) or down (1) */
void ipynb_select_move(IpynbTab* t, guint index, gint by);
/* Delete the cells of index's range, kept for ipynb_select_undo_delete()
 * (which puts back the cells deleted, cut or merged last and hides
 * t->undo_button) */
void ipynb_select_delete(IpynbTab* t, guint index);
void ipynb_select_undo_delete(IpynbTab* t);
/* Free the cells kept by the last delete */
void ipynb_select_drop_deleted(IpynbTab* t);
/* Give the cells of index's range that type */
void ipynb_select_set_type(IpynbTab* t, guint index, IpynbCellType type);
/* Copy the cells of index's range (for every notebook tab) / copy and
 * delete them / paste the cells copied below the range (then selected) /
 * merge the range into its first cell (a cell alone: with the next one;
 * Undo splits them again) */
void ipynb_select_copy(IpynbTab* t, guint index);
void ipynb_select_cut(IpynbTab* t, guint index);
void ipynb_select_paste(IpynbTab* t, guint index);
void ipynb_select_merge(IpynbTab* t, guint index);
/* The selection keys (Ctrl+Shift+Up / Down, Esc) and those of the actions
 * above (Ctrl+Shift+C / X / V / M); TRUE when key is one of them */
gboolean ipynb_select_key(IpynbTab* t, guint keyval, GdkModifierType mods);

/* ipynb_menu.c */
/* A right click on widget (of the cell at index) opens the cell menu: run,
 * copy, cut, paste, merge, insert, move, delete (the selected cells when
 * the cell is one of them) */
void ipynb_menu_attach(IpynbTab* t, guint index, GtkWidget* widget);
/* The text menu of the source view of the cell at index gets Split Cell */
void ipynb_menu_add_text(IpynbTab* t, guint index, GtkWidget* view);

#endif /* IPYNB_TAB_PRIVATE_H */
