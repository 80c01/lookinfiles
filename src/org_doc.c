/* Org-mode document structure and editing commands on an array of lines. */
#include "org_doc.h"
#include "org_time.h"

#include <stdlib.h>
#include <string.h>

#define ORG_TAGS_COLUMN 77

/* ------------------------------------------------------------------------- */
/* Lines and text helpers                                                    */
/* ------------------------------------------------------------------------- */

GPtrArray* org_lines_new(const gchar* text) {
    GPtrArray* lines = g_ptr_array_new_with_free_func(g_free);
    gchar** parts = g_strsplit(text != NULL ? text : "", "\n", -1);
    for (gint i = 0; parts[i] != NULL; i++) {
        g_ptr_array_add(lines, parts[i]);
    }
    g_free(parts); /* the strings now belong to lines */
    if (lines->len == 0) g_ptr_array_add(lines, g_strdup(""));
    return lines;
}

gchar* org_lines_join(GPtrArray* lines) {
    GString* out = g_string_new(NULL);
    for (guint i = 0; i < lines->len; i++) {
        if (i > 0) g_string_append_c(out, '\n');
        g_string_append(out, (const gchar*)g_ptr_array_index(lines, i));
    }
    return g_string_free(out, FALSE);
}

const gchar* org_line(GPtrArray* lines, gint i) {
    if (lines == NULL || i < 0 || (guint)i >= lines->len) return "";
    return (const gchar*)g_ptr_array_index(lines, i);
}

static gint n_lines(GPtrArray* lines) {
    return (gint)lines->len;
}

/* Lines that hold content: a final empty line (the text ended with a
 * newline) never belongs to a subtree or an item. */
static gint doc_limit(GPtrArray* lines) {
    gint n = n_lines(lines);
    if (n > 1 && org_line(lines, n - 1)[0] == '\0') return n - 1;
    return n;
}

static void set_line(GPtrArray* lines, gint i, gchar* text) {
    g_free(g_ptr_array_index(lines, i));
    g_ptr_array_index(lines, i) = text;
}

static void insert_line(GPtrArray* lines, gint i, gchar* text) {
    g_ptr_array_insert(lines, i, text);
}

static void swap_blocks(GPtrArray* lines, gint start, gint mid, gint end) {
    gint n = end - start;
    gpointer* tmp = g_new(gpointer, n);
    gint k = 0;
    for (gint i = mid; i < end; i++) tmp[k++] = g_ptr_array_index(lines, i);
    for (gint i = start; i < mid; i++) tmp[k++] = g_ptr_array_index(lines, i);
    for (gint i = 0; i < n; i++) g_ptr_array_index(lines, start + i) = tmp[i];
    g_free(tmp);
}

gint org_utf8_width(const gchar* s) {
    if (s == NULL) return 0;
    return (gint)g_utf8_strlen(s, -1);
}

gint org_col_to_byte(const gchar* s, gint col) {
    if (s == NULL || col <= 0) return 0;
    const gchar* p = s;
    for (gint i = 0; i < col && *p != '\0'; i++) {
        p = g_utf8_next_char(p);
    }
    return (gint)(p - s);
}

gint org_byte_to_col(const gchar* s, gint byte) {
    if (s == NULL || byte <= 0) return 0;
    gint len = (gint)strlen(s);
    if (byte > len) byte = len;
    return (gint)g_utf8_pointer_to_offset(s, s + byte);
}

static gboolean is_blank(const gchar* s) {
    for (; *s != '\0'; s++) {
        if (*s != ' ' && *s != '\t') return FALSE;
    }
    return TRUE;
}

/* Leading whitespace width (tab = 8 columns) */
static gint indent_of(const gchar* s) {
    gint n = 0;
    for (; *s == ' ' || *s == '\t'; s++) n += (*s == '\t') ? 8 : 1;
    return n;
}

/* Byte length of the leading whitespace */
static gint indent_bytes(const gchar* s) {
    gint n = 0;
    while (s[n] == ' ' || s[n] == '\t') n++;
    return n;
}

static gboolean has_prefix_ci(const gchar* s, const gchar* prefix) {
    return g_ascii_strncasecmp(s, prefix, strlen(prefix)) == 0;
}

static void clamp_pos(GPtrArray* lines, OrgPos* pos) {
    if (pos->line < 0) pos->line = 0;
    if (pos->line >= n_lines(lines)) pos->line = n_lines(lines) - 1;
    gint w = org_utf8_width(org_line(lines, pos->line));
    if (pos->col < 0) pos->col = 0;
    if (pos->col > w) pos->col = w;
}

static void set_msg(gchar** msg, const gchar* text) {
    if (msg != NULL) {
        g_free(*msg);
        *msg = g_strdup(text);
    }
}

OrgStartupVisibility org_startup_visibility(GPtrArray* lines) {
    OrgStartupVisibility vis = ORG_STARTUP_OVERVIEW;
    for (gint i = 0; i < n_lines(lines); i++) {
        const gchar* l = org_line(lines, i);
        if (l[0] != '#' || !has_prefix_ci(l, "#+STARTUP:")) continue;
        gchar** words = g_strsplit_set(l + 10, " \t", -1);
        for (gint k = 0; words[k] != NULL; k++) {
            gchar* w = g_ascii_strdown(words[k], -1);
            if (strcmp(w, "overview") == 0 || strcmp(w, "fold") == 0) vis = ORG_STARTUP_OVERVIEW;
            else if (strcmp(w, "content") == 0) vis = ORG_STARTUP_CONTENTS;
            else if (strcmp(w, "showall") == 0 || strcmp(w, "nofold") == 0 ||
                     strcmp(w, "showeverything") == 0) vis = ORG_STARTUP_SHOW_ALL;
            g_free(w);
        }
        g_strfreev(words);
    }
    return vis;
}

/* ------------------------------------------------------------------------- */
/* TODO configuration                                                        */
/* ------------------------------------------------------------------------- */

static void cfg_add_sequence(OrgTodoConfig* cfg, const gchar* spec) {
    gchar** words = g_strsplit_set(spec, " \t", -1);
    GPtrArray* seq = g_ptr_array_new_with_free_func(g_free);
    gint bar = -1;
    for (gint i = 0; words[i] != NULL; i++) {
        if (words[i][0] == '\0') continue;
        if (strcmp(words[i], "|") == 0) {
            bar = (gint)seq->len;
            continue;
        }
        /* "TODO(t)" or "WAIT(w@/!)": keep the keyword */
        gchar* kw = g_strdup(words[i]);
        gchar* paren = strchr(kw, '(');
        if (paren != NULL) *paren = '\0';
        if (kw[0] == '\0') {
            g_free(kw);
            continue;
        }
        g_ptr_array_add(seq, kw);
    }
    g_strfreev(words);
    if (seq->len == 0) {
        g_ptr_array_unref(seq);
        return;
    }
    if (bar < 0) bar = (gint)seq->len - 1; /* the last keyword means done */
    for (guint i = (guint)bar; i < seq->len; i++) {
        g_hash_table_add(cfg->done, g_strdup(g_ptr_array_index(seq, i)));
    }
    g_ptr_array_add(cfg->sequences, seq);
}

OrgTodoConfig* org_todo_config_new(GPtrArray* lines) {
    OrgTodoConfig* cfg = g_new0(OrgTodoConfig, 1);
    cfg->sequences = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);
    cfg->done = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (gint i = 0; lines != NULL && i < n_lines(lines); i++) {
        const gchar* l = org_line(lines, i);
        if (l[0] != '#' || l[1] != '+') continue;
        const gchar* rest = NULL;
        if (has_prefix_ci(l, "#+TODO:")) rest = l + 7;
        else if (has_prefix_ci(l, "#+SEQ_TODO:")) rest = l + 11;
        else if (has_prefix_ci(l, "#+TYP_TODO:")) rest = l + 11;
        if (rest != NULL) {
            cfg_add_sequence(cfg, rest);
        } else if (has_prefix_ci(l, "#+STARTUP:")) {
            gchar* low = g_ascii_strdown(l + 10, -1);
            if (strstr(low, "nologdone") != NULL) cfg->log_done = FALSE;
            else if (strstr(low, "logdone") != NULL || strstr(low, "lognotedone") != NULL) cfg->log_done = TRUE;
            g_free(low);
        }
    }
    if (cfg->sequences->len == 0) cfg_add_sequence(cfg, "TODO | DONE");
    return cfg;
}

