/* Search the user guide (see user_guide_search.h) */
#include "chrome/user_guide_search.h"
#include "chrome/user_guide.h"
#include "core/i18n.h"

#include <string.h>

/* Characters of an excerpt longer than this are cut around the first match */
#define EXCERPT_CHARS 200
#define EXCERPT_BEFORE 60

/* A word of the query, folded */
typedef struct {
    gunichar* c;
    glong n;
} Word;

/* c compared without case or accents; typographic quotes and spaces as the
 * keyboard types them. One character for one, so offsets stay those of the
 * text. */
static gunichar fold_char(gunichar c) {
    switch (c) {
    case 0x2018: case 0x2019: case 0x02BC: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x00A0: case 0x2009: case 0x202F: return ' ';
    default: break;
    }
    gunichar d[G_UNICHAR_MAX_DECOMPOSITION_LENGTH];
    gsize n = g_unichar_fully_decompose(c, FALSE, d, G_N_ELEMENTS(d));
    return g_unichar_tolower(n > 0 ? d[0] : c);
}

/* text folded, one gunichar per character; *len its length */
static gunichar* fold_text(const gchar* text, glong* len) {
    glong n = g_utf8_strlen(text, -1);
    gunichar* out = g_new(gunichar, n + 1);
    glong i = 0;
    for (const gchar* p = text; *p != '\0' && i < n; p = g_utf8_next_char(p)) {
        out[i++] = fold_char(g_utf8_get_char(p));
    }
    out[i] = 0;
    *len = i;
    return out;
}

static void word_clear(gpointer p) {
    g_free(((Word*)p)->c);
}

/* The words of query (Word), empty when it has none */
static GArray* query_words(const gchar* query) {
    GArray* words = g_array_new(FALSE, FALSE, sizeof(Word));
    g_array_set_clear_func(words, word_clear);
    if (query == NULL) return words;
    glong len = 0;
    gunichar* folded = fold_text(query, &len);
    glong i = 0;
    while (i < len) {
        while (i < len && g_unichar_isspace(folded[i])) i++;
        glong start = i;
        while (i < len && !g_unichar_isspace(folded[i])) i++;
        if (i > start) {
            Word w = {g_new(gunichar, i - start), i - start};
            memcpy(w.c, folded + start, sizeof(gunichar) * (gsize)(i - start));
            g_array_append_val(words, w);
        }
    }
    g_free(folded);
    return words;
}

/* First occurrence of w in hay at or after from, or -1 */
static glong find_word(const gunichar* hay, glong len, const Word* w, glong from) {
    for (glong i = from; i + w->n <= len; i++) {
        if (hay[i] == w->c[0] && memcmp(hay + i, w->c, sizeof(gunichar) * (gsize)w->n) == 0) return i;
    }
    return -1;
}

static glong count_word(const gunichar* hay, glong len, const Word* w) {
    glong n = 0;
    for (glong i = find_word(hay, len, w, 0); i >= 0; i = find_word(hay, len, w, i + w->n)) n++;
    return n;
}

static gint compare_ranges(gconstpointer a, gconstpointer b) {
    const gint* x = a;
    const gint* y = b;
    return x[0] != y[0] ? (x[0] < y[0] ? -1 : 1) : (x[1] < y[1] ? -1 : (x[1] > y[1]));
}

GArray* user_guide_match_ranges(const gchar* text, const gchar* query) {
    GArray* ranges = g_array_new(FALSE, FALSE, sizeof(gint) * 2);
    if (text == NULL) return ranges;
    GArray* words = query_words(query);
    glong len = 0;
    gunichar* hay = fold_text(text, &len);
    for (guint k = 0; k < words->len; k++) {
        const Word* w = &g_array_index(words, Word, k);
        for (glong i = find_word(hay, len, w, 0); i >= 0; i = find_word(hay, len, w, i + w->n)) {
            gint r[2] = {(gint)i, (gint)(i + w->n)};
            g_array_append_vals(ranges, r, 1);
        }
    }
    g_free(hay);
    g_array_unref(words);
    g_array_sort(ranges, compare_ranges);
    /* Merge overlaps ("set" in "settings" with "settings") */
    guint out = 0;
    gint* r = (gint*)(void*)ranges->data;
    for (guint i = 0; i < ranges->len; i++) {
        if (out > 0 && r[2 * i] <= r[2 * (out - 1) + 1]) {
            r[2 * (out - 1) + 1] = MAX(r[2 * (out - 1) + 1], r[2 * i + 1]);
        } else {
            r[2 * out] = r[2 * i];
            r[2 * out + 1] = r[2 * i + 1];
            out++;
        }
    }
    g_array_set_size(ranges, out);
    return ranges;
}

