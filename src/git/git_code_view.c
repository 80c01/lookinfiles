/* The Languages section of the Statistics view (see git_code_view.h) */
#include "git/git_code_view.h"
#include "core/i18n.h"
#include "git/git_code_stats.h"
#include "theme/palette.h"
#include "theme/theme.h"

/* Languages listed at most (the others are summed in a last row) */
#define LANGS_SHOWN 15
#define BAR_H 12.0
#define SWATCH 10
/* Data of the section's widget: the GitCodeView, freed with it */
#define CODE_VIEW_KEY "lookin-git-code-view"

/* Colours of the languages, in their order (the others: the last one) */
static const PaletteRole COLORS[] = {
    PAL_ICON_BLUE, PAL_ICON_ORANGE, PAL_ICON_GREEN, PAL_ICON_PURPLE, PAL_ICON_YELLOW,
    PAL_ICON_RED,  PAL_ICON_PINK,   PAL_SYN_TYPE,   PAL_SYN_STRING,  PAL_SYN_VARIABLE,
};
#define OTHER_COLOR PAL_ICON_GREY_LIGHT

/* A row of the table and a part of the bar */
typedef struct {
    gchar* name;
    guint files;
    guint64 lines;
    guint64 nonblank;
    guint64 bytes;
    PaletteRole color;
} Part;

struct GitCodeView {
    GtkWidget* widget;  /* the section (owns this struct) */
    GtkWidget* summary;
    GtkWidget* bar;     /* GtkDrawingArea */
    GtkWidget* table;   /* GtkGrid */
    GArray* parts;      /* Part, in the order of the table */
    guint64 lines;      /* of all the parts */
    GitCodeStats* stats; /* shown, or NULL */
    gchar* root;
    GCancellable* cancellable;
    gboolean stale;     /* read when the section is shown */
};

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".git-code-head { color: @fg_subtle; font-weight: 600; }"
                  ".git-code-cell { color: @fg_text; font-feature-settings: \"tnum\"; }"
                  ".git-code-dim { color: @fg_subtle; font-feature-settings: \"tnum\"; }");
}

static void part_clear(gpointer data) {
    Part* p = data;
    g_free(p->name);
}

static void cancel_read(GitCodeView* v) {
    if (v->cancellable != NULL) {
        g_cancellable_cancel(v->cancellable);
        g_clear_object(&v->cancellable);
    }
}

static gdouble share_of(const GitCodeView* v, guint64 lines) {
    return v->lines > 0 ? 100.0 * (gdouble)lines / (gdouble)v->lines : 0.0;
}

static gchar* count_text(guint64 n) {
    return g_strdup_printf("%" G_GUINT64_FORMAT, n);
}

/* The parts of stats: the first languages, then the others summed, the
 * files of no known language last */
static void fill_parts(GitCodeView* v, const GitCodeStats* s) {
    g_array_set_size(v->parts, 0);
    v->lines = 0;
    if (s == NULL) return;
    Part rest = {NULL, 0, 0, 0, 0, OTHER_COLOR};
    guint rest_langs = 0;
    const GitLangStats* unknown = NULL;
    for (guint i = 0; i < s->languages->len; i++) {
        const GitLangStats* l = g_ptr_array_index(s->languages, i);
        if (l->name == NULL) {
            unknown = l;
        } else if (v->parts->len < LANGS_SHOWN) {
            guint k = v->parts->len;
            Part p = {g_strdup(l->name), l->files, l->lines, l->nonblank, l->bytes,
                      k < G_N_ELEMENTS(COLORS) ? COLORS[k] : OTHER_COLOR};
            g_array_append_val(v->parts, p);
        } else {
            rest_langs++;
            rest.files += l->files;
            rest.lines += l->lines;
            rest.nonblank += l->nonblank;
            rest.bytes += l->bytes;
        }
    }
    if (rest_langs > 0) {
        rest.name = g_strdup_printf(ngettext("%u other language", "%u other languages", rest_langs), rest_langs);
        g_array_append_val(v->parts, rest);
    }
    if (unknown != NULL) {
        Part p = {g_strdup(_("Other files")), unknown->files, unknown->lines, unknown->nonblank, unknown->bytes,
                  OTHER_COLOR};
        g_array_append_val(v->parts, p);
    }
    for (guint i = 0; i < v->parts->len; i++) v->lines += g_array_index(v->parts, Part, i).lines;
}

