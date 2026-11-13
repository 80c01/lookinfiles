#ifndef IPYNB_TAB_PRIVATE_H
#define IPYNB_TAB_PRIVATE_H

/* Shared by the files of the Jupyter notebook tab: ipynb_tab.c (the tab,
 * its cells, saving), ipynb_run.c (running cells in a kernel) and
 * ipynb_tagbar.c (the tagbar). The public API is ipynb_tab.h. */
#include "core/app_types.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_kernel.h"

typedef struct IpynbTagbar IpynbTagbar;

typedef struct {
    AppData* app;
    GtkWidget* root;        /* the notebook page */
    GtkWidget* tab_label;   /* owned ref: kept while the page moves */
    GtkWidget* cells_box;   /* one child per cell */
    GtkWidget* scroll;      /* the GtkScrolledWindow of cells_box */
    GtkWidget* info;        /* kernel and cell count */
    GtkWidget* status;      /* saved, or the error of the last save or run */
    GtkWidget* undo_button; /* puts the cell deleted last back */
    GPtrArray* views;       /* GtkTextView* of each cell (not referenced) */
    GHashTable* running;    /* keys (ipynb_doc_cell_key()) of the cells run or waiting to run */
    GHashTable* editing;    /* keys of the Markdown cells shown as source, not rendered */
    IpynbDoc* doc;
    gchar* path;
    gchar* name;
    IpynbCell* deleted;     /* the cell deleted last, and where it was */
    guint deleted_at;
    guint current;          /* the cell with the focus, or used last */
    guint save_source;
    guint rebuild_source;
    guint focus_after;      /* cell focused once rebuilt */
    gboolean dirty;
    gboolean building;      /* filling the buffers: not a change */
    gboolean closed;

    /* Running cells (ipynb_run.c) */
    IpynbKernel* kernel;     /* NULL until a cell runs */
    GtkWidget* kernel_button; /* the kernel's state; its menu chooses the kernel */
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

    /* Tagbar (ipynb_tagbar.c) */
    IpynbTagbar* tagbar;     /* NULL once released */
    gboolean chord_k;        /* Ctrl+K typed: Ctrl+O shows / hides the tagbar */
} IpynbTab;

/* ipynb_tab.c */
void ipynb_tab_set_status(IpynbTab* t, const gchar* text, gboolean error);
void ipynb_tab_update_info(IpynbTab* t);
/* The notebook changed: saved shortly */
void ipynb_tab_mark_changed(IpynbTab* t);
void ipynb_tab_focus_cell(IpynbTab* t, guint index);
/* Insert a cell (the cells are built again, then it has the focus) */
void ipynb_tab_insert_cell(IpynbTab* t, guint at, IpynbCellType type);
/* Show the prompt and outputs of one cell again */
void ipynb_tab_refresh_cell(IpynbTab* t, guint index);
/* Show the Markdown cell at index rendered (it was run) */
void ipynb_tab_render_cell(IpynbTab* t, guint index);
/* Build every cell again (the kernel's language changed) */
void ipynb_tab_rebuild_cells(IpynbTab* t);

/* ipynb_run.c */
/* Add the run buttons to the tab's bar; returns the kernel button (its
 * state, and a menu of the installed kernels), placed by the caller */
GtkWidget* ipynb_run_bar(IpynbTab* t, GtkWidget* bar);
/* The row that asks for a line (input()), hidden */
GtkWidget* ipynb_run_input_row_new(IpynbTab* t);
/* Run the cell at index: a code cell in the kernel (started first if
 * needed); a Markdown cell is rendered */
void ipynb_run_cell(IpynbTab* t, guint index);
/* Run the current cell and go to the next one (a new code cell after the
 * last) */
void ipynb_run_advance(IpynbTab* t);
/* Stop the kernel (tab closed) */
void ipynb_run_stop(IpynbTab* t);

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

#endif /* IPYNB_TAB_PRIVATE_H */
