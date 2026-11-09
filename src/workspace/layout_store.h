#ifndef LAYOUT_STORE_H
#define LAYOUT_STORE_H

/* Saved workspace layouts: named panel arrangements the user saves and picks
 * from a list (layouts.c). A layout holds panel visibility, split sizes, the
 * preview/terminal orientation, the dedicated editor panel with its splits
 * and terminal tabs, the terminal pane tree, and the tagbars (shown in the
 * editor tabs, in the preview, and their width); not the folder, view or
 * open files. Applying one never changes
 * the default view of new workspaces, except the default layout below.
 *
 * The default layout is the panel arrangement new workspaces open with. It is
 * stored apart from the named layouts (group [Default]); without one, the
 * built-in default applies (explorer and preview, no terminal).
 *
 * Pure data and file I/O (GKeyFile), no widgets: unit-tested in tests/. */
#include <glib.h>

/* In editor_splits, the tab entry that marks a pane holding files */
#define LAYOUT_EDITOR_FILES 0u

typedef struct {
    gchar* name;
    gboolean files_vis;        /* explorer panel shown */
    gboolean preview_vis;      /* preview panel shown */
    gboolean term_vis;         /* terminal panel shown */
    gboolean editor_dedicated; /* editor tabs in the dedicated panel */
    gboolean stacked;          /* terminal below the preview (else beside it) */
    gdouble main_frac;         /* explorer width / window width */
    gdouble right_frac;        /* preview share of the preview/terminal pair */
    gdouble editor_frac;       /* preview share of the preview/editor pair */
    gchar* terminals;          /* terminal pane tree (see below), "" if none */
    gchar* editor_splits;      /* splits of the dedicated editor panel as tab_tree.h
                                * text: a notebook lists LAYOUT_EDITOR_FILES ("0")
                                * when it held files, then "t" per terminal tab,
                                * e.g. "H:0.500([0],[t,t])"; "" if none */
    gboolean tagbars;          /* the three tagbar fields below were saved (layouts
                                * saved before them keep the current tagbars) */
    gboolean editor_tagbar;    /* code and Markdown editor tabs show the tagbar */
    gboolean preview_tagbar;   /* the preview shows the tagbar */
    gint tagbar_width;         /* tagbar width in pixels, 0 = default width */
} SavedLayout;

SavedLayout* saved_layout_new(const gchar* name);
SavedLayout* saved_layout_copy(const SavedLayout* layout);
void saved_layout_free(gpointer layout);

/* Short description for the list, e.g. "Explorer · Preview · Terminal (2 terminals, beside)". Caller frees. */
gchar* saved_layout_summary(const SavedLayout* layout);

/* Built-in default: explorer and preview side by side, no terminal */
SavedLayout* layout_store_builtin_default(void);
/* TRUE when the layout differs from the built-in default */
gboolean layout_store_is_customized(const SavedLayout* layout);

/* Built-in presets, never written to layouts.ini: Code (explorer, preview,
 * editor panel, terminal below), Terminal (explorer, preview, two terminals
 * beside) and Focus (preview only). Names are translated; a saved layout of
 * the same name replaces a preset, and deleting it brings the preset back.
 * Returns the presets not replaced by one of saved (may be NULL), as a
 * GPtrArray of SavedLayout* that frees its items. */
GPtrArray* layout_store_presets(GPtrArray* saved);

/* Smallest pane the Auto grid makes, in pixels (about 70 columns by 12
 * lines): panes go side by side only with that room for each, except the
 * two panes two files or more always get */
#define LAYOUT_GRID_MIN_W 520
#define LAYOUT_GRID_MIN_H 240
/* Smallest share of the preview beside the editor panel the Auto layout
 * makes to put two editors side by side */
#define LAYOUT_PREVIEW_MIN_FRAC 0.25

/* What the Auto layout changes in the panels: each field is 1 (yes), 0
 * (no) or -1 (as it is) */
typedef struct {
    gint show_terminal;
    gint show_preview;
    gint terminal_below; /* stacked: below the preview (else beside it) */
} AutoRules;

/* The Auto layout (name translated): current with the editor tabs in the
 * dedicated panel, the panels changed as rules says (NULL: none), and the
 * editor panel split as layout_store_grid_splits() for the size it gets in
 * a main area of width x height pixels (0 = unknown). Caller frees. */
SavedLayout* layout_store_auto(const SavedLayout* current, guint files, const AutoRules* rules, gint width,
                               gint height);
/* editor_splits text of a grid for files open files in an editor panel of
 * width x height pixels (0 = unknown, taken as wide), panes being at least
 * LAYOUT_GRID_MIN_W x LAYOUT_GRID_MIN_H: up to four panes, as many as fit
 * (more files share the panes as tabs). Two go side by side when the panel
 * is wide, one above the other when it is tall; three go one on the left
 * and two stacked on the right (wide) or one on top and two below (tall);
 * four go two by two; without room for two by two, the panes make one row
 * or one column. Caller frees. */
gchar* layout_store_grid_splits(guint files, gint width, gint height);
/* Size the dedicated editor panel gets with layout l in a main area of
 * width x height pixels (explorer, terminal and preview taken off) */
void layout_store_editor_area(const SavedLayout* l, gint width, gint height, gint* editor_w, gint* editor_h);

/* ~/.config/lookinfiles/layouts.ini. Caller frees. */
gchar* layout_store_default_path(void);

/* Named layouts in file order (GPtrArray of SavedLayout*, frees its items);
 * empty if the file is missing or unreadable */
GPtrArray* layout_store_load(const gchar* path);
/* Default layout of new workspaces (the built-in one if none is saved). Caller frees. */
SavedLayout* layout_store_load_default(const gchar* path);
/* Write the named layouts and the default (not written when NULL or built-in) */
gboolean layout_store_save(const gchar* path, GPtrArray* layouts, const SavedLayout* default_layout,
                           GError** error);

/* One layout in a group of a key file (also used by session.ini); the
 * name is written only when named */
SavedLayout* saved_layout_read(GKeyFile* kf, const gchar* group, const gchar* name);
void saved_layout_write(GKeyFile* kf, const gchar* group, const SavedLayout* layout, gboolean named);

SavedLayout* layout_store_find(GPtrArray* layouts, const gchar* name);
/* Add a layout (taking ownership), replacing one with the same name in place */
void layout_store_put(GPtrArray* layouts, SavedLayout* layout);
gboolean layout_store_remove(GPtrArray* layouts, const gchar* name);

/* Terminal pane tree: "T" is a pane with one terminal tab, "T<n>" one with n
 * tabs; "H:<frac>(<a>,<b>)" splits side by side and "V:<frac>(<a>,<b>)" one
 * above the other, <frac> being the share of the first child, e.g.
 * "H:0.500(T3,V:0.400(T,T))". */
#define TERM_LAYOUT_MAX_TABS 32

typedef struct TermLayoutNode {
    gboolean is_terminal;
    guint tabs; /* terminal tabs of a pane (1 or more) */
    gboolean vertical;
    gdouble frac;
    struct TermLayoutNode* first;
    struct TermLayoutNode* second;
} TermLayoutNode;

/* NULL if text is empty or invalid */
TermLayoutNode* term_layout_parse(const gchar* text);
gchar* term_layout_to_string(const TermLayoutNode* node);
/* Number of terminals (the tabs of every pane) */
guint term_layout_count(const TermLayoutNode* node);
void term_layout_free(TermLayoutNode* node);

#endif /* LAYOUT_STORE_H */
