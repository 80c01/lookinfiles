/* PowerPoint 97-2003 presentations to HTML (see ppt_reader.h) */
#include "ppt_reader.h"
#include <gio/gio.h>
#include <string.h>

#define PPT_MAX_STREAM (64 * 1024 * 1024)
#define MAX_DEPTH 16

#define RT_DOCUMENT 0x03E8
#define RT_SLIDE 0x03EE
#define RT_NOTES 0x03F0
#define RT_SLIDE_PERSIST 0x03F3
#define RT_MAIN_MASTER 0x03F8
#define RT_SLIDE_LIST_WITH_TEXT 0x0FF0
#define RT_TEXT_HEADER 0x0F9F
#define RT_TEXT_CHARS 0x0FA0
#define RT_TEXT_BYTES 0x0FA8
#define RT_DOCUMENT_ENCRYPTION 0x2F14

/* Text types of TextHeaderAtom */
#define TT_TITLE 0
#define TT_BODY 1
#define TT_NOTES 2
#define TT_OTHER 4
#define TT_CENTER_BODY 5
#define TT_CENTER_TITLE 6
#define TT_HALF_BODY 7
#define TT_QUARTER_BODY 8

static guint16 u16(const guint8* p) {
    return (guint16)(p[0] | (p[1] << 8));
}