void org_todo_config_free(OrgTodoConfig* cfg) {
    if (cfg == NULL) return;
    g_ptr_array_unref(cfg->sequences);
    g_hash_table_destroy(cfg->done);
    g_free(cfg);
}

/* Sequence index and keyword index of word, FALSE if not a keyword */
static gboolean cfg_find(const OrgTodoConfig* cfg, const gchar* word, guint* s, guint* k) {
    if (cfg == NULL || word == NULL) return FALSE;
    for (guint i = 0; i < cfg->sequences->len; i++) {
        GPtrArray* seq = g_ptr_array_index(cfg->sequences, i);
        for (guint j = 0; j < seq->len; j++) {
            if (strcmp(g_ptr_array_index(seq, j), word) == 0) {
                if (s) *s = i;
                if (k) *k = j;
                return TRUE;
            }
        }
    }
    return FALSE;
}

gboolean org_todo_is_keyword(const OrgTodoConfig* cfg, const gchar* word) {
    return cfg_find(cfg, word, NULL, NULL);
}

gboolean org_todo_is_done(const OrgTodoConfig* cfg, const gchar* word) {
    return cfg != NULL && word != NULL && g_hash_table_contains(cfg->done, word);
}

static const gchar* cfg_first_todo(const OrgTodoConfig* cfg) {
    GPtrArray* seq = g_ptr_array_index(cfg->sequences, 0);
    return g_ptr_array_index(seq, 0);
}

/* ------------------------------------------------------------------------- */
/* Headings                                                                  */
/* ------------------------------------------------------------------------- */

gint org_heading_level(const gchar* line) {
    if (line == NULL || line[0] != '*') return 0;
    gint level = 0;
    while (line[level] == '*') level++;
    return line[level] == ' ' ? level : 0;
}

/* A tag string ":a:b:" (no empty tag, no spaces) */
static gboolean is_tag_token(const gchar* s, gsize len) {
    if (len < 3 || s[0] != ':' || s[len - 1] != ':') return FALSE;
    for (gsize i = 1; i < len; i++) {
        if (s[i] == ':' && s[i - 1] == ':') return FALSE;
        if (s[i] == ' ' || s[i] == '\t') return FALSE;
    }
    return TRUE;
}

gboolean org_heading_parse(const gchar* line, const OrgTodoConfig* cfg, OrgHeading* h) {
    memset(h, 0, sizeof(*h));
    h->level = org_heading_level(line);
    if (h->level == 0) return FALSE;
    const gchar* p = line + h->level;
    while (*p == ' ') p++;

    /* Keyword */
    const gchar* w = p;
    while (*w != '\0' && *w != ' ') w++;
    if (w > p) {
        gchar* word = g_strndup(p, w - p);
        if (org_todo_is_keyword(cfg, word)) {
            h->keyword = word;
            p = w;
            while (*p == ' ') p++;
        } else {
            g_free(word);
        }
    }
    /* Priority cookie */
    if (p[0] == '[' && p[1] == '#' && g_ascii_isalnum(p[2]) && p[3] == ']' &&
        (p[4] == ' ' || p[4] == '\0')) {
        h->priority = p[2];
        p += 4;
        while (*p == ' ') p++;
    }
    /* Tags: last word when it is ":a:b:" */
    gchar* rest = g_strdup(p);
    g_strchomp(rest);
    gsize len = strlen(rest);
    gchar* sp = strrchr(rest, ' ');
    gchar* tok = sp != NULL ? sp + 1 : rest;
    gsize tlen = len - (gsize)(tok - rest);
    if (is_tag_token(tok, tlen) && (sp != NULL || tok == rest)) {
        h->tags = g_strdup(tok);
        *tok = '\0';
        g_strchomp(rest);
    }
    h->title = rest;
    return TRUE;
}

void org_heading_clear(OrgHeading* h) {
    g_free(h->keyword);
    g_free(h->title);
    g_free(h->tags);
    memset(h, 0, sizeof(*h));
}

gchar* org_heading_build(const OrgHeading* h) {
    GString* out = g_string_new(NULL);
    for (gint i = 0; i < h->level; i++) g_string_append_c(out, '*');
    if (h->keyword != NULL) g_string_append_printf(out, " %s", h->keyword);
    if (h->priority != 0) g_string_append_printf(out, " [#%c]", h->priority);
    if (h->title != NULL && h->title[0] != '\0') g_string_append_printf(out, " %s", h->title);
    if (out->len == (gsize)h->level) g_string_append_c(out, ' '); /* "* " stays a heading */
    if (h->tags != NULL && h->tags[0] != '\0') {
        gint w = org_utf8_width(out->str);
        gint target = ORG_TAGS_COLUMN - org_utf8_width(h->tags);
        gint pad = (target - w > 1) ? target - w : 1;
        if (out->str[out->len - 1] == ' ') pad = MAX(pad - 1, 0);
        for (gint i = 0; i < pad; i++) g_string_append_c(out, ' ');
        g_string_append(out, h->tags);
    }
    return g_string_free(out, FALSE);
}

/* Column where the title starts in the line org_heading_build() makes */
static gint heading_title_col(const OrgHeading* h) {
    gint col = h->level + 1;
    if (h->keyword != NULL) col += org_utf8_width(h->keyword) + 1;
    if (h->priority != 0) col += 5;
    return col;
}

/* Replace heading line h by the rebuilt parsed heading; keep the cursor on
 * the same text when it is on that line */
static void heading_store(GPtrArray* lines, OrgPos* pos, gint h, gint old_title_col,
                          const OrgHeading* parsed) {
    gchar* text = org_heading_build(parsed);
    set_line(lines, h, text);
    if (pos != NULL && pos->line == h) {
        gint new_col = heading_title_col(parsed);
        if (pos->col >= old_title_col) {
            pos->col += new_col - old_title_col;
        } else if (pos->col > parsed->level) {
            pos->col = MIN(pos->col, new_col);
        }
        clamp_pos(lines, pos);
    }
}

gint org_heading_at(GPtrArray* lines, gint line) {
    if (line >= n_lines(lines)) line = n_lines(lines) - 1;
    for (gint i = line; i >= 0; i--) {
        if (org_heading_level(org_line(lines, i)) > 0) return i;
    }
    return -1;
}

gint org_heading_parent(GPtrArray* lines, gint h) {
    gint level = org_heading_level(org_line(lines, h));
    if (level <= 1) return -1;
    for (gint i = h - 1; i >= 0; i--) {
        gint l = org_heading_level(org_line(lines, i));
        if (l > 0 && l < level) return i;
    }
    return -1;
}

gint org_subtree_end(GPtrArray* lines, gint h) {
    gint limit = doc_limit(lines);
    gint level = org_heading_level(org_line(lines, h));
    if (level == 0) return MIN(h + 1, limit);
    gint i = h + 1;
    for (; i < limit; i++) {
        gint l = org_heading_level(org_line(lines, i));
        if (l > 0 && l <= level) break;
    }
    return i;
}

gint org_entry_end(GPtrArray* lines, gint h) {
    gint limit = doc_limit(lines);
    gint i = h + 1;
    while (i < limit && org_heading_level(org_line(lines, i)) == 0) i++;
    return i;
}