static void set_color(cairo_t* cr, PaletteRole role) {
    double r, g, b;
    palette_rgb(role, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
}

/* Left edge and width of part i in a bar width wide */
static void part_span(const GitCodeView* v, guint i, gdouble width, gdouble* x, gdouble* w) {
    guint64 before = 0;
    for (guint k = 0; k < i; k++) before += g_array_index(v->parts, Part, k).lines;
    gdouble total = (gdouble)MAX(v->lines, 1u);
    *x = width * (gdouble)before / total;
    *w = width * (gdouble)g_array_index(v->parts, Part, i).lines / total;
}

static void draw_bar(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    GitCodeView* v = data;
    gdouble y = (height - BAR_H) / 2.0;
    if (v->lines == 0) {
        set_color(cr, PAL_BG_BLOCK);
        cairo_rectangle(cr, 0, y, width, BAR_H);
        cairo_fill(cr);
        return;
    }
    for (guint i = 0; i < v->parts->len; i++) {
        gdouble x, w;
        part_span(v, i, width, &x, &w);
        if (w <= 0) continue;
        set_color(cr, g_array_index(v->parts, Part, i).color);
        /* A thin gap between parts wide enough to keep one */
        cairo_rectangle(cr, x, y, w > 3.0 ? w - 1.0 : w, BAR_H);
        cairo_fill(cr);
    }
}

static gboolean on_bar_tooltip(GtkWidget* widget, gint x, gint y, gboolean keyboard, GtkTooltip* tooltip,
                               gpointer data) {
    (void)y;
    GitCodeView* v = data;
    if (keyboard || v->lines == 0) return FALSE;
    gdouble width = gtk_widget_get_width(widget);
    for (guint i = 0; i < v->parts->len; i++) {
        gdouble px, pw;
        part_span(v, i, width, &px, &pw);
        if (x < px || x >= px + pw) continue;
        const Part* p = &g_array_index(v->parts, Part, i);
        gchar* lines = count_text(p->lines);
        /* TRANSLATORS: tooltip of a part of the languages bar: "C: 1200 lines (85.2 %)" */
        gchar* tip = g_strdup_printf(_("%s: %s lines (%.1f %%)"), p->name, lines, share_of(v, p->lines));
        gtk_tooltip_set_text(tooltip, tip);
        g_free(tip);
        g_free(lines);
        return TRUE;
    }
    return FALSE;
}

static void draw_swatch(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)area;
    set_color(cr, (PaletteRole)GPOINTER_TO_INT(data));
    cairo_rectangle(cr, 0, (height - SWATCH) / 2.0, MIN(width, SWATCH), SWATCH);
    cairo_fill(cr);
}

static GtkWidget* cell(GtkWidget* grid, const gchar* text, const gchar* css_class, gint col, gint row,
                       gboolean right) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), right ? 1.0f : 0.0f);
    gtk_widget_add_css_class(label, css_class);
    gtk_grid_attach(GTK_GRID(grid), label, col, row, 1, 1);
    return label;
}

static void count_cell(GtkWidget* grid, guint64 n, const gchar* css_class, gint col, gint row) {
    gchar* text = count_text(n);
    cell(grid, text, css_class, col, row, TRUE);
    g_free(text);
}