/* ---- Markdown to plain text ---- */

static gboolean is_word_char(const gchar* p) {
    return p != NULL && *p != '\0' && g_unichar_isalnum(g_utf8_get_char(p));
}

/* The ']' closing a label starting at p (after its '['), or NULL */
static const gchar* label_end(const gchar* p) {
    gint depth = 0;
    for (; *p != '\0'; p++) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        } else if (*p == '[') {
            depth++;
        } else if (*p == ']') {
            if (depth == 0) return p;
            depth--;
        }
    }
    return NULL;
}

/* The ')' closing a link destination starting at p (after its '('), or NULL */
static const gchar* dest_end(const gchar* p) {
    gint depth = 0;
    for (; *p != '\0'; p++) {
        if (*p == '(') {
            depth++;
        } else if (*p == ')') {
            if (depth == 0) return p;
            depth--;
        }
    }
    return NULL;
}

/* Inline Markdown of [s, end) as text, appended to out */
static void append_plain_inline(GString* out, const gchar* s, const gchar* end) {
    const gchar* p = s;
    while (p < end && *p != '\0') {
        if (*p == '\\' && p + 1 < end && g_ascii_ispunct(p[1])) {
            g_string_append_c(out, p[1]);
            p += 2;
            continue;
        }
        if (*p == '`') {
            const gchar* q = p;
            while (q < end && *q == '`') q++;
            gsize ticks = (gsize)(q - p);
            const gchar* close = q;
            while (close < end) {
                close = memchr(close, '`', (gsize)(end - close));
                if (close == NULL) break;
                const gchar* c = close;
                while (c < end && *c == '`') c++;
                if ((gsize)(c - close) == ticks) break;
                close = c;
            }
            if (close == NULL || close >= end) {
                g_string_append_len(out, p, (gssize)ticks);
                p = q;
                continue;
            }
            const gchar* a = q;
            const gchar* b = close;
            if (b - a >= 2 && *a == ' ' && b[-1] == ' ') {
                a++;
                b--;
            }
            g_string_append_len(out, a, b - a);
            p = close + ticks;
            continue;
        }
        if (*p == '!' && p + 1 < end && p[1] == '[') {
            p++;
            continue;
        }
        if (*p == '[') {
            const gchar* close = label_end(p + 1);
            if (close != NULL && close < end) {
                append_plain_inline(out, p + 1, close);
                p = close + 1;
                if (p < end && *p == '(') {
                    const gchar* d = dest_end(p + 1);
                    if (d != NULL && d < end) p = d + 1;
                }
                continue;
            }
        }
        if (*p == '*') {
            p++;
            continue;
        }
        if (*p == '~' && p + 1 < end && p[1] == '~') {
            p += 2;
            continue;
        }
        if (*p == '_') {
            /* Emphasis, not a name such as PREVIEW_BYTES */
            gboolean before = p > s && g_unichar_isalnum(g_utf8_get_char(g_utf8_prev_char(p)));
            gboolean after = p + 1 < end && is_word_char(p + 1);
            if (!before || !after) {
                p++;
                continue;
            }
        }
        if (*p == '<' && p + 1 < end && (g_ascii_isalpha(p[1]) || p[1] == '/')) {
            const gchar* close = memchr(p, '>', (gsize)(end - p));
            if (close != NULL) {
                if (g_str_has_prefix(p + 1, "http") || g_str_has_prefix(p + 1, "mailto:")) {
                    g_string_append_len(out, p + 1, close - p - 1); /* autolink */
                }
                p = close + 1;
                continue;
            }
        }
        const gchar* next = g_utf8_next_char(p);
        g_string_append_len(out, p, next - p);
        p = next;
    }
}