gint org_heading_next(GPtrArray* lines, gint line, gint dir, gboolean same_level) {
    gint limit = doc_limit(lines);
    if (!same_level) {
        for (gint i = line + dir; i >= 0 && i < limit; i += dir) {
            if (org_heading_level(org_line(lines, i)) > 0) return i;
        }
        return -1;
    }
    gint h = org_heading_at(lines, line);
    if (h < 0) return -1;
    gint level = org_heading_level(org_line(lines, h));
    if (dir > 0) {
        gint e = org_subtree_end(lines, h);
        return (e < limit && org_heading_level(org_line(lines, e)) == level) ? e : -1;
    }
    for (gint i = h - 1; i >= 0; i--) {
        gint l = org_heading_level(org_line(lines, i));
        if (l > 0 && l <= level) return l == level ? i : -1;
    }
    return -1;
}

gint org_heading_find_title(GPtrArray* lines, const OrgTodoConfig* cfg, const gchar* title) {
    gint fallback = -1;
    for (gint i = 0; i < n_lines(lines); i++) {
        OrgHeading h;
        if (!org_heading_parse(org_line(lines, i), cfg, &h)) continue;
        gboolean exact = g_strcmp0(h.title, title) == 0;
        gboolean loose = !exact && g_ascii_strcasecmp(h.title, title) == 0;
        org_heading_clear(&h);
        if (exact) return i;
        if (loose && fallback < 0) fallback = i;
    }
    return fallback;
}

/* ":NAME:" drawer start (not ":END:") */
static gboolean is_drawer_start(const gchar* line) {
    const gchar* p = line + indent_bytes(line);
    if (*p != ':') return FALSE;
    const gchar* q = p + 1;
    while (g_ascii_isalnum(*q) || *q == '_' || *q == '-') q++;
    if (q == p + 1 || *q != ':' || !is_blank(q + 1)) return FALSE;
    return !has_prefix_ci(p, ":END:");
}

static gboolean is_drawer_end(const gchar* line) {
    const gchar* p = line + indent_bytes(line);
    return has_prefix_ci(p, ":END:") && is_blank(p + 5);
}

OrgFoldKind org_fold_kind(GPtrArray* lines, gint line, gint* last) {
    const gchar* l = org_line(lines, line);
    gint limit = doc_limit(lines);
    if (org_heading_level(l) > 0) {
        if (last) *last = org_subtree_end(lines, line) - 1;
        return ORG_FOLD_HEADING;
    }
    if (is_drawer_start(l)) {
        for (gint i = line + 1; i < limit; i++) {
            const gchar* t = org_line(lines, i);
            if (org_heading_level(t) > 0) break;
            if (is_drawer_end(t)) {
                if (last) *last = i;
                return ORG_FOLD_DRAWER;
            }
        }
        return ORG_FOLD_NONE;
    }
    const gchar* p = l + indent_bytes(l);
    if (has_prefix_ci(p, "#+begin_")) {
        const gchar* name = p + 8;
        gsize nlen = 0;
        while (name[nlen] != '\0' && name[nlen] != ' ' && name[nlen] != '\t') nlen++;
        if (nlen == 0) return ORG_FOLD_NONE;
        gchar* end_kw = g_strdup_printf("#+end_%.*s", (gint)nlen, name);
        for (gint i = line + 1; i < limit; i++) {
            const gchar* t = org_line(lines, i);
            if (org_heading_level(t) > 0) break;
            if (has_prefix_ci(t + indent_bytes(t), end_kw)) {
                g_free(end_kw);
                if (last) *last = i;
                return ORG_FOLD_BLOCK;
            }
        }
        g_free(end_kw);
    }
    return ORG_FOLD_NONE;
}

/* ------------------------------------------------------------------------- */
/* Structure editing                                                         */
/* ------------------------------------------------------------------------- */

static gchar* stars(gint level) {
    return g_strnfill(level, '*');
}

gboolean org_promote(GPtrArray* lines, OrgPos* pos, gint dir, gboolean subtree, gchar** msg) {
    gint h = pos->line;
    gint level = org_heading_level(org_line(lines, h));
    if (level == 0) return FALSE;
    if (dir < 0 && level == 1) {
        set_msg(msg, "Cannot promote to level 0");
        return FALSE;
    }
    gint end = subtree ? org_subtree_end(lines, h) : h + 1;
    for (gint i = h; i < end; i++) {
        const gchar* l = org_line(lines, i);
        if (org_heading_level(l) == 0) continue;
        set_line(lines, i, dir > 0 ? g_strconcat("*", l, NULL) : g_strdup(l + 1));
    }
    if (pos->col >= level) pos->col += dir > 0 ? 1 : -1;
    clamp_pos(lines, pos);
    return TRUE;
}

gboolean org_move_subtree(GPtrArray* lines, OrgPos* pos, gint dir, OrgSwap* swap, gchar** msg) {
    gint h = org_heading_at(lines, pos->line);
    if (h < 0) return FALSE;
    gint level = org_heading_level(org_line(lines, h));
    gint e = org_subtree_end(lines, h);
    OrgSwap sw;
    if (dir < 0) {
        gint p = org_heading_next(lines, h, -1, TRUE);
        if (p < 0) {
            set_msg(msg, "Cannot move past superior level or buffer limit");
            return FALSE;
        }
        sw.start = p;
        sw.mid = h;
        sw.end = e;
        pos->line -= h - p;
    } else {
        gint limit = doc_limit(lines);
        if (e >= limit || org_heading_level(org_line(lines, e)) != level) {
            set_msg(msg, "Cannot move past superior level or buffer limit");
            return FALSE;
        }
        gint e2 = org_subtree_end(lines, e);
        sw.start = h;
        sw.mid = e;
        sw.end = e2;
        pos->line += e2 - e;
    }
    swap_blocks(lines, sw.start, sw.mid, sw.end);
    if (swap != NULL) *swap = sw;
    return TRUE;
}

gboolean org_insert_heading(GPtrArray* lines, OrgPos* pos, const OrgTodoConfig* cfg,
                            gboolean respect_content, gboolean todo) {
    gint line = pos->line;
    const gchar* cur = org_line(lines, line);
    gint h = org_heading_at(lines, line);
    gint level = h >= 0 ? org_heading_level(org_line(lines, h)) : 1;
    gchar* st = stars(level);
    gchar* prefix = (todo && cfg != NULL)
                        ? g_strdup_printf("%s %s ", st, cfg_first_todo(cfg))
                        : g_strdup_printf("%s ", st);
    g_free(st);
    gint at;
    gchar* title = g_strdup("");

    if (!respect_content && h == line && pos->col == 0) {
        at = line; /* at the start of a heading: insert above */
    } else if (respect_content || h == line) {
        if (h < 0) {
            at = doc_limit(lines);
        } else {
            at = org_subtree_end(lines, h);
            while (at - 1 > h && is_blank(org_line(lines, at - 1))) at--;
        }
    } else if (is_blank(cur)) {
        /* An empty line becomes the heading */
        g_free(title);
        set_line(lines, line, g_strconcat(prefix, "", NULL));
        pos->col = org_utf8_width(prefix);
        g_free(prefix);
        return TRUE;
    } else if (pos->col == 0) {
        at = line;
    } else {
        /* Split the line: the text after the cursor becomes the title */
        gint b = org_col_to_byte(cur, pos->col);
        g_free(title);
        title = g_strdup(cur + b);
        g_strchug(title);
        gchar* head = g_strndup(cur, b);
        g_strchomp(head);
        set_line(lines, line, head);
        at = line + 1;
    }
    insert_line(lines, at, g_strconcat(prefix, title, NULL));
    pos->line = at;
    pos->col = org_utf8_width(prefix);
    g_free(title);
    g_free(prefix);
    return TRUE;
}