static void show_table(GitCodeView* v) {
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(v->table)) != NULL) gtk_grid_remove(GTK_GRID(v->table), child);
    if (v->parts->len == 0) return;
    cell(v->table, _("Language"), "git-code-head", 1, 0, FALSE);
    cell(v->table, _("Files"), "git-code-head", 2, 0, TRUE);
    cell(v->table, _("Lines"), "git-code-head", 3, 0, TRUE);
    cell(v->table, _("Non-Blank"), "git-code-head", 4, 0, TRUE);
    cell(v->table, _("Size"), "git-code-head", 5, 0, TRUE);
    cell(v->table, _("Share"), "git-code-head", 6, 0, TRUE);
    for (guint i = 0; i < v->parts->len; i++) {
        const Part* p = &g_array_index(v->parts, Part, i);
        gint row = (gint)i + 1;
        GtkWidget* swatch = gtk_drawing_area_new();
        gtk_widget_set_size_request(swatch, SWATCH, SWATCH);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch), draw_swatch, GINT_TO_POINTER(p->color), NULL);
        gtk_grid_attach(GTK_GRID(v->table), swatch, 0, row, 1, 1);
        GtkWidget* name = cell(v->table, p->name, "git-code-cell", 1, row, FALSE);
        gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(name), 28);
        count_cell(v->table, p->files, "git-code-cell", 2, row);
        count_cell(v->table, p->lines, "git-code-cell", 3, row);
        count_cell(v->table, p->nonblank, "git-code-dim", 4, row);
        gchar* size = g_format_size(p->bytes);
        cell(v->table, size, "git-code-dim", 5, row, TRUE);
        g_free(size);
        gchar* share = g_strdup_printf("%.1f %%", share_of(v, p->lines));
        cell(v->table, share, "git-code-dim", 6, row, TRUE);
        g_free(share);
    }
}

static void show_summary(GitCodeView* v) {
    const GitCodeStats* s = v->stats;
    if (s == NULL) return;
    if (s->tree == NULL) {
        gtk_label_set_text(GTK_LABEL(v->summary), _("No commits in this repository."));
        return;
    }
    gchar* files = g_strdup_printf(ngettext("%u file", "%u files", s->files), s->files);
    gchar* count = count_text(s->lines);
    gchar* lines = g_strdup_printf(ngettext("%s line", "%s lines", (gulong)MIN(s->lines, G_MAXULONG)), count);
    gchar* size = g_format_size(s->bytes);
    /* TRANSLATORS: summary of the Languages section: "120 files", "4000 lines", a size */
    GString* text = g_string_new(NULL);
    g_string_printf(text, _("%s, %s, %s at HEAD"), files, lines, size);
    guint out = s->binary + s->generated + s->large;
    if (out > 0) {
        g_string_append(text, "\n");
        g_string_append_printf(text,
                               ngettext("%u file left out (binary, generated, vendored or too large)",
                                        "%u files left out (binary, generated, vendored or too large)", out),
                               out);
    }
    gtk_label_set_text(GTK_LABEL(v->summary), text->str);
    g_string_free(text, TRUE);
    g_free(size);
    g_free(lines);
    g_free(count);
    g_free(files);
}

/* Show stats (taken; NULL: nothing) */
static void show_stats(GitCodeView* v, GitCodeStats* stats) {
    git_code_stats_free(v->stats);
    v->stats = stats;
    fill_parts(v, stats);
    show_table(v);
    show_summary(v);
    gtk_widget_set_visible(v->bar, v->lines > 0);
    gtk_widget_queue_draw(v->bar);
}

typedef struct {
    gchar* root;
    gchar* unless_tree;
    gboolean unchanged;
    GitCodeStats* stats;
} ReadTask;

static void read_task_free(gpointer data) {
    ReadTask* td = data;
    g_free(td->root);
    g_free(td->unless_tree);
    git_code_stats_free(td->stats);
    g_free(td);
}

static void read_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    ReadTask* td = data;
    td->stats = git_code_stats_read(td->root, td->unless_tree, &td->unchanged, cancellable);
    if (g_task_return_error_if_cancelled(task)) return;
    g_task_return_boolean(task, TRUE);
}

