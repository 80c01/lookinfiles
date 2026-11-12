#ifndef IPYNB_TAB_PRIVATE_H
#define IPYNB_TAB_PRIVATE_H

/* Shared by the files of the Jupyter notebook tab: ipynb_tab.c (the tab,
 * its cells, saving) and ipynb_run.c (running cells in a kernel). The
 * public API is ipynb_tab.h. */
#include "core/app_types.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_kernel.h"

typedef struct {
    AppData* app;
    GtkWidget* root;        /* the notebook page */
    GtkWidget* tab_label;   /* owned ref: kept while the page moves */
    GtkWidget* cells_box;   /* one child per cell */
    GtkWidget* info;        /* kernel and cell count */
    GtkWidget* status;      /* saved, or the error of the last save or run */
    GtkWidget* undo_button; /* puts the cell deleted last back */
    GPtrArray* views;       /* GtkTextView* of each cell (not referenced) */
    GHashTable* running;    /* keys (ipynb_doc_cell_key()) of the cells run or waiting to run */
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
    GtkWidget* kernel_label; /* the kernel's state */
    GtkWidget* interrupt_button;
    GtkWidget* restart_button;
    GtkWidget* input_row;    /* shown while the code asks for a line */
    GtkWidget* input_prompt;
    GtkWidget* input_entry;
    GtkWidget* input_password;
    guint clear_wait;        /* cell whose outputs go at its next output */
    GHashTable* stale;       /* keys of the cells to show again */
    guint refresh_source;
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

/* ipynb_run.c */
/* Add the run buttons to the tab's bar; returns the kernel state label,
 * placed by the caller */
GtkWidget* ipynb_run_bar(IpynbTab* t, GtkWidget* bar);
/* The row that asks for a line (input()), hidden */
GtkWidget* ipynb_run_input_row_new(IpynbTab* t);
/* Run the cell at index (a code cell; the kernel starts first if needed) */
void ipynb_run_cell(IpynbTab* t, guint index);
/* Run the current cell and go to the next one (a new code cell after the
 * last) */
void ipynb_run_advance(IpynbTab* t);
/* Stop the kernel (tab closed) */
void ipynb_run_stop(IpynbTab* t);

#endif /* IPYNB_TAB_PRIVATE_H */