static guint32 u32(const guint8* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static const gunichar CP1252[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

typedef struct {
    gint type;     /* text type */
    gchar* text;   /* UTF-8, paragraphs separated by '\n' */
} Text;

typedef struct {
    GArray* texts; /* Text */
} Slide;

static void text_clear(gpointer p) {
    g_free(((Text*)p)->text);
}

static Slide* slide_new(void) {
    Slide* s = g_new0(Slide, 1);
    s->texts = g_array_new(FALSE, FALSE, sizeof(Text));
    g_array_set_clear_func(s->texts, text_clear);
    return s;
}

static void slide_free(gpointer p) {
    Slide* s = p;
    g_array_free(s->texts, TRUE);
    g_free(s);
}

/* Text of a TextCharsAtom / TextBytesAtom: CR ends a paragraph, VT is a line break */
static gchar* atom_text(guint16 type, const guint8* d, guint32 len) {
    GString* out = g_string_new(NULL);
    guint32 n = type == RT_TEXT_CHARS ? len / 2 : len;
    for (guint32 i = 0; i < n; i++) {
        gunichar c = type == RT_TEXT_CHARS ? u16(d + 2 * i) : d[i];
        if (type == RT_TEXT_BYTES && c >= 0x80 && c < 0xA0) c = CP1252[c - 0x80];
        if (c == 0x0D) c = '\n';
        else if (c == 0x0B) c = ' ';
        else if ((c < 0x20 && c != '\t') || (c >= 0xD800 && c < 0xE000)) continue;
        g_string_append_unichar(out, c);
    }
    return g_string_free(out, FALSE);
}

typedef struct {
    const guint8* data;
    gsize len;
    gboolean encrypted;
    GPtrArray* list_slides;   /* from SlideListWithText instance 0 */
    GPtrArray* list_notes;    /* instance 2 */
    GPtrArray* slides;        /* from the slide containers */
    GPtrArray* notes;         /* from the notes containers after the first slide */
} Ppt;

static void add_text(Slide* s, gint* pending_type, guint16 type, const guint8* d, guint32 len) {
    if (s == NULL) return;
    gchar* t = atom_text(type, d, len);
    g_strstrip(t);
    /* "*" stands for a field (slide number, date) */
    if (t[0] == '\0' || strcmp(t, "*") == 0) {
        g_free(t);
        return;
    }
    Text text = {*pending_type, t};
    g_array_append_val(s->texts, text);
    *pending_type = TT_OTHER;
}

/* Text atoms under [p, end), into slide */
static void collect(Ppt* ppt, gsize p, gsize end, gint depth, Slide* slide, gint* pending_type) {
    while (p + 8 <= end) {
        guint16 verinst = u16(ppt->data + p), type = u16(ppt->data + p + 2);
        guint32 len = u32(ppt->data + p + 4);
        if (len > end - p - 8) return;
        const guint8* d = ppt->data + p + 8;
        if (type == RT_DOCUMENT_ENCRYPTION) ppt->encrypted = TRUE;
        if (type == RT_TEXT_HEADER && len >= 4) *pending_type = (gint)u32(d);
        else if (type == RT_TEXT_CHARS || type == RT_TEXT_BYTES) add_text(slide, pending_type, type, d, len);
        else if ((verinst & 0xF) == 0xF && depth < MAX_DEPTH) collect(ppt, p + 8, p + 8 + len, depth + 1, slide, pending_type);
        p += 8 + (gsize)len;
    }
}

/* SlideListWithText: each SlidePersistAtom starts a slide */
static void read_slide_list(Ppt* ppt, gsize p, gsize end, GPtrArray* out) {
    Slide* cur = NULL;
    gint pending = TT_OTHER;
    while (p + 8 <= end) {
        guint16 type = u16(ppt->data + p + 2);
        guint32 len = u32(ppt->data + p + 4);
        if (len > end - p - 8) return;
        const guint8* d = ppt->data + p + 8;
        if (type == RT_SLIDE_PERSIST) {
            cur = slide_new();
            g_ptr_array_add(out, cur);
            pending = TT_OTHER;
        } else if (type == RT_TEXT_HEADER && len >= 4) {
            pending = (gint)u32(d);
        } else if (type == RT_TEXT_CHARS || type == RT_TEXT_BYTES) {
            add_text(cur, &pending, type, d, len);
        }
        p += 8 + (gsize)len;
    }
}

static void read_document(Ppt* ppt, gsize p, gsize end) {
    while (p + 8 <= end) {
        guint16 verinst = u16(ppt->data + p), type = u16(ppt->data + p + 2);
        guint32 len = u32(ppt->data + p + 4);
        if (len > end - p - 8) return;
        if (type == RT_SLIDE_LIST_WITH_TEXT) {
            guint inst = verinst >> 4;
            if (inst == 0) read_slide_list(ppt, p + 8, p + 8 + len, ppt->list_slides);
            else if (inst == 2) read_slide_list(ppt, p + 8, p + 8 + len, ppt->list_notes);
        } else if (type == RT_DOCUMENT_ENCRYPTION) {
            ppt->encrypted = TRUE;
        }
        p += 8 + (gsize)len;
    }
}

static gboolean has_text(GPtrArray* slides) {
    for (guint i = 0; i < slides->len; i++) {
        if (((Slide*)g_ptr_array_index(slides, i))->texts->len > 0) return TRUE;
    }
    return FALSE;
}

static gboolean is_title(gint type) {
    return type == TT_TITLE || type == TT_CENTER_TITLE;
}

static gboolean is_body(gint type) {
    return type == TT_BODY || type == TT_CENTER_BODY || type == TT_HALF_BODY || type == TT_QUARTER_BODY;
}

static void append_escaped(GString* out, const gchar* s) {
    gchar* e = g_markup_escape_text(s, -1);
    g_string_append(out, e);
    g_free(e);
}

static void render_slide(GString* html, guint number, const Slide* s, const Slide* notes) {
    if (number > 1) g_string_append(html, "<hr>\n");
    g_string_append_printf(html, "<h2>Slide %u</h2>\n", number);
    /* The title: the title placeholder, else the first text */
    gint title = -1;
    for (guint i = 0; i < s->texts->len && title < 0; i++) {
        if (is_title(g_array_index(s->texts, Text, i).type)) title = (gint)i;
    }
    if (title < 0 && s->texts->len > 0) title = 0;
    if (title >= 0) {
        gchar* t = g_strdup(g_array_index(s->texts, Text, title).text);
        for (gchar* c = t; *c != '\0'; c++) {
            if (*c == '\n') *c = ' ';
        }
        g_string_append(html, "<h3>");
        append_escaped(html, t);
        g_string_append(html, "</h3>\n");
        g_free(t);
    }
    for (guint i = 0; i < s->texts->len; i++) {
        if ((gint)i == title) continue;
        const Text* t = &g_array_index(s->texts, Text, i);
        gchar** paras = g_strsplit(t->text, "\n", -1);
        guint n = g_strv_length(paras);
        /* Several paragraphs or a body placeholder: a list, like pptx */
        gboolean list = is_body(t->type) || n > 1;
        if (list) g_string_append(html, "<ul>\n");
        for (guint k = 0; k < n; k++) {
            g_strstrip(paras[k]);
            if (paras[k][0] == '\0') continue;
            g_string_append(html, list ? "<li>" : "<p>");
            append_escaped(html, paras[k]);
            g_string_append(html, list ? "</li>\n" : "</p>\n");
        }
        if (list) g_string_append(html, "</ul>\n");
        g_strfreev(paras);
    }
    if (notes != NULL && notes->texts->len > 0) {
        GString* all = g_string_new(NULL);
        for (guint i = 0; i < notes->texts->len; i++) {
            if (all->len > 0) g_string_append_c(all, ' ');
            g_string_append(all, g_array_index(notes->texts, Text, i).text);
        }
        g_string_append(html, "<p><i>Notes: ");
        append_escaped(html, all->str);
        g_string_append(html, "</i></p>\n");
        g_string_free(all, TRUE);
    }
}

gchar* ppt_reader_to_html(OleReader* ole, const gchar* head, gsize max_text, guint* slides, gboolean* encrypted,
                          GError** error) {
    if (encrypted != NULL) *encrypted = FALSE;
    if (slides != NULL) *slides = 0;
    GBytes* stream = ole_reader_read(ole, "PowerPoint Document", PPT_MAX_STREAM, error);
    if (stream == NULL) return NULL;
    Ppt ppt = {0};
    ppt.data = g_bytes_get_data(stream, &ppt.len);
    ppt.list_slides = g_ptr_array_new_with_free_func(slide_free);
    ppt.list_notes = g_ptr_array_new_with_free_func(slide_free);
    ppt.slides = g_ptr_array_new_with_free_func(slide_free);
    ppt.notes = g_ptr_array_new_with_free_func(slide_free);
    /* Top-level containers in stream order */
    gsize p = 0;
    gboolean any = FALSE;
    while (p + 8 <= ppt.len) {
        guint16 type = u16(ppt.data + p + 2);
        guint32 len = u32(ppt.data + p + 4);
        if (len > ppt.len - p - 8) break;
        any = TRUE;
        gint pending = TT_OTHER;
        if (type == RT_DOCUMENT) {
            read_document(&ppt, p + 8, p + 8 + len);
        } else if (type == RT_SLIDE) {
            Slide* s = slide_new();
            collect(&ppt, p + 8, p + 8 + len, 1, s, &pending);
            g_ptr_array_add(ppt.slides, s);
        } else if (type == RT_NOTES && ppt.slides->len > 0) {
            /* Notes containers before the first slide are the notes master */
            Slide* s = slide_new();
            collect(&ppt, p + 8, p + 8 + len, 1, s, &pending);
            g_ptr_array_add(ppt.notes, s);
        } else if (type == RT_DOCUMENT_ENCRYPTION) {
            ppt.encrypted = TRUE;
        }
        /* Masters (RT_MAIN_MASTER) and other records are skipped */
        p += 8 + (gsize)len;
    }
    gchar* html = NULL;
    if (ppt.encrypted) {
        if (encrypted != NULL) *encrypted = TRUE;
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "the presentation is encrypted");
    } else if (!any) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "no presentation records");
    } else {
        GPtrArray* shown = has_text(ppt.list_slides) ? ppt.list_slides : ppt.slides;
        GPtrArray* notes = has_text(ppt.list_notes) ? ppt.list_notes : ppt.notes;
        if (shown->len == 0) shown = ppt.list_slides;
        GString* out = g_string_new("<html>");
        g_string_append(out, head != NULL ? head : "<head></head>");
        g_string_append(out, "<body>\n");
        for (guint i = 0; i < shown->len; i++) {
            if (out->len > max_text) {
                g_string_append(out, "<p><i>The presentation continues; the preview stops here.</i></p>\n");
                break;
            }
            render_slide(out, i + 1, g_ptr_array_index(shown, i), i < notes->len ? g_ptr_array_index(notes, i) : NULL);
        }
        if (shown->len == 0) g_string_append(out, "<p><i>(no slides)</i></p>\n");
        g_string_append(out, "</body></html>");
        html = g_string_free(out, FALSE);
        if (slides != NULL) *slides = shown->len;
    }
    g_ptr_array_unref(ppt.list_slides);
    g_ptr_array_unref(ppt.list_notes);
    g_ptr_array_unref(ppt.slides);
    g_ptr_array_unref(ppt.notes);
    g_bytes_unref(stream);
    return html;
}