/* A table separator row ("|---|:--:|") */
static gboolean is_table_rule(const gchar* p) {
    gboolean dash = FALSE;
    for (; *p != '\0'; p++) {
        if (*p == '-') {
            dash = TRUE;
        } else if (*p != '|' && *p != ':' && *p != ' ' && *p != '\t') {
            return FALSE;
        }
    }
    return dash;
}

gchar* user_guide_plain(const gchar* line) {
    GString* out = g_string_new(NULL);
    const gchar* p = line != NULL ? line : "";
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#') {
        while (*p == '#') p++;
        while (*p == ' ') p++;
    }
    while (*p == '>') {
        p++;
        while (*p == ' ') p++;
    }
    if ((*p == '-' || *p == '*' || *p == '+') && p[1] == ' ') {
        p += 2;
    } else if (g_ascii_isdigit(*p)) {
        const gchar* q = p;
        while (g_ascii_isdigit(*q)) q++;
        if ((*q == '.' || *q == ')') && q[1] == ' ') p = q + 2;
    }
    if (*p == '|') {
        if (!is_table_rule(p)) {
            /* Cells between unescaped '|' */
            const gchar* cell = p + 1;
            for (const gchar* q = cell;; q++) {
                if (*q == '\\' && q[1] != '\0') {
                    q++;
                    continue;
                }
                if (*q != '|' && *q != '\0') continue;
                GString* text = g_string_new(NULL);
                append_plain_inline(text, cell, q);
                g_strstrip(text->str);
                text->len = strlen(text->str);
                if (text->len > 0) {
                    if (out->len > 0) g_string_append(out, " · ");
                    g_string_append_len(out, text->str, (gssize)text->len);
                }
                g_string_free(text, TRUE);
                if (*q == '\0') break;
                cell = q + 1;
            }
        }
    } else {
        append_plain_inline(out, p, p + strlen(p));
    }
    gchar* text = g_string_free(out, FALSE);
    gsize n = strlen(text);
    while (n > 0 && text[n - 1] == '\\') text[--n] = '\0'; /* hard line break */
    return g_strstrip(text);
}

gchar* user_guide_md_escape(const gchar* text) {
    GString* out = g_string_new(NULL);
    /* Not ']' and ')': "\[...\]" and "\(...\)" would be math */
    for (const gchar* p = text != NULL ? text : ""; *p != '\0'; p++) {
        if (strchr("\\`*_[<>|~$&!#", *p) != NULL) g_string_append_c(out, '\\');
        g_string_append_c(out, *p);
    }
    return g_string_free(out, FALSE);
}

/* ---- Sections ---- */

/* Heading level of line (1 to 6), 0 when it is not one */
static gint heading_level(const gchar* line, gsize len) {
    gsize n = 0;
    while (n < len && line[n] == '#') n++;
    return n >= 1 && n <= 6 && n < len && line[n] == ' ' ? (gint)n : 0;
}

/* line starts a block of its own: a list item, a table row or a quote */
static gboolean starts_block(const gchar* line) {
    const gchar* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if ((*p == '-' || *p == '*' || *p == '+') && p[1] == ' ') return TRUE;
    if (*p == '|' || *p == '>') return TRUE;
    const gchar* q = p;
    while (g_ascii_isdigit(*q)) q++;
    return q > p && (*q == '.' || *q == ')') && q[1] == ' ';
}

/* line is text a paragraph may hold: not blank, not a table row */
static gboolean starts_text(const gchar* line) {
    const gchar* p = line;
    while (*p == ' ' || *p == '\t') p++;
    return *p != '\0' && *p != '|';
}

