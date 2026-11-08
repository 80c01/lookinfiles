/* RTF to HTML (see rtf.h). */
#include "rtf.h"
#include "i18n.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { DEST_TEXT, DEST_SKIP, DEST_TITLE, DEST_AUTHOR, DEST_PICT } Destination;

typedef struct {
    gboolean bold;
    gboolean italic;
    gboolean underline;
    gboolean strike;
    gint uc;               /* characters to skip after \uN */
    Destination dest;
    gboolean in_table;     /* \intbl */
} State;

typedef struct {
    GString* out;
    GArray* stack;         /* State */
    State st;
    State shown;           /* formatting of the tags currently open */
    gboolean par_open;
    gboolean row_open;
    gboolean cell_open;
    gint skip_chars;       /* fallback characters after \uN */
    gchar* charset;        /* iconv name of \ansicpg */
    GString* title;
    GString* author;
    GString* pict;         /* hex data of the current picture */
    const gchar* pict_mime;
    gsize text_len;
    gsize max_text;
    gboolean truncated;
} Rtf;

static void close_tags(Rtf* r) {
    if (r->shown.strike) g_string_append(r->out, "</s>");
    if (r->shown.underline) g_string_append(r->out, "</u>");
    if (r->shown.italic) g_string_append(r->out, "</i>");
    if (r->shown.bold) g_string_append(r->out, "</b>");
    r->shown.bold = r->shown.italic = r->shown.underline = r->shown.strike = FALSE;
}

static void sync_tags(Rtf* r) {
    if (r->shown.bold == r->st.bold && r->shown.italic == r->st.italic &&
        r->shown.underline == r->st.underline && r->shown.strike == r->st.strike) {
        return;
    }
    close_tags(r);
    if (r->st.bold) g_string_append(r->out, "<b>");
    if (r->st.italic) g_string_append(r->out, "<i>");
    if (r->st.underline) g_string_append(r->out, "<u>");
    if (r->st.strike) g_string_append(r->out, "<s>");
    r->shown.bold = r->st.bold;
    r->shown.italic = r->st.italic;
    r->shown.underline = r->st.underline;
    r->shown.strike = r->st.strike;
}

static void open_par(Rtf* r) {
    if (r->st.in_table && !r->row_open) {
        g_string_append(r->out, "<tr>");
        r->row_open = TRUE;
    }
    if (r->st.in_table && !r->cell_open) {
        g_string_append(r->out, "<td>");
        r->cell_open = TRUE;
        return;
    }
    if (!r->st.in_table && !r->par_open) {
        g_string_append(r->out, "<p>");
        r->par_open = TRUE;
    }
}

/* Text of the current destination (UTF-8) */
static void emit(Rtf* r, const gchar* text, gsize len) {
    if (len == 0) {
        return;
    }
    switch (r->st.dest) {
    case DEST_SKIP:
    case DEST_PICT:
        return;
    case DEST_TITLE:
        g_string_append_len(r->title, text, (gssize)len);
        return;
    case DEST_AUTHOR:
        g_string_append_len(r->author, text, (gssize)len);
        return;
    case DEST_TEXT:
        break;
    }
    if (r->truncated) {
        return;
    }
    open_par(r);
    sync_tags(r);
    gchar* escaped = g_markup_escape_text(text, (gssize)len);
    g_string_append(r->out, escaped);
    g_free(escaped);
    r->text_len += len;
    if (r->text_len > r->max_text) {
        r->truncated = TRUE;
    }
}

static void emit_char(Rtf* r, gunichar c) {
    gchar buf[8];
    gint n = g_unichar_to_utf8(c, buf);
    emit(r, buf, (gsize)n);
}

static void end_par(Rtf* r) {
    if (r->st.dest != DEST_TEXT || r->truncated) {
        return;
    }
    close_tags(r);
    if (r->st.in_table) {
        if (r->cell_open) {
            g_string_append(r->out, "<br>");
        }
        return;
    }
    if (r->par_open) {
        g_string_append(r->out, "</p>\n");
        r->par_open = FALSE;
    } else {
        g_string_append(r->out, "<p></p>\n");
    }
}