/* The task reaches its view through its callback data: it is cancelled
 * before the view is freed, and a cancelled task's callback returns first */
static void on_read_ready(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    GTask* task = G_TASK(res);
    if (!g_task_propagate_boolean(task, NULL)) return; /* cancelled: a newer read, or exit */
    GitCodeView* v = data;
    ReadTask* td = g_task_get_task_data(task);
    if (td->unchanged) return; /* the tree shown */
    if (td->stats == NULL) {
        show_stats(v, NULL);
        gtk_label_set_text(GTK_LABEL(v->summary), _("The files of this repository cannot be read."));
        return;
    }
    show_stats(v, g_steal_pointer(&td->stats));
}

static void start_read(GitCodeView* v) {
    cancel_read(v);
    v->stale = FALSE;
    v->cancellable = g_cancellable_new();
    ReadTask* td = g_new0(ReadTask, 1);
    td->root = g_strdup(v->root);
    td->unless_tree = v->stats != NULL ? g_strdup(v->stats->tree) : NULL;
    if (v->stats == NULL) gtk_label_set_text(GTK_LABEL(v->summary), _("Counting the files…"));
    GTask* task = g_task_new(NULL, v->cancellable, on_read_ready, v);
    g_task_set_task_data(task, td, read_task_free);
    g_task_run_in_thread(task, read_thread);
    g_object_unref(task);
}

void git_code_view_set_root(GitCodeView* v, const gchar* root, gboolean reload) {
    if (v == NULL) return;
    if (g_strcmp0(v->root, root) != 0) {
        cancel_read(v);
        g_free(v->root);
        v->root = g_strdup(root);
        show_stats(v, NULL);
        gtk_label_set_text(GTK_LABEL(v->summary), "");
    } else if (!reload) {
        return;
    }
    if (v->root == NULL) return;
    v->stale = !gtk_widget_get_mapped(v->widget);
    if (!v->stale) start_read(v);
}

static void on_view_map(GtkWidget* widget, gpointer data) {
    (void)widget;
    GitCodeView* v = data;
    if (v->stale && v->root != NULL) start_read(v);
}

void git_code_view_cancel(GitCodeView* v) {
    if (v != NULL) cancel_read(v);
}

static void git_code_view_free(gpointer data) {
    GitCodeView* v = data;
    cancel_read(v);
    git_code_stats_free(v->stats);
    g_array_unref(v->parts);
    g_free(v->root);
    g_free(v);
}

GitCodeView* git_code_view_new(void) {
    ensure_css();
    GitCodeView* v = g_new0(GitCodeView, 1);
    v->parts = g_array_new(FALSE, TRUE, sizeof(Part));
    g_array_set_clear_func(v->parts, part_clear);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    v->summary = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(v->summary), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(v->summary), TRUE);
    gtk_widget_add_css_class(v->summary, "git-stats-summary");
    gtk_box_append(GTK_BOX(box), v->summary);

    v->bar = gtk_drawing_area_new();
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(v->bar), (int)BAR_H + 4);
    gtk_widget_set_hexpand(v->bar, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(v->bar), draw_bar, v, NULL);
    gtk_widget_set_has_tooltip(v->bar, TRUE);
    g_signal_connect(v->bar, "query-tooltip", G_CALLBACK(on_bar_tooltip), v);
    gtk_widget_set_visible(v->bar, FALSE);
    gtk_box_append(GTK_BOX(box), v->bar);

    v->table = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(v->table), 16);
    gtk_grid_set_row_spacing(GTK_GRID(v->table), 2);
    gtk_widget_set_halign(v->table, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), v->table);

    v->widget = box;
    g_object_set_data_full(G_OBJECT(box), CODE_VIEW_KEY, v, git_code_view_free);
    g_signal_connect(box, "map", G_CALLBACK(on_view_map), v);
    return v;
}

GtkWidget* git_code_view_widget(GitCodeView* v) {
    return v != NULL ? v->widget : NULL;
}