typedef struct {
    gchar* heading; /* plain text, NULL: the page's top */
    GPtrArray* lines; /* gchar*: the lines under it */
} Section;

static void section_free(gpointer p) {
    Section* s = p;
    g_free(s->heading);
    g_ptr_array_unref(s->lines);
    g_free(s);
}

/* Sections of markdown; *title its first "# " heading (plain), or NULL */
static GPtrArray* split_sections(const gchar* markdown, gchar** title) {
    GPtrArray* sections = g_ptr_array_new_with_free_func(section_free);
    Section* cur = g_new0(Section, 1);
    cur->lines = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(sections, cur);
    *title = NULL;
    gboolean in_code = FALSE;
    gboolean joinable = FALSE; /* the last line takes the next one */
    gchar** lines = g_strsplit(markdown, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar* line = lines[i];
        gsize len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
        if (g_str_has_prefix(line, "```")) {
            in_code = !in_code;
            joinable = FALSE;
            continue;
        }
        gint level = in_code ? 0 : heading_level(line, len);
        if (level == 1 && *title == NULL) {
            *title = user_guide_plain(line);
            joinable = FALSE;
            continue;
        }
        if (level > 0) {
            joinable = FALSE;
            cur = g_new0(Section, 1);
            cur->heading = user_guide_plain(line);
            cur->lines = g_ptr_array_new_with_free_func(g_free);
            g_ptr_array_add(sections, cur);
            continue;
        }
        /* A paragraph or a list item is one line: its lines joined */
        gboolean text = !in_code && len > 0 && starts_text(line);
        if (text && joinable && cur->lines->len > 0 && !starts_block(line)) {
            gchar** last = (gchar**)&g_ptr_array_index(cur->lines, cur->lines->len - 1);
            gchar* joined = g_strconcat(*last, " ", g_strchug(line), NULL);
            g_free(*last);
            *last = joined;
        } else {
            g_ptr_array_add(cur->lines, g_strdup(line));
        }
        joinable = text;
    }
    g_strfreev(lines);
    return sections;
}

/* The Markdown excerpt of plain: cut around its first match, matches in bold */
static gchar* make_excerpt(const gchar* plain, const gchar* query) {
    glong len = g_utf8_strlen(plain, -1);
    GArray* ranges = user_guide_match_ranges(plain, query);
    glong from = 0;
    glong to = len;
    if (len > EXCERPT_CHARS) {
        glong first = ranges->len > 0 ? g_array_index(ranges, gint, 0) : 0;
        from = MAX(0, first - EXCERPT_BEFORE);
        to = MIN(len, from + EXCERPT_CHARS);
        from = MAX(0, to - EXCERPT_CHARS);
    }
    GString* out = g_string_new(from > 0 ? "…" : "");
    glong at = from;
    for (guint i = 0; i <= ranges->len && at < to; i++) {
        glong s = i < ranges->len ? ((gint*)(void*)ranges->data)[2 * i] : to;
        glong e = i < ranges->len ? ((gint*)(void*)ranges->data)[2 * i + 1] : to;
        s = CLAMP(s, at, to);
        e = CLAMP(e, s, to);
        const gchar* ps = g_utf8_offset_to_pointer(plain, at);
        const gchar* pm = g_utf8_offset_to_pointer(plain, s);
        const gchar* pe = g_utf8_offset_to_pointer(plain, e);
        gchar* before = g_strndup(ps, (gsize)(pm - ps));
        gchar* esc = user_guide_md_escape(before);
        g_string_append(out, esc);
        g_free(esc);
        g_free(before);
        if (e > s) {
            gchar* match = g_strndup(pm, (gsize)(pe - pm));
            esc = user_guide_md_escape(match);
            g_string_append_printf(out, "**%s**", esc);
            g_free(esc);
            g_free(match);
        }
        at = e;
    }
    if (to < len) g_string_append(out, "…");
    g_array_unref(ranges);
    return g_string_free(out, FALSE);
}