static void end_cell(Rtf* r) {
    close_tags(r);
    open_par(r);
    if (r->cell_open) {
        g_string_append(r->out, "</td>");
        r->cell_open = FALSE;
    }
}

static void end_row(Rtf* r) {
    if (r->cell_open) {
        end_cell(r);
    }
    if (r->row_open) {
        g_string_append(r->out, "</tr>\n");
        r->row_open = FALSE;
    }
}

/* Bytes of a code page as UTF-8 */
static void emit_codepage_byte(Rtf* r, guchar byte) {
    gchar in[2] = { (gchar)byte, 0 };
    gchar* utf8 = g_convert(in, 1, "UTF-8", r->charset != NULL ? r->charset : "WINDOWS-1252", NULL, NULL, NULL);
    if (utf8 == NULL) {
        utf8 = g_convert(in, 1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
    }
    if (utf8 != NULL) {
        emit(r, utf8, strlen(utf8));
        g_free(utf8);
    }
}

static void control_word(Rtf* r, const gchar* word, gboolean has_param, glong param, gboolean starred) {
    gboolean on = !has_param || param != 0;
    if (starred && r->st.dest == DEST_TEXT) {
        r->st.dest = DEST_SKIP; /* {\*\unknown ...} */
        return;
    }
#define IS(w) (strcmp(word, (w)) == 0)
    if (IS("par") || IS("sect")) {
        end_par(r);
    } else if (IS("line")) {
        if (r->st.dest == DEST_TEXT && !r->truncated) {
            open_par(r);
            g_string_append(r->out, "<br>");
        }
    } else if (IS("tab")) {
        emit(r, "    ", 4);
    } else if (IS("b")) {
        r->st.bold = on;
    } else if (IS("i")) {
        r->st.italic = on;
    } else if (IS("ul") || IS("uld") || IS("uldb") || IS("ulw")) {
        r->st.underline = on;
    } else if (IS("ulnone")) {
        r->st.underline = FALSE;
    } else if (IS("strike") || IS("striked")) {
        r->st.strike = on;
    } else if (IS("plain")) {
        r->st.bold = r->st.italic = r->st.underline = r->st.strike = FALSE;
    } else if (IS("pard")) {
        r->st.in_table = FALSE;
    } else if (IS("intbl")) {
        r->st.in_table = TRUE;
        if (r->par_open) {
            close_tags(r);
            g_string_append(r->out, "</p>\n");
            r->par_open = FALSE;
        }
    } else if (IS("trowd")) {
        if (r->par_open) {
            close_tags(r);
            g_string_append(r->out, "</p>\n");
            r->par_open = FALSE;
        }
        if (!r->row_open && r->st.dest == DEST_TEXT) {
            /* a table starts unless one is open */
            if (r->out->len < 7 || !g_str_has_suffix(r->out->str, "</tr>\n")) {
                g_string_append(r->out, "<table>\n");
            }
        }
    } else if (IS("cell")) {
        end_cell(r);
    } else if (IS("row")) {
        end_row(r);
    } else if (IS("u")) {
        glong c = param < 0 ? param + 65536 : param;
        if (c > 0) {
            emit_char(r, (gunichar)c);
        }
        r->skip_chars = r->st.uc;
    } else if (IS("uc")) {
        r->st.uc = (gint)param;
    } else if (IS("ansicpg")) {
        g_free(r->charset);
        r->charset = g_strdup_printf("CP%ld", param);
    } else if (IS("emdash")) {
        emit(r, "—", strlen("—"));
    } else if (IS("endash")) {
        emit(r, "–", strlen("–"));
    } else if (IS("bullet")) {
        emit(r, "•", strlen("•"));
    } else if (IS("lquote")) {
        emit(r, "‘", strlen("‘"));
    } else if (IS("rquote")) {
        emit(r, "’", strlen("’"));
    } else if (IS("ldblquote")) {
        emit(r, "“", strlen("“"));
    } else if (IS("rdblquote")) {
        emit(r, "”", strlen("”"));
    } else if (IS("fonttbl") || IS("colortbl") || IS("stylesheet") || IS("header") || IS("footer") ||
               IS("headerl") || IS("headerr") || IS("footerl") || IS("footerr") || IS("footnote") ||
               IS("listtable") || IS("listoverridetable") || IS("revtbl") || IS("rsidtbl") ||
               IS("generator") || IS("xmlnstbl") || IS("themedata") || IS("colorschememapping") ||
               IS("datastore") || IS("latentstyles") || IS("pgdsctbl") || IS("operator") ||
               IS("company") || IS("subject") || IS("keywords") || IS("comment") || IS("doccomm") ||
               IS("creatim") || IS("revtim") || IS("printim") || IS("buptim") || IS("fldinst") ||
               IS("object") || IS("shpinst") || IS("nonshppict")) {
        r->st.dest = DEST_SKIP;
    } else if (IS("info")) {
        r->st.dest = DEST_SKIP;
    } else if (IS("title")) {
        r->st.dest = DEST_TITLE;
    } else if (IS("author")) {
        r->st.dest = DEST_AUTHOR;
    } else if (IS("pict")) {
        r->st.dest = DEST_PICT;
        g_string_truncate(r->pict, 0);
        r->pict_mime = NULL;
    } else if (IS("pngblip") && r->st.dest == DEST_PICT) {
        r->pict_mime = "image/png";
    } else if (IS("jpegblip") && r->st.dest == DEST_PICT) {
        r->pict_mime = "image/jpeg";
    }
#undef IS
}

/* End of a picture group: its hex data as an image */
static void end_picture(Rtf* r) {
    if (r->pict_mime == NULL || r->pict->len < 2 || r->truncated) {
        return;
    }
    gsize n = r->pict->len / 2;
    guchar* bytes = g_malloc(n);
    for (gsize i = 0; i < n; i++) {
        bytes[i] = (guchar)((g_ascii_xdigit_value(r->pict->str[2 * i]) << 4) |
                            g_ascii_xdigit_value(r->pict->str[2 * i + 1]));
    }
    gchar* b64 = g_base64_encode(bytes, n);
    g_free(bytes);
    open_par(r);
    g_string_append_printf(r->out, "<img src=\"data:%s;base64,%s\">", r->pict_mime, b64);
    g_free(b64);
}

gchar* rtf_to_html(const gchar* rtf, gsize len, gsize max_text, GError** error) {
    if (len < 5 || strncmp(rtf, "{\\rtf", 5) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("not an RTF document"));
        return NULL;
    }
    Rtf r = { 0 };
    r.out = g_string_new(NULL);
    r.stack = g_array_new(FALSE, FALSE, sizeof(State));
    r.st.uc = 1;
    r.title = g_string_new(NULL);
    r.author = g_string_new(NULL);
    r.pict = g_string_new(NULL);
    r.max_text = max_text;

    gsize i = 0;
    while (i < len) {
        gchar c = rtf[i];
        if (c == '{') {
            g_array_append_val(r.stack, r.st);
            i++;
            /* {\*\destination}: skipped unless known */
            if (i + 1 < len && rtf[i] == '\\' && rtf[i + 1] == '*') {
                if (r.st.dest == DEST_TEXT) {
                    r.st.dest = DEST_SKIP;
                }
                i += 2;
            }
            continue;
        }
        if (c == '}') {
            if (r.st.dest == DEST_PICT && (r.stack->len == 0 ||
                                           g_array_index(r.stack, State, r.stack->len - 1).dest != DEST_PICT)) {
                end_picture(&r);
            }
            if (r.stack->len > 0) {
                r.st = g_array_index(r.stack, State, r.stack->len - 1);
                g_array_set_size(r.stack, r.stack->len - 1);
            }
            i++;
            continue;
        }
        if (c == '\\' && i + 1 < len) {
            gchar n = rtf[i + 1];
            if (g_ascii_isalpha(n)) {
                gsize j = i + 1;
                while (j < len && g_ascii_isalpha(rtf[j]) && j - i < 32) {
                    j++;
                }
                gchar word[33];
                memcpy(word, rtf + i + 1, j - i - 1);
                word[j - i - 1] = '\0';
                gboolean has_param = FALSE;
                glong param = 0;
                if (j < len && (rtf[j] == '-' || g_ascii_isdigit(rtf[j]))) {
                    gchar* end = NULL;
                    param = strtol(rtf + j, &end, 10);
                    has_param = TRUE;
                    j = (gsize)(end - rtf);
                }
                if (j < len && rtf[j] == ' ') {
                    j++; /* delimiter */
                }
                control_word(&r, word, has_param, param, FALSE);
                i = j;
                continue;
            }
            if (n == '\'' && i + 3 < len) {
                gint hi = g_ascii_xdigit_value(rtf[i + 2]);
                gint lo = g_ascii_xdigit_value(rtf[i + 3]);
                if (r.skip_chars > 0) {
                    r.skip_chars--;
                } else if (hi >= 0 && lo >= 0) {
                    emit_codepage_byte(&r, (guchar)(hi * 16 + lo));
                }
                i += 4;
                continue;
            }
            /* Control symbols */
            if (n == '~') {
                emit(&r, "\xC2\xA0", 2);
            } else if (n == '_') {
                emit(&r, "-", 1);
            } else if (n == '\\' || n == '{' || n == '}') {
                emit(&r, &rtf[i + 1], 1);
            } else if (n == '\n' || n == '\r') {
                end_par(&r);
            }
            i += 2;
            continue;
        }
        if (c == '\r' || c == '\n') {
            i++;
            continue;
        }
        if (r.st.dest == DEST_PICT) {
            if (g_ascii_isxdigit(c)) {
                g_string_append_c(r.pict, c);
            }
            i++;
            continue;
        }
        if (r.skip_chars > 0) {
            r.skip_chars--;
            i++;
            continue;
        }
        /* A run of plain characters (bytes of the code page if not ASCII) */
        gsize j = i;
        while (j < len && rtf[j] != '\\' && rtf[j] != '{' && rtf[j] != '}' && rtf[j] != '\r' &&
               rtf[j] != '\n' && (guchar)rtf[j] < 0x80) {
            j++;
        }
        if (j > i) {
            emit(&r, rtf + i, j - i);
            i = j;
        } else {
            emit_codepage_byte(&r, (guchar)c);
            i++;
        }
    }
    close_tags(&r);
    if (r.par_open) {
        g_string_append(r.out, "</p>\n");
    }
    end_row(&r);
    if (strstr(r.out->str, "<table>") != NULL) {
        /* Close the tables: every run of rows ends before the next paragraph */
        GString* fixed = g_string_new(NULL);
        gboolean in_table = FALSE;
        gchar** lines = g_strsplit(r.out->str, "\n", -1);
        for (gint k = 0; lines[k] != NULL; k++) {
            if (g_str_has_prefix(lines[k], "<table>")) {
                in_table = TRUE;
            } else if (in_table && !g_str_has_prefix(lines[k], "<tr>")) {
                g_string_append(fixed, "</table>\n");
                in_table = FALSE;
            }
            g_string_append(fixed, lines[k]);
            if (lines[k + 1] != NULL) {
                g_string_append_c(fixed, '\n');
            }
        }
        if (in_table) {
            g_string_append(fixed, "</table>\n");
        }
        g_strfreev(lines);
        g_string_free(r.out, TRUE);
        r.out = fixed;
    }

    GString* doc = g_string_new("<html><head>");
    g_strstrip(r.title->str);
    g_strstrip(r.author->str);
    if (r.title->str[0] != '\0') {
        gchar* t = g_markup_escape_text(r.title->str, -1);
        g_string_append_printf(doc, "<title>%s</title>", t);
        g_free(t);
    }
    if (r.author->str[0] != '\0') {
        gchar* a = g_markup_escape_text(r.author->str, -1);
        g_string_append_printf(doc, "<meta name=\"author\" content=\"%s\">", a);
        g_free(a);
    }
    g_string_append(doc, "</head><body>\n");
    g_string_append_len(doc, r.out->str, (gssize)r.out->len);
    if (r.truncated) {
        g_string_append_printf(doc, "<hr>\n<p><i>%s</i></p>\n", _("Document truncated: only the beginning is shown."));
    }
    g_string_append(doc, "</body></html>\n");
    g_string_free(r.out, TRUE);
    g_array_free(r.stack, TRUE);
    g_string_free(r.title, TRUE);
    g_string_free(r.author, TRUE);
    g_string_free(r.pict, TRUE);
    g_free(r.charset);
    return g_string_free(doc, FALSE);
}