gboolean org_toggle_heading(GPtrArray* lines, OrgPos* pos) {
    const gchar* l = org_line(lines, pos->line);
    gint level = org_heading_level(l);
    if (level > 0) {
        set_line(lines, pos->line, g_strdup(l + level + 1));
        pos->col = MAX(0, pos->col - level - 1);
        return TRUE;
    }
    if (is_blank(l)) return FALSE;
    gint h = org_heading_at(lines, pos->line);
    gint new_level = h >= 0 ? org_heading_level(org_line(lines, h)) + 1 : 1;
    gchar* st = stars(new_level);
    OrgItem it;
    gchar* text;
    if (org_item_parse(l, &it)) {
        const gchar* kw = it.checkbox == 'X' ? "DONE " : (it.checkbox != 0 ? "TODO " : "");
        text = g_strdup_printf("%s %s%s", st, kw, l + it.content);
    } else {
        text = g_strdup_printf("%s %s", st, l + indent_bytes(l));
    }
    g_free(st);
    set_line(lines, pos->line, text);
    pos->col = org_utf8_width(text);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Planning lines (SCHEDULED / DEADLINE / CLOSED)                            */
/* ------------------------------------------------------------------------- */

static const gchar* PLANNING_KW[] = {"CLOSED:", "DEADLINE:", "SCHEDULED:"};

static gboolean is_planning_line(const gchar* line) {
    const gchar* p = line + indent_bytes(line);
    for (guint i = 0; i < G_N_ELEMENTS(PLANNING_KW); i++) {
        if (g_str_has_prefix(p, PLANNING_KW[i])) return TRUE;
    }
    return FALSE;
}

/* Timestamp text after keyword kw ("SCHEDULED:") on a planning line */
static gchar* planning_get(const gchar* line, const gchar* kw) {
    const gchar* p = strstr(line, kw);
    if (p == NULL) return NULL;
    p += strlen(kw);
    while (*p == ' ') p++;
    gchar close = *p == '<' ? '>' : (*p == '[' ? ']' : 0);
    if (close == 0) return NULL;
    const gchar* e = strchr(p, close);
    if (e == NULL) return NULL;
    return g_strndup(p, e - p + 1);
}

gboolean org_set_planning(GPtrArray* lines, OrgPos* pos, gint h, const gchar* keyword,
                          const gchar* ts_text) {
    if (org_heading_level(org_line(lines, h)) == 0) return FALSE;
    gchar* kw = g_strconcat(keyword, ":", NULL);
    gint pl = h + 1;
    gboolean exists = pl < doc_limit(lines) && is_planning_line(org_line(lines, pl));
    gchar* values[G_N_ELEMENTS(PLANNING_KW)] = {NULL};
    gchar* indent = g_strdup("");
    if (exists) {
        const gchar* l = org_line(lines, pl);
        g_free(indent);
        indent = g_strndup(l, indent_bytes(l));
        for (guint i = 0; i < G_N_ELEMENTS(PLANNING_KW); i++) {
            values[i] = planning_get(l, PLANNING_KW[i]);
        }
    }
    for (guint i = 0; i < G_N_ELEMENTS(PLANNING_KW); i++) {
        if (strcmp(PLANNING_KW[i], kw) == 0) {
            g_free(values[i]);
            values[i] = ts_text != NULL ? g_strdup(ts_text) : NULL;
        }
    }
    GString* out = g_string_new(indent);
    for (guint i = 0; i < G_N_ELEMENTS(PLANNING_KW); i++) {
        if (values[i] == NULL) continue;
        if (out->len > strlen(indent)) g_string_append_c(out, ' ');
        g_string_append_printf(out, "%s %s", PLANNING_KW[i], values[i]);
        g_free(values[i]);
    }
    gboolean empty = out->len == strlen(indent);
    g_free(indent);
    g_free(kw);
    if (exists && empty) {
        g_string_free(out, TRUE);
        g_ptr_array_remove_index(lines, pl);
        if (pos->line > pl) pos->line--;
        else if (pos->line == pl) {
            pos->line = h;
            pos->col = org_utf8_width(org_line(lines, h));
        }
    } else if (exists) {
        set_line(lines, pl, g_string_free(out, FALSE));
        if (pos->line == pl) clamp_pos(lines, pos);
    } else if (!empty) {
        insert_line(lines, pl, g_string_free(out, FALSE));
        if (pos->line >= pl) pos->line++;
    } else {
        g_string_free(out, TRUE);
    }
    return TRUE;
}

/* Shift the repeating SCHEDULED / DEADLINE timestamps of heading h.
 * Returns the new text of the first one shifted (for the message) or NULL. */
static gchar* apply_repeaters(GPtrArray* lines, gint h, GDateTime* now) {
    gint pl = h + 1;
    if (pl >= doc_limit(lines) || !is_planning_line(org_line(lines, pl))) return NULL;
    gchar* first = NULL;
    GString* l = g_string_new(org_line(lines, pl));
    for (guint k = 1; k < G_N_ELEMENTS(PLANNING_KW); k++) {
        gchar* ts_text = planning_get(l->str, PLANNING_KW[k]);
        if (ts_text == NULL) continue;
        OrgTimestamp ts;
        if (org_timestamp_parse(ts_text, strlen(ts_text), &ts) &&
            org_timestamp_apply_repeater(&ts, now)) {
            gchar* nt = org_timestamp_format(&ts);
            const gchar* at = strstr(l->str, ts_text);
            gssize off = at - l->str;
            g_string_erase(l, off, (gssize)strlen(ts_text));
            g_string_insert(l, off, nt);
            if (first == NULL) first = g_strdup_printf("%s %s", PLANNING_KW[k], nt);
            g_free(nt);
        }
        g_free(ts_text);
    }
    set_line(lines, pl, g_string_free(l, FALSE));
    return first;
}

/* ------------------------------------------------------------------------- */
/* TODO, priority, tags                                                      */
/* ------------------------------------------------------------------------- */

gboolean org_todo_set(GPtrArray* lines, OrgPos* pos, gint h, const OrgTodoConfig* cfg,
                      const gchar* keyword, GDateTime* now, gchar** msg) {
    OrgHeading hd;
    if (!org_heading_parse(org_line(lines, h), cfg, &hd)) return FALSE;
    gint old_tc = heading_title_col(&hd);
    gchar* old = hd.keyword;
    gboolean was_done = org_todo_is_done(cfg, old);
    gboolean now_done = org_todo_is_done(cfg, keyword);
    hd.keyword = g_strdup(keyword);

    if (now_done && !was_done && now != NULL) {
        gchar* repeated = apply_repeaters(lines, h, now);
        if (repeated != NULL) {
            /* A repeating entry goes back to its previous state */
            g_free(hd.keyword);
            hd.keyword = g_strdup(old != NULL ? old : cfg_first_todo(cfg));
            gchar* m = g_strdup_printf("Entry repeats: %s", repeated);
            set_msg(msg, m);
            g_free(m);
            g_free(repeated);
        } else if (cfg->log_done) {
            OrgTimestamp ts;
            org_timestamp_from_datetime(&ts, now, FALSE, TRUE);
            gchar* t = org_timestamp_format(&ts);
            heading_store(lines, pos, h, old_tc, &hd);
            org_set_planning(lines, pos, h, "CLOSED", t);
            g_free(t);
            goto done;
        }
    } else if (was_done && !now_done) {
        heading_store(lines, pos, h, old_tc, &hd);
        gint pl = h + 1;
        if (pl < doc_limit(lines) && is_planning_line(org_line(lines, pl)) &&
            strstr(org_line(lines, pl), "CLOSED:") != NULL) {
            org_set_planning(lines, pos, h, "CLOSED", NULL);
        }
        goto done;
    }
    heading_store(lines, pos, h, old_tc, &hd);

done:
    org_heading_clear(&hd);
    g_free(old);
    org_update_statistics(lines, cfg);
    return TRUE;
}

gboolean org_todo_cycle(GPtrArray* lines, OrgPos* pos, gint h, const OrgTodoConfig* cfg,
                        gint dir, GDateTime* now, gchar** msg) {
    OrgHeading hd;
    if (!org_heading_parse(org_line(lines, h), cfg, &hd)) return FALSE;
    const gchar* next = NULL;
    guint s, k;
    if (hd.keyword == NULL || !cfg_find(cfg, hd.keyword, &s, &k)) {
        GPtrArray* seq = g_ptr_array_index(cfg->sequences, 0);
        next = g_ptr_array_index(seq, dir > 0 ? 0 : seq->len - 1);
    } else {
        GPtrArray* seq = g_ptr_array_index(cfg->sequences, s);
        gint nk = (gint)k + (dir > 0 ? 1 : -1);
        next = (nk >= 0 && nk < (gint)seq->len) ? g_ptr_array_index(seq, nk) : NULL;
    }
    org_heading_clear(&hd);
    return org_todo_set(lines, pos, h, cfg, next, now, msg);
}

gboolean org_priority(GPtrArray* lines, OrgPos* pos, gint h, const OrgTodoConfig* cfg,
                      gint dir, gchar set) {
    OrgHeading hd;
    if (!org_heading_parse(org_line(lines, h), cfg, &hd)) return FALSE;
    gint old_tc = heading_title_col(&hd);
    gchar p = hd.priority;
    if (set == ' ') {
        p = 0;
    } else if (set != 0) {
        p = g_ascii_toupper(set);
    } else if (dir > 0) {
        p = (p == 0) ? 'A' : (p <= 'A' ? 0 : p - 1);
    } else {
        p = (p == 0) ? 'C' : (p >= 'C' ? 0 : p + 1);
    }
    hd.priority = p;
    heading_store(lines, pos, h, old_tc, &hd);
    org_heading_clear(&hd);
    return TRUE;
}

gchar* org_get_tags(GPtrArray* lines, gint h, const OrgTodoConfig* cfg) {
    OrgHeading hd;
    if (!org_heading_parse(org_line(lines, h), cfg, &hd)) return NULL;
    gchar* tags = g_strdup(hd.tags);
    org_heading_clear(&hd);
    return tags;
}

gboolean org_set_tags(GPtrArray* lines, OrgPos* pos, gint h, const OrgTodoConfig* cfg,
                      const gchar* tags) {
    OrgHeading hd;
    if (!org_heading_parse(org_line(lines, h), cfg, &hd)) return FALSE;
    gint old_tc = heading_title_col(&hd);
    gchar** parts = g_strsplit_set(tags != NULL ? tags : "", ": ,\t", -1);
    GString* t = g_string_new(NULL);
    for (gint i = 0; parts[i] != NULL; i++) {
        if (parts[i][0] == '\0') continue;
        g_string_append_printf(t, ":%s", parts[i]);
    }
    g_strfreev(parts);
    if (t->len > 0) g_string_append_c(t, ':');
    g_free(hd.tags);
    hd.tags = t->len > 0 ? g_strdup(t->str) : NULL;
    g_string_free(t, TRUE);
    heading_store(lines, pos, h, old_tc, &hd);
    org_heading_clear(&hd);
    return TRUE;
}

static void add_tag_words(GHashTable* set, const gchar* text) {
    gchar** parts = g_strsplit_set(text, ": \t", -1);
    for (gint i = 0; parts[i] != NULL; i++) {
        /* "#+TAGS: { a b }" groups and "work(w)" fast keys */
        gchar* w = parts[i];
        gchar* paren = strchr(w, '(');
        if (paren != NULL) *paren = '\0';
        if (w[0] != '\0' && strchr("{}[]", w[0]) == NULL) g_hash_table_add(set, g_strdup(w));
    }
    g_strfreev(parts);
}

static gint compare_str(gconstpointer a, gconstpointer b) {
    return g_strcmp0(*(const gchar* const*)a, *(const gchar* const*)b);
}

GPtrArray* org_collect_tags(GPtrArray* lines, const OrgTodoConfig* cfg) {
    GHashTable* set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (gint i = 0; i < n_lines(lines); i++) {
        const gchar* l = org_line(lines, i);
        if (has_prefix_ci(l, "#+TAGS:")) {
            add_tag_words(set, l + 7);
            continue;
        }
        OrgHeading h;
        if (!org_heading_parse(l, cfg, &h)) continue;
        if (h.tags != NULL) add_tag_words(set, h.tags);
        org_heading_clear(&h);
    }
    GPtrArray* out = g_ptr_array_new_with_free_func(g_free);
    GHashTableIter it;
    gpointer key;
    g_hash_table_iter_init(&it, set);
    while (g_hash_table_iter_next(&it, &key, NULL)) {
        g_ptr_array_add(out, key);
        g_hash_table_iter_steal(&it);
    }
    g_hash_table_destroy(set);
    g_ptr_array_sort(out, compare_str);
    return out;
}

/* ------------------------------------------------------------------------- */
/* Clock                                                                     */
/* ------------------------------------------------------------------------- */

/* "CLOCK: [2026-10-29 Thu 10:00]" with no end yet */
static gboolean is_open_clock(const gchar* line, gchar** start_ts) {
    const gchar* p = line + indent_bytes(line);
    if (!g_str_has_prefix(p, "CLOCK: [")) return FALSE;
    p += 7;
    const gchar* e = strchr(p, ']');
    if (e == NULL || !is_blank(e + 1)) return FALSE;
    if (start_ts != NULL) *start_ts = g_strndup(p, e - p + 1);
    return TRUE;
}

gint org_clock_running(GPtrArray* lines) {
    for (gint i = 0; i < n_lines(lines); i++) {
        if (is_open_clock(org_line(lines, i), NULL)) return i;
    }
    return -1;
}

gboolean org_clock_out(GPtrArray* lines, OrgPos* pos, GDateTime* now, gchar** msg) {
    (void)pos;
    gint r = org_clock_running(lines);
    gchar* start_text = NULL;
    if (r < 0 || !is_open_clock(org_line(lines, r), &start_text)) {
        set_msg(msg, "No running clock");
        return FALSE;
    }
    OrgTimestamp start, end;
    if (!org_timestamp_parse(start_text, strlen(start_text), &start) || !start.has_time) {
        g_free(start_text);
        set_msg(msg, "Invalid clock line");
        return FALSE;
    }
    org_timestamp_from_datetime(&end, now, FALSE, TRUE);
    gint64 minutes = MAX(0, org_timestamp_minutes_between(&start, &end));
    gchar* end_text = org_timestamp_format(&end);
    const gchar* l = org_line(lines, r);
    gchar* text = g_strdup_printf("%.*sCLOCK: %s--%s => %2d:%02d", indent_bytes(l), l,
                                  start_text, end_text, (gint)(minutes / 60), (gint)(minutes % 60));
    set_line(lines, r, text);
    gchar* m = g_strdup_printf("Clock stopped at %s after %d:%02d", end_text,
                               (gint)(minutes / 60), (gint)(minutes % 60));
    set_msg(msg, m);
    g_free(m);
    g_free(end_text);
    g_free(start_text);
    return TRUE;
}

gboolean org_clock_in(GPtrArray* lines, OrgPos* pos, gint h, GDateTime* now, gchar** msg) {
    if (org_heading_level(org_line(lines, h)) == 0) return FALSE;
    if (org_clock_running(lines) >= 0) {
        org_clock_out(lines, pos, now, msg);
    }
    gint limit = doc_limit(lines);
    gint at = h + 1;
    if (at < limit && is_planning_line(org_line(lines, at))) at++;
    if (at < limit && has_prefix_ci(org_line(lines, at) + indent_bytes(org_line(lines, at)), ":PROPERTIES:")) {
        gint i = at + 1;
        while (i < limit && !is_drawer_end(org_line(lines, i)) &&
               org_heading_level(org_line(lines, i)) == 0) {
            i++;
        }
        if (i < limit && is_drawer_end(org_line(lines, i))) at = i + 1;
    }
    OrgTimestamp ts;
    org_timestamp_from_datetime(&ts, now, FALSE, TRUE);
    gchar* t = org_timestamp_format(&ts);
    gint added;
    const gchar* l = org_line(lines, at);
    if (at < limit && has_prefix_ci(l + indent_bytes(l), ":LOGBOOK:")) {
        insert_line(lines, at + 1, g_strdup_printf("CLOCK: %s", t));
        added = 1;
    } else {
        insert_line(lines, at, g_strdup(":END:"));
        insert_line(lines, at, g_strdup_printf("CLOCK: %s", t));
        insert_line(lines, at, g_strdup(":LOGBOOK:"));
        added = 3;
    }
    if (pos->line >= at) pos->line += added;
    gchar* m = (msg != NULL && *msg != NULL)
                   ? g_strdup_printf("%s; clock started at %s", *msg, t)
                   : g_strdup_printf("Clock started at %s", t);
    set_msg(msg, m);
    g_free(m);
    g_free(t);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Lists and checkboxes                                                      */
/* ------------------------------------------------------------------------- */

gboolean org_item_parse(const gchar* line, OrgItem* it) {
    memset(it, 0, sizeof(*it));
    if (line == NULL) return FALSE;
    gint b = indent_bytes(line);
    it->indent = indent_of(line);
    const gchar* p = line + b;
    gint blen = 0;
    if ((p[0] == '-' || p[0] == '+' || (p[0] == '*' && b > 0)) && (p[1] == ' ' || p[1] == '\0')) {
        blen = 1;
    } else if (g_ascii_isdigit(p[0])) {
        gint d = 0;
        while (g_ascii_isdigit(p[d]) && d < 9) d++;
        if ((p[d] == '.' || p[d] == ')') && (p[d + 1] == ' ' || p[d + 1] == '\0')) {
            it->ordered = TRUE;
            it->number = atoi(p);
            blen = d + 1;
        }
    }
    if (blen == 0) return FALSE;
    memcpy(it->bullet, p, blen);
    it->bullet[blen] = '\0';
    const gchar* q = p + blen;
    while (*q == ' ') q++;
    if (q[0] == '[' && (q[1] == ' ' || q[1] == 'X' || q[1] == 'x' || q[1] == '-') && q[2] == ']' &&
        (q[3] == ' ' || q[3] == '\0')) {
        it->checkbox = q[1] == 'x' ? 'X' : q[1];
        q += 3;
        while (*q == ' ') q++;
    }
    it->content = (gint)(q - line);
    return TRUE;
}

/* Byte offset of the '[' of the item's checkbox, -1 if none */
static gint checkbox_byte(const gchar* line) {
    OrgItem it;
    if (!org_item_parse(line, &it) || it.checkbox == 0) return -1;
    const gchar* q = line + indent_bytes(line) + strlen(it.bullet);
    while (*q == ' ') q++;
    return (gint)(q - line);
}

static void set_checkbox(GPtrArray* lines, gint i, gchar c) {
    gint b = checkbox_byte(org_line(lines, i));
    if (b < 0) return;
    gchar* l = g_strdup(org_line(lines, i));
    l[b + 1] = c;
    set_line(lines, i, l);
}

gint org_item_end(GPtrArray* lines, gint i) {
    gint limit = doc_limit(lines);
    gint ind = indent_of(org_line(lines, i));
    gint last = i;
    gint blanks = 0;
    for (gint j = i + 1; j < limit; j++) {
        const gchar* l = org_line(lines, j);
        if (is_blank(l)) {
            if (++blanks >= 2) break;
            continue;
        }
        blanks = 0;
        if (org_heading_level(l) > 0 || indent_of(l) <= ind) break;
        last = j;
    }
    return last + 1;
}

gint org_item_at(GPtrArray* lines, gint line) {
    OrgItem it;
    if (org_item_parse(org_line(lines, line), &it)) return line;
    for (gint j = line - 1; j >= 0 && j > line - 2000; j--) {
        const gchar* l = org_line(lines, j);
        if (org_heading_level(l) > 0) return -1;
        if (is_blank(l)) continue;
        if (org_item_parse(l, &it)) {
            if (org_item_end(lines, j) > line) return j;
            if (it.indent == 0) return -1;
        } else if (indent_of(l) == 0) {
            return -1;
        }
    }
    return -1;
}

gint org_item_parent(GPtrArray* lines, gint i) {
    gint ind = indent_of(org_line(lines, i));
    for (gint j = i - 1; j >= 0; j--) {
        const gchar* l = org_line(lines, j);
        if (is_blank(l)) continue;
        if (org_heading_level(l) > 0) return -1;
        gint li = indent_of(l);
        if (li >= ind) continue;
        OrgItem it;
        if (org_item_parse(l, &it) && org_item_end(lines, j) > i) return j;
        return -1;
    }
    return -1;
}

static gint item_prev_sibling(GPtrArray* lines, gint i) {
    gint ind = indent_of(org_line(lines, i));
    for (gint j = i - 1; j >= 0; j--) {
        const gchar* l = org_line(lines, j);
        if (is_blank(l)) continue;
        if (org_heading_level(l) > 0) return -1;
        gint li = indent_of(l);
        if (li > ind) continue;
        OrgItem it;
        if (li == ind && org_item_parse(l, &it) && org_item_end(lines, j) + 1 >= i) return j;
        return -1;
    }
    return -1;
}

static gint item_next_sibling(GPtrArray* lines, gint i) {
    gint ind = indent_of(org_line(lines, i));
    gint k = org_item_end(lines, i);
    gint limit = doc_limit(lines);
    if (k < limit && is_blank(org_line(lines, k))) k++;
    OrgItem it;
    if (k < limit && org_item_parse(org_line(lines, k), &it) && it.indent == ind) return k;
    return -1;
}

static gint item_first_sibling(GPtrArray* lines, gint i) {
    gint p;
    while ((p = item_prev_sibling(lines, i)) >= 0) i = p;
    return i;
}

/* Replace the bullet of item line i with bullet; keep the cursor in place */
static void set_bullet(GPtrArray* lines, OrgPos* pos, gint i, const gchar* bullet) {
    const gchar* l = org_line(lines, i);
    OrgItem it;
    if (!org_item_parse(l, &it)) return;
    gint b = indent_bytes(l);
    gint old_len = (gint)strlen(it.bullet);
    gchar* text = g_strdup_printf("%.*s%s%s", b, l, bullet, l + b + old_len);
    set_line(lines, i, text);
    if (pos != NULL && pos->line == i && pos->col > b) {
        pos->col += (gint)strlen(bullet) - old_len;
        clamp_pos(lines, pos);
    }
}

void org_renumber_list(GPtrArray* lines, gint i) {
    OrgItem it;
    if (!org_item_parse(org_line(lines, i), &it)) return;
    gint j = item_first_sibling(lines, i);
    if (!org_item_parse(org_line(lines, j), &it) || !it.ordered) return;
    gint n = 1;
    for (; j >= 0; j = item_next_sibling(lines, j)) {
        if (!org_item_parse(org_line(lines, j), &it) || !it.ordered) break;
        gchar delim = it.bullet[strlen(it.bullet) - 1];
        gchar* b = g_strdup_printf("%d%c", n++, delim);
        if (strcmp(b, it.bullet) != 0) set_bullet(lines, NULL, j, b);
        g_free(b);
    }
}

gboolean org_insert_item(GPtrArray* lines, OrgPos* pos, gboolean checkbox) {
    gint i = org_item_at(lines, pos->line);
    if (i < 0) return FALSE;
    const gchar* l = org_line(lines, i);
    OrgItem it;
    org_item_parse(l, &it);
    gchar* bullet = it.ordered ? g_strdup_printf("%d%c", it.number + 1, it.bullet[strlen(it.bullet) - 1])
                               : g_strdup(it.bullet);
    gchar* prefix = g_strdup_printf("%.*s%s %s", indent_bytes(l), l, bullet,
                                    (checkbox || it.checkbox != 0) ? "[ ] " : "");
    g_free(bullet);
    gint col_b = org_col_to_byte(l, pos->col);
    gint at;
    gchar* rest = g_strdup("");
    if (pos->line == i && col_b <= indent_bytes(l)) {
        at = i; /* before the bullet: new item above */
    } else {
        at = org_item_end(lines, i);
        if (pos->line == i && col_b >= it.content && l[col_b] != '\0') {
            g_free(rest);
            rest = g_strdup(l + col_b);
            g_strchug(rest);
            gchar* head = g_strndup(l, col_b);
            g_strchomp(head);
            set_line(lines, i, head);
        }
    }
    insert_line(lines, at, g_strconcat(prefix, rest, NULL));
    pos->line = at;
    pos->col = org_utf8_width(prefix);
    g_free(prefix);
    g_free(rest);
    org_renumber_list(lines, at);
    return TRUE;
}

/* Shift lines [from, to) by delta columns (negative removes leading spaces) */
static void shift_lines(GPtrArray* lines, gint from, gint to, gint delta) {
    for (gint j = from; j < to; j++) {
        const gchar* l = org_line(lines, j);
        if (is_blank(l)) continue;
        if (delta > 0) {
            gchar* pad = g_strnfill(delta, ' ');
            set_line(lines, j, g_strconcat(pad, l, NULL));
            g_free(pad);
        } else {
            gint k = 0;
            while (k < -delta && (l[k] == ' ' || l[k] == '\t')) k++;
            set_line(lines, j, g_strdup(l + k));
        }
    }
}

gboolean org_indent_item(GPtrArray* lines, OrgPos* pos, gint dir, gchar** msg) {
    gint i = org_item_at(lines, pos->line);
    if (i < 0) return FALSE;
    gint ind = indent_of(org_line(lines, i));
    gint e = org_item_end(lines, i);
    gint target;
    gint other;
    if (dir > 0) {
        other = item_prev_sibling(lines, i);
        if (other < 0) {
            set_msg(msg, "Cannot indent the first item of a list");
            return FALSE;
        }
        OrgItem p;
        org_item_parse(org_line(lines, other), &p);
        target = p.indent + (gint)strlen(p.bullet) + 1;
    } else {
        other = org_item_parent(lines, i);
        if (other < 0) {
            set_msg(msg, "Cannot outdent beyond margin");
            return FALSE;
        }
        target = indent_of(org_line(lines, other));
    }
    gint delta = target - ind;
    shift_lines(lines, i, e, delta);
    if (pos->line >= i && pos->line < e) {
        pos->col += delta;
        clamp_pos(lines, pos);
    }
    org_renumber_list(lines, i);
    org_renumber_list(lines, other);
    return TRUE;
}

gboolean org_move_item(GPtrArray* lines, OrgPos* pos, gint dir, OrgSwap* swap, gchar** msg) {
    gint i = org_item_at(lines, pos->line);
    if (i < 0) return FALSE;
    OrgSwap sw;
    if (dir < 0) {
        gint p = item_prev_sibling(lines, i);
        if (p < 0) {
            set_msg(msg, "Cannot move this item further up");
            return FALSE;
        }
        gint e = org_item_end(lines, i);
        sw.start = p;
        sw.mid = i;
        sw.end = e;
        pos->line -= i - p;
    } else {
        gint n = item_next_sibling(lines, i);
        if (n < 0) {
            set_msg(msg, "Cannot move this item further down");
            return FALSE;
        }
        gint e2 = org_item_end(lines, n);
        sw.start = i;
        sw.mid = n;
        sw.end = e2;
        pos->line += e2 - n;
    }
    swap_blocks(lines, sw.start, sw.mid, sw.end);
    if (swap != NULL) *swap = sw;
    org_renumber_list(lines, sw.start);
    return TRUE;
}

gboolean org_cycle_bullet(GPtrArray* lines, OrgPos* pos, gint dir) {
    static const gchar* BULLETS[] = {"-", "+", "*", "1.", "1)"};
    gint n = (gint)G_N_ELEMENTS(BULLETS);
    gint i = org_item_at(lines, pos->line);
    if (i < 0) return FALSE;
    OrgItem it;
    org_item_parse(org_line(lines, i), &it);
    gint cur = 0;
    if (it.ordered) cur = it.bullet[strlen(it.bullet) - 1] == '.' ? 3 : 4;
    else cur = it.bullet[0] == '-' ? 0 : (it.bullet[0] == '+' ? 1 : 2);
    gint next = cur;
    do {
        next = (next + (dir >= 0 ? 1 : -1) + n) % n;
    } while (next == 2 && it.indent == 0); /* "*" at column 0 would be a heading */
    for (gint j = item_first_sibling(lines, i); j >= 0; j = item_next_sibling(lines, j)) {
        set_bullet(lines, pos, j, BULLETS[next]);
    }
    org_renumber_list(lines, i);
    return TRUE;
}

gboolean org_ctrl_c_minus(GPtrArray* lines, OrgPos* pos) {
    const gchar* l = org_line(lines, pos->line);
    if (org_item_at(lines, pos->line) == pos->line) return org_cycle_bullet(lines, pos, 1);
    if (is_blank(l) || org_heading_level(l) > 0) return FALSE;
    gint b = indent_bytes(l);
    set_line(lines, pos->line, g_strdup_printf("%.*s- %s", b, l, l + b));
    if (pos->col >= b) pos->col += 2;
    return TRUE;
}

/* Direct sub-items of item i (their indent is the first sub-item's) */
static void item_children_stats(GPtrArray* lines, gint i, gint* done, gint* total,
                                gint* partial) {
    gint e = org_item_end(lines, i);
    gint child_ind = -1;
    for (gint j = i + 1; j < e; j++) {
        OrgItem c;
        if (!org_item_parse(org_line(lines, j), &c)) continue;
        if (child_ind < 0) child_ind = c.indent;
        if (c.indent != child_ind || c.checkbox == 0) continue;
        (*total)++;
        if (c.checkbox == 'X') (*done)++;
        if (c.checkbox == '-') (*partial)++;
    }
}

gboolean org_toggle_checkbox(GPtrArray* lines, OrgPos* pos, gchar** msg) {
    gint i = org_item_at(lines, pos->line);
    OrgItem it;
    if (i < 0 || !org_item_parse(org_line(lines, i), &it) || it.checkbox == 0) {
        set_msg(msg, "No checkbox here");
        return FALSE;
    }
    gchar c = it.checkbox == 'X' ? ' ' : 'X';
    set_checkbox(lines, i, c);
    gint e = org_item_end(lines, i);
    for (gint j = i + 1; j < e; j++) set_checkbox(lines, j, c);
    /* Parents follow their children: all [X], none [ ], else [-] */
    for (gint p = org_item_parent(lines, i); p >= 0; p = org_item_parent(lines, p)) {
        OrgItem pi;
        org_item_parse(org_line(lines, p), &pi);
        if (pi.checkbox == 0) break;
        gint done = 0, total = 0, partial = 0;
        item_children_stats(lines, p, &done, &total, &partial);
        if (total == 0) break;
        set_checkbox(lines, p, done == total ? 'X' : ((done == 0 && partial == 0) ? ' ' : '-'));
    }
    return TRUE;
}

/* Find the next statistics cookie in s from byte from: [n/m] or [p%] */
static gboolean find_cookie(const gchar* s, gint from, gint* start, gint* end, gboolean* percent) {
    for (const gchar* p = strchr(s + from, '['); p != NULL; p = strchr(p + 1, '[')) {
        const gchar* q = p + 1;
        while (g_ascii_isdigit(*q)) q++;
        if (*q == '%' && q[1] == ']') {
            *start = (gint)(p - s);
            *end = (gint)(q - s) + 2;
            *percent = TRUE;
            return TRUE;
        }
        if (*q == '/') {
            q++;
            while (g_ascii_isdigit(*q)) q++;
            if (*q == ']') {
                *start = (gint)(p - s);
                *end = (gint)(q - s) + 1;
                *percent = FALSE;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static void heading_stats(GPtrArray* lines, gint h, const OrgTodoConfig* cfg, gint* done, gint* total) {
    gint ee = org_entry_end(lines, h);
    gint min_ind = G_MAXINT;
    for (gint j = h + 1; j < ee; j++) {
        OrgItem it;
        if (org_item_parse(org_line(lines, j), &it) && it.checkbox != 0) min_ind = MIN(min_ind, it.indent);
    }
    if (min_ind != G_MAXINT) {
        for (gint j = h + 1; j < ee; j++) {
            OrgItem it;
            if (!org_item_parse(org_line(lines, j), &it) || it.checkbox == 0 || it.indent != min_ind) continue;
            (*total)++;
            if (it.checkbox == 'X') (*done)++;
        }
        return;
    }
    gint se = org_subtree_end(lines, h);
    for (gint j = h + 1; j < se; j++) {
        if (org_heading_level(org_line(lines, j)) == 0 || org_heading_parent(lines, j) != h) continue;
        OrgHeading c;
        org_heading_parse(org_line(lines, j), cfg, &c);
        if (c.keyword != NULL) {
            (*total)++;
            if (org_todo_is_done(cfg, c.keyword)) (*done)++;
        }
        org_heading_clear(&c);
    }
}

gboolean org_update_statistics(GPtrArray* lines, const OrgTodoConfig* cfg) {
    gboolean changed = FALSE;
    for (gint i = 0; i < n_lines(lines); i++) {
        const gchar* l = org_line(lines, i);
        gint s, e;
        gboolean pct;
        if (strchr(l, '[') == NULL || !find_cookie(l, 0, &s, &e, &pct)) continue;
        gint done = 0, total = 0, partial = 0;
        gboolean heading = org_heading_level(l) > 0;
        OrgItem it;
        if (heading) heading_stats(lines, i, cfg, &done, &total);
        else if (org_item_parse(l, &it)) item_children_stats(lines, i, &done, &total, &partial);
        else continue;
        GString* out = g_string_new(NULL);
        gint from = 0;
        while (find_cookie(l, from, &s, &e, &pct)) {
            g_string_append_len(out, l + from, s - from);
            if (pct) g_string_append_printf(out, "[%d%%]", total > 0 ? done * 100 / total : 0);
            else g_string_append_printf(out, "[%d/%d]", done, total);
            from = e;
        }
        g_string_append(out, l + from);
        if (strcmp(out->str, l) != 0) {
            changed = TRUE;
            if (heading) {
                OrgHeading hd;
                org_heading_parse(out->str, cfg, &hd);
                gboolean tags = hd.tags != NULL;
                if (tags) {
                    set_line(lines, i, org_heading_build(&hd));
                    g_string_free(out, TRUE);
                    out = NULL;
                }
                org_heading_clear(&hd);
            }
            if (out != NULL) set_line(lines, i, g_string_free(out, FALSE));
        } else {
            g_string_free(out, TRUE);
        }
    }
    return changed;
}

/* ------------------------------------------------------------------------- */
/* Links                                                                     */
/* ------------------------------------------------------------------------- */

gchar* org_link_at(const gchar* line, gint col) {
    if (line == NULL) return NULL;
    for (const gchar* p = strstr(line, "[["); p != NULL; p = strstr(p + 2, "[[")) {
        const gchar* e = strstr(p + 2, "]]");
        if (e == NULL) break;
        gint s = (gint)(p - line);
        gint end = (gint)(e - line) + 2;
        if (col >= s && col <= end) {
            const gchar* mid = g_strstr_len(p + 2, e - p - 2, "][");
            const gchar* te = mid != NULL ? mid : e;
            return g_strndup(p + 2, te - p - 2);
        }
    }
    static const gchar* SCHEMES[] = {"https://", "http://", "file:", "mailto:"};
    for (guint k = 0; k < G_N_ELEMENTS(SCHEMES); k++) {
        for (const gchar* p = strstr(line, SCHEMES[k]); p != NULL; p = strstr(p + 1, SCHEMES[k])) {
            const gchar* e = p;
            while (*e != '\0' && *e != ' ' && *e != '\t' && *e != '>' && *e != ']' && *e != ')' &&
                   *e != '"') {
                e++;
            }
            gint s = (gint)(p - line);
            if (col >= s && col <= (gint)(e - line)) return g_strndup(p, e - p);
        }
    }
    return NULL;
}

gchar* org_link_format(const gchar* target, const gchar* desc) {
    if (desc == NULL || desc[0] == '\0' || g_strcmp0(desc, target) == 0) {
        return g_strdup_printf("[[%s]]", target);
    }
    return g_strdup_printf("[[%s][%s]]", target, desc);
}

gint org_link_find_internal(GPtrArray* lines, const OrgTodoConfig* cfg, const gchar* target) {
    if (target == NULL || target[0] == '\0') return -1;
    if (target[0] == '*') return org_heading_find_title(lines, cfg, target + 1);
    if (target[0] == '#') {
        for (gint i = 0; i < n_lines(lines); i++) {
            const gchar* l = org_line(lines, i);
            const gchar* p = l + indent_bytes(l);
            if (has_prefix_ci(p, ":CUSTOM_ID:")) {
                gchar* v = g_strstrip(g_strdup(p + 11));
                gboolean eq = strcmp(v, target + 1) == 0;
                g_free(v);
                if (eq) {
                    gint h = org_heading_at(lines, i);
                    return h >= 0 ? h : i;
                }
            }
        }
        return -1;
    }
    gchar* dedicated = g_strdup_printf("<<%s>>", target);
    gint found = -1;
    for (gint i = 0; i < n_lines(lines) && found < 0; i++) {
        const gchar* l = org_line(lines, i);
        const gchar* p = l + indent_bytes(l);
        if (strstr(l, dedicated) != NULL) {
            found = i;
        } else if (has_prefix_ci(p, "#+NAME:")) {
            gchar* v = g_strstrip(g_strdup(p + 7));
            if (strcmp(v, target) == 0) found = i;
            g_free(v);
        }
    }
    g_free(dedicated);
    if (found < 0) found = org_heading_find_title(lines, cfg, target);
    return found;
}

/* ------------------------------------------------------------------------- */
/* Templates                                                                 */
/* ------------------------------------------------------------------------- */

gboolean org_expand_template(GPtrArray* lines, OrgPos* pos) {
    static const struct {
        gchar key;
        const gchar* block;
    } TEMPLATES[] = {
        {'s', "src"}, {'e', "example"}, {'q', "quote"}, {'c', "center"},
        {'v', "verse"}, {'x', "export"}, {'C', "comment"}, {'l', "export latex"},
        {'h', "export html"}, {'a', "export ascii"},
    };
    const gchar* l = org_line(lines, pos->line);
    if (pos->col != org_utf8_width(l)) return FALSE;
    gint b = indent_bytes(l);
    const gchar* p = l + b;
    if (p[0] != '<' || !g_ascii_isalpha(p[1]) || (p[2] != '\0' && p[2] != ' ')) return FALSE;
    const gchar* block = NULL;
    for (guint k = 0; k < G_N_ELEMENTS(TEMPLATES); k++) {
        if (TEMPLATES[k].key == p[1]) block = TEMPLATES[k].block;
    }
    if (block == NULL) return FALSE;
    gchar* args = g_strstrip(g_strdup(p[2] != '\0' ? p + 3 : ""));
    gchar* indent = g_strndup(l, b);
    gchar* name = g_strdup(block);
    gchar* sp = strchr(name, ' ');
    if (sp != NULL) *sp = '\0';
    gboolean want_lang = strcmp(block, "src") == 0 && args[0] == '\0';
    gchar* begin = args[0] != '\0' ? g_strdup_printf("%s#+begin_%s %s", indent, block, args)
                                   : g_strdup_printf("%s#+begin_%s%s", indent, block, want_lang ? " " : "");
    set_line(lines, pos->line, begin);
    insert_line(lines, pos->line + 1, g_strdup_printf("%s#+end_%s", indent, name));
    insert_line(lines, pos->line + 1, g_strdup(indent));
    if (want_lang) {
        pos->col = org_utf8_width(begin);
    } else {
        pos->line++;
        pos->col = org_utf8_width(indent);
    }
    g_free(args);
    g_free(indent);
    g_free(name);
    return TRUE;
}