/* Number of words found in text */
static guint words_in(const gchar* text, GArray* words) {
    glong len = 0;
    gunichar* hay = fold_text(text, &len);
    guint n = 0;
    for (guint k = 0; k < words->len; k++) {
        if (find_word(hay, len, &g_array_index(words, Word, k), 0) >= 0) n++;
    }
    g_free(hay);
    return n;
}

void user_guide_hit_free(gpointer hit) {
    UserGuideHit* h = hit;
    if (h == NULL) return;
    g_free(h->page);
    g_free(h->anchor);
    g_free(h->page_title);
    g_free(h->heading);
    g_free(h->excerpt);
    g_free(h);
}

/* Section s, as a hit for words, or NULL when a word is missing */
static UserGuideHit* search_section(const gchar* page, const gchar* title, const Section* s,
                                    GArray* words, const gchar* query) {
    glong hlen = 0, tlen = 0;
    gunichar* head = fold_text(s->heading != NULL ? s->heading : "", &hlen);
    gunichar* tit = fold_text(s->heading == NULL && title != NULL ? title : "", &tlen);
    GPtrArray* plains = g_ptr_array_new_with_free_func(g_free);
    GPtrArray* folded = g_ptr_array_new_with_free_func(g_free);
    GArray* lens = g_array_new(FALSE, FALSE, sizeof(glong));
    for (guint i = 0; i < s->lines->len; i++) {
        gchar* plain = user_guide_plain(g_ptr_array_index(s->lines, i));
        glong n = 0;
        g_ptr_array_add(plains, plain);
        g_ptr_array_add(folded, fold_text(plain, &n));
        g_array_append_val(lens, n);
    }
    gint score = 0;
    gboolean all = TRUE;
    for (guint k = 0; k < words->len && all; k++) {
        const Word* w = &g_array_index(words, Word, k);
        gboolean in_head = find_word(head, hlen, w, 0) >= 0;
        gboolean in_title = find_word(tit, tlen, w, 0) >= 0;
        glong count = 0;
        for (guint i = 0; i < folded->len; i++) {
            count += count_word(g_ptr_array_index(folded, i), g_array_index(lens, glong, i), w);
        }
        all = in_head || in_title || count > 0;
        score += (in_title ? 15 : 0) + (in_head ? 10 : 0) + (gint)MIN(count, 5);
    }
    UserGuideHit* hit = NULL;
    if (all) {
        hit = g_new0(UserGuideHit, 1);
        hit->page = g_strdup(page);
        hit->page_title = g_strdup(title != NULL ? title : page);
        hit->heading = g_strdup(s->heading);
        hit->anchor = s->heading != NULL ? user_guide_anchor(s->heading) : NULL;
        hit->score = score;
        /* The line with the most words (the first of those) */
        guint best_n = 0;
        const gchar* best = NULL;
        for (guint i = 0; i < plains->len; i++) {
            guint n = words_in(g_ptr_array_index(plains, i), words);
            if (n > best_n) {
                best_n = n;
                best = g_ptr_array_index(plains, i);
            }
        }
        if (best != NULL) hit->excerpt = make_excerpt(best, query);
    }
    g_array_unref(lens);
    g_ptr_array_unref(folded);
    g_ptr_array_unref(plains);
    g_free(tit);
    g_free(head);
    return hit;
}

void user_guide_search_text(const gchar* page, const gchar* markdown, const gchar* query, GPtrArray* hits) {
    if (page == NULL || markdown == NULL || hits == NULL) return;
    GArray* words = query_words(query);
    if (words->len > 0) {
        gchar* title = NULL;
        GPtrArray* sections = split_sections(markdown, &title);
        for (guint i = 0; i < sections->len; i++) {
            UserGuideHit* hit = search_section(page, title, g_ptr_array_index(sections, i), words, query);
            if (hit != NULL) g_ptr_array_add(hits, hit);
        }
        g_ptr_array_unref(sections);
        g_free(title);
    }
    g_array_unref(words);
}

/* Best score first; the guide's own pages before the reference pages, then
 * the order of the guide */
typedef struct {
    UserGuideHit* hit;
    guint order;
} Ranked;

