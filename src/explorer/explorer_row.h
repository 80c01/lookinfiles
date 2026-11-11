#ifndef EXPLORER_ROW_H
#define EXPLORER_ROW_H

/* One row of the explorer tree (files, bookmarks, notes and remote views).
 *
 * Rows form a tree: each row owns the list of its children (a GListStore of
 * ExplorerRow, NULL for a leaf). The invisible root row returned by
 * explorer_row_new_root() holds the top-level rows. Rows added with
 * explorer_row_append_container() have a children list (even empty) and can
 * be expanded; the files view puts a placeholder child under unread
 * directories and replaces it when the row is expanded.
 *
 * Fields are public for reading. Change them through the setters: they emit
 * "changed" (void handler(ExplorerRow* row, gpointer data)) so the row widget
 * redraws. No widgets here: the view is explorer_tree. */
#include <gio/gio.h>

#define EXPLORER_TYPE_ROW (explorer_row_get_type())
G_DECLARE_FINAL_TYPE(ExplorerRow, explorer_row, EXPLORER, ROW, GObject)

struct _ExplorerRow {
    GObject parent_instance;
    gchar* type;          /* "dir", "file", "bookmark", "bm_folder", "notebook", "note",
                           * "trash", "trash_note", "buffer", "remote", "-" (placeholder) */
    gchar* name;          /* raw file, bookmark, note... name */
    gchar* label;         /* display label: the name (after an emoji when the row has
                           * neither icon_name nor glyph, e.g. a custom notebook icon) */
    gchar* icon_name;     /* file name whose VS Code file-type icon the view draws before
                           * the label (files, notes, links to files), "" for none */
    gchar* glyph;         /* otherwise a fixed glyph (icons.h: "folder", "trash"...), "" for none */
    gchar* path;          /* absolute path (or bookmark target), "" if none */
    gchar* badge;         /* Pango markup after the name: git status, count, target */
    gboolean loaded;      /* directory rows: children read from disk */
    guint bm_id;          /* bookmark item id (bookmarks view), 0 otherwise */
    ExplorerRow* parent;  /* parent row: the root for top-level rows, NULL for the root
                           * and for removed rows (use explorer_row_get_parent()) */
    gboolean is_root;
    GListStore* children; /* children (ExplorerRow), NULL for a leaf */
};

/* Invisible root: its children are the top-level rows */
ExplorerRow* explorer_row_new_root(void);

/* Append a leaf row (loaded = TRUE, badge "") and return it (owned by parent).
 * NULL strings are stored as "". */
ExplorerRow* explorer_row_append(ExplorerRow* parent, const gchar* type, const gchar* name,
                                 const gchar* label, const gchar* path);

/* Append a row that can be expanded (directory, folder, notebook, trash).
 * Whether a row can expand is fixed when it enters the tree: a leaf never
 * becomes a container later. */
ExplorerRow* explorer_row_append_container(ExplorerRow* parent, const gchar* type,
                                           const gchar* name, const gchar* label,
                                           const gchar* path);

/* Insert a row at position (past the end: appended), a container one (that
 * can be expanded) when container is TRUE; returns it (owned by parent) */
ExplorerRow* explorer_row_insert(ExplorerRow* parent, guint position, gboolean container,
                                 const gchar* type, const gchar* name, const gchar* label,
                                 const gchar* path);

/* A row made for parent (a container) but not added to it yet (new
 * reference): fill it and its children first, then add a whole level with
 * explorer_row_insert_rows(), one model change instead of one per row */
ExplorerRow* explorer_row_new_child(ExplorerRow* parent, gboolean container, const gchar* type,
                                   const gchar* name, const gchar* label, const gchar* path);

/* Insert rows (ExplorerRow*, each made by explorer_row_new_child() for
 * parent; the array keeps its references) at position, past the end: appended */
void explorer_row_insert_rows(ExplorerRow* parent, guint position, GPtrArray* rows);

/* Remove the child at position (nothing when out of range) */
void explorer_row_remove_at(ExplorerRow* parent, guint position);

/* Remove every child (the row stays expandable) */
void explorer_row_remove_children(ExplorerRow* row);

/* Remove the row from its parent */
void explorer_row_remove(ExplorerRow* row);

guint explorer_row_n_children(ExplorerRow* row);

/* Child at index i, NULL if out of range (borrowed: owned by row) */
ExplorerRow* explorer_row_child(ExplorerRow* row, guint i);

/* Parent row, NULL for top-level rows */
ExplorerRow* explorer_row_get_parent(ExplorerRow* row);

/* Index of row among its parent's children, -1 if detached */
gint explorer_row_index(ExplorerRow* row);

/* Number of ancestors below the root: 1 for a top-level row */
guint explorer_row_depth(ExplorerRow* row);

/* TRUE when ancestor is row or one of its ancestors */
gboolean explorer_row_is_descendant(ExplorerRow* row, ExplorerRow* ancestor);

gboolean explorer_row_is(ExplorerRow* row, const gchar* type);

void explorer_row_set_label(ExplorerRow* row, const gchar* label);
void explorer_row_set_badge(ExplorerRow* row, const gchar* badge);
void explorer_row_set_icon_name(ExplorerRow* row, const gchar* icon_name);
void explorer_row_set_glyph(ExplorerRow* row, const gchar* glyph);
void explorer_row_set_loaded(ExplorerRow* row, gboolean loaded);
void explorer_row_set_bm_id(ExplorerRow* row, guint id);

/* First row below `from` (depth-first, `from` excluded) for which match returns TRUE */
typedef gboolean (*ExplorerRowMatch)(ExplorerRow* row, gpointer data);
ExplorerRow* explorer_row_find(ExplorerRow* from, ExplorerRowMatch match, gpointer data);

/* First row below `from` whose path is `path` */
ExplorerRow* explorer_row_find_by_path(ExplorerRow* from, const gchar* path);

#endif /* EXPLORER_ROW_H */