static gint compare_ranked(gconstpointer a, gconstpointer b) {
    const Ranked* x = a;
    const Ranked* y = b;
    if (x->hit->score != y->hit->score) return x->hit->score > y->hit->score ? -1 : 1;
    return x->order < y->order ? -1 : (x->order > y->order);
}

GPtrArray* user_guide_search(const gchar* query) {
    GPtrArray* found = g_ptr_array_new();
    gchar** pages = user_guide_pages();
    /* The guide's pages, its contents first, then the reference pages */
    for (gint pass = 0; pass < 3; pass++) {
        for (gint i = 0; pages[i] != NULL; i++) {
            gboolean home = g_strcmp0(pages[i], USER_GUIDE_HOME) == 0;
            gboolean guide = g_str_has_prefix(pages[i], "user-guide/");
            if ((pass == 0) != home || (pass == 1) != (guide && !home)) continue;
            gchar* text = user_guide_read_localized(pages[i]);
            user_guide_search_text(pages[i], text, query, found);
            g_free(text);
        }
    }
    g_strfreev(pages);
    GArray* ranked = g_array_sized_new(FALSE, FALSE, sizeof(Ranked), found->len);
    for (guint i = 0; i < found->len; i++) {
        Ranked r = {g_ptr_array_index(found, i), i};
        g_array_append_val(ranked, r);
    }
    g_array_sort(ranked, compare_ranked);
    GPtrArray* hits = g_ptr_array_new_with_free_func(user_guide_hit_free);
    for (guint i = 0; i < ranked->len; i++) {
        UserGuideHit* hit = g_array_index(ranked, Ranked, i).hit;
        if (hits->len < USER_GUIDE_SEARCH_MAX_HITS) {
            g_ptr_array_add(hits, hit);
        } else {
            user_guide_hit_free(hit);
        }
    }
    g_array_unref(ranked);
    g_ptr_array_unref(found);
    return hits;
}

gchar* user_guide_search_page(const gchar* query) {
    GString* out = g_string_new(NULL);
    gchar* q = g_strstrip(g_strdup(query != NULL ? query : ""));
    gchar* esc = user_guide_md_escape(q);
    g_string_append(out, "# ");
    /* TRANSLATORS: title of the Help tab's search results; %s is what was searched */
    g_string_append_printf(out, _("Search: %s"), esc);
    g_string_append(out, "\n\n");
    g_free(esc);
    GPtrArray* hits = user_guide_search(q);
    if (q[0] == '\0') {
        g_string_append(out, _("Type words to search for in the search field above the page."));
        g_string_append(out, "\n");
    } else if (hits->len == 0) {
        g_string_append(out, _("No section of the guide holds every word searched."));
        g_string_append(out, "\n");
    } else {
        if (hits->len >= USER_GUIDE_SEARCH_MAX_HITS) {
            g_string_append_printf(out, _("The first %u sections that hold every word searched, best first:"),
                                   hits->len);
        } else {
            g_string_append_printf(out, ngettext("%u section holds every word searched:",
                                                 "%u sections hold every word searched, best first:", hits->len),
                                   hits->len);
        }
        g_string_append(out, "\n\n");
        for (guint i = 0; i < hits->len; i++) {
            UserGuideHit* hit = g_ptr_array_index(hits, i);
            gchar* label = hit->heading != NULL ? g_strdup_printf("%s › %s", hit->page_title, hit->heading)
                                                : g_strdup(hit->page_title);
            gchar* label_esc = user_guide_md_escape(label);
            g_string_append_printf(out, "- [%s](%s%s%s)", label_esc, hit->page, hit->anchor != NULL ? "#" : "",
                                   hit->anchor != NULL ? hit->anchor : "");
            if (hit->excerpt != NULL) g_string_append_printf(out, ": %s", hit->excerpt);
            g_string_append_c(out, '\n');
            g_free(label_esc);
            g_free(label);
        }
    }
    g_ptr_array_unref(hits);
    g_free(q);
    return g_string_free(out, FALSE);
}
