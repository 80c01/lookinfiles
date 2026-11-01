/* File type detection (by name, and by content: magic numbers) and icons.
 * No AppData, no widgets; the only I/O is file_kind_of_path() (unit-tested in tests/). */
#include "file_types.h"
#include "text_encoding.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

/* File extension ("" if none), ignoring dot-files initial dot */
const gchar* file_extension(const gchar* name) {
    if (name == NULL) {
        return "";
    }
    const gchar* dot = strrchr(name, '.');
    return (dot != NULL && dot != name) ? dot + 1 : "";
}

/* Return TRUE if file name indicates Markdown content */
gboolean is_markdown_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "md") == 0 ||
            g_ascii_strcasecmp(ext, "markdown") == 0 ||
            g_ascii_strcasecmp(ext, "mdown") == 0 ||
            g_ascii_strcasecmp(ext, "mkd") == 0 ||
            g_ascii_strcasecmp(ext, "mkdn") == 0);
}

/* Return TRUE if file name indicates HTML content */
gboolean is_html_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "html") == 0 ||
            g_ascii_strcasecmp(ext, "htm") == 0 ||
            g_ascii_strcasecmp(ext, "xhtml") == 0 ||
            g_ascii_strcasecmp(ext, "shtml") == 0);
}

/* Return TRUE if file name indicates Emacs Org-mode content */
gboolean is_org_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "org") == 0 ||
            g_ascii_strcasecmp(ext, "org_archive") == 0);
}

gboolean is_raw_photo_file(const gchar* name) {
    static const gchar* const RAW[] = { "cr2", "nef", "nrw", "arw", "srf", "sr2", "dng", "orf", "rw2",
                                        "pef", "srw", "raf", "3fr", "erf", "kdc", "mos", "iiq" };
    const gchar* ext = file_extension(name);
    for (gsize i = 0; i < G_N_ELEMENTS(RAW); i++) {
        if (g_ascii_strcasecmp(ext, RAW[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Return TRUE if file name indicates an image format */
gboolean is_image_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "png") == 0 ||
            g_ascii_strcasecmp(ext, "jpg") == 0 ||
            g_ascii_strcasecmp(ext, "jpeg") == 0 ||
            g_ascii_strcasecmp(ext, "gif") == 0 ||
            g_ascii_strcasecmp(ext, "svg") == 0 ||
            g_ascii_strcasecmp(ext, "svgz") == 0 ||
            g_ascii_strcasecmp(ext, "bmp") == 0 ||
            g_ascii_strcasecmp(ext, "ico") == 0 ||
            g_ascii_strcasecmp(ext, "webp") == 0 ||
            g_ascii_strcasecmp(ext, "tiff") == 0 ||
            g_ascii_strcasecmp(ext, "tif") == 0 ||
            g_ascii_strcasecmp(ext, "pnm") == 0 ||
            g_ascii_strcasecmp(ext, "pbm") == 0 ||
            g_ascii_strcasecmp(ext, "pgm") == 0 ||
            g_ascii_strcasecmp(ext, "ppm") == 0 ||
            g_ascii_strcasecmp(ext, "xpm") == 0 ||
            g_ascii_strcasecmp(ext, "xbm") == 0 ||
            g_ascii_strcasecmp(ext, "tga") == 0 ||
            g_ascii_strcasecmp(ext, "icns") == 0 ||
            g_ascii_strcasecmp(ext, "cur") == 0 ||
            g_ascii_strcasecmp(ext, "heic") == 0 || g_ascii_strcasecmp(ext, "heif") == 0 ||
            g_ascii_strcasecmp(ext, "avif") == 0 || g_ascii_strcasecmp(ext, "jxl") == 0 ||
            is_raw_photo_file(name));
}

/* Return TRUE if file name indicates a PDF document */
gboolean is_pdf_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "pdf") == 0);
}

gboolean is_word_document_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "docx") == 0 ||
            g_ascii_strcasecmp(ext, "docm") == 0 ||
            g_ascii_strcasecmp(ext, "dotx") == 0 ||
            g_ascii_strcasecmp(ext, "dotm") == 0);
}

/* Return TRUE if file name indicates a video file */
gboolean is_video_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "mp4") == 0 ||
            g_ascii_strcasecmp(ext, "m4v") == 0 ||
            g_ascii_strcasecmp(ext, "mkv") == 0 ||
            g_ascii_strcasecmp(ext, "webm") == 0 ||
            g_ascii_strcasecmp(ext, "mov") == 0 ||
            g_ascii_strcasecmp(ext, "qt") == 0 ||
            g_ascii_strcasecmp(ext, "avi") == 0 ||
            g_ascii_strcasecmp(ext, "flv") == 0 ||
            g_ascii_strcasecmp(ext, "wmv") == 0 ||
            g_ascii_strcasecmp(ext, "asf") == 0 ||
            g_ascii_strcasecmp(ext, "ogv") == 0 ||
            g_ascii_strcasecmp(ext, "ts") == 0 ||
            g_ascii_strcasecmp(ext, "mts") == 0 ||
            g_ascii_strcasecmp(ext, "m2ts") == 0 ||
            g_ascii_strcasecmp(ext, "3gp") == 0 ||
            g_ascii_strcasecmp(ext, "vob") == 0 ||
            g_ascii_strcasecmp(ext, "mpg") == 0 ||
            g_ascii_strcasecmp(ext, "mpeg") == 0);
}

/* Return TRUE if file name indicates an audio file */
gboolean is_audio_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "mp3") == 0 ||
            g_ascii_strcasecmp(ext, "wav") == 0 ||
            g_ascii_strcasecmp(ext, "wave") == 0 ||
            g_ascii_strcasecmp(ext, "ogg") == 0 ||
            g_ascii_strcasecmp(ext, "oga") == 0 ||
            g_ascii_strcasecmp(ext, "opus") == 0 ||
            g_ascii_strcasecmp(ext, "flac") == 0 ||
            g_ascii_strcasecmp(ext, "aac") == 0 ||
            g_ascii_strcasecmp(ext, "m4a") == 0 ||
            g_ascii_strcasecmp(ext, "wma") == 0 ||
            g_ascii_strcasecmp(ext, "aiff") == 0 ||
            g_ascii_strcasecmp(ext, "aif") == 0 ||
            g_ascii_strcasecmp(ext, "alac") == 0 ||
            g_ascii_strcasecmp(ext, "mid") == 0 ||
            g_ascii_strcasecmp(ext, "midi") == 0);
}

/* Return TRUE if file content can be rendered richly in preview pane */
gboolean is_renderable_file(const gchar* name) {
    return is_markdown_file(name) || is_html_file(name) || is_org_file(name) || is_image_file(name) || is_pdf_file(name) || is_video_file(name) || is_audio_file(name) ||
           is_word_document_file(name);
}

/* Extensions of the kinds that have no is_*_file() helper */
static const struct {
    const gchar* ext;
    FileKind kind;
} KIND_EXTENSIONS[] = {
    { "odt", FILE_KIND_DOCUMENT }, { "ott", FILE_KIND_DOCUMENT },
    { "xlsx", FILE_KIND_SPREADSHEET }, { "xlsm", FILE_KIND_SPREADSHEET },
    { "xltx", FILE_KIND_SPREADSHEET }, { "xltm", FILE_KIND_SPREADSHEET },
    { "ods", FILE_KIND_SPREADSHEET }, { "ots", FILE_KIND_SPREADSHEET },
    { "pptx", FILE_KIND_PRESENTATION }, { "pptm", FILE_KIND_PRESENTATION },
    { "ppsx", FILE_KIND_PRESENTATION }, { "potx", FILE_KIND_PRESENTATION },
    { "odp", FILE_KIND_PRESENTATION }, { "otp", FILE_KIND_PRESENTATION },
    { "epub", FILE_KIND_EBOOK },
    { "zip", FILE_KIND_ARCHIVE }, { "jar", FILE_KIND_ARCHIVE }, { "war", FILE_KIND_ARCHIVE },
    { "ear", FILE_KIND_ARCHIVE }, { "apk", FILE_KIND_ARCHIVE }, { "aar", FILE_KIND_ARCHIVE },
    { "whl", FILE_KIND_ARCHIVE }, { "nupkg", FILE_KIND_ARCHIVE }, { "vsix", FILE_KIND_ARCHIVE },
    { "xpi", FILE_KIND_ARCHIVE }, { "cbz", FILE_KIND_ARCHIVE },
    { "tar", FILE_KIND_ARCHIVE }, { "tgz", FILE_KIND_ARCHIVE }, { "gz", FILE_KIND_ARCHIVE },
    { "tbz", FILE_KIND_ARCHIVE }, { "tbz2", FILE_KIND_ARCHIVE }, { "bz2", FILE_KIND_ARCHIVE },
    { "xz", FILE_KIND_ARCHIVE }, { "txz", FILE_KIND_ARCHIVE }, { "lz", FILE_KIND_ARCHIVE },
    { "lzma", FILE_KIND_ARCHIVE }, { "zst", FILE_KIND_ARCHIVE }, { "tzst", FILE_KIND_ARCHIVE },
    { "7z", FILE_KIND_ARCHIVE }, { "rar", FILE_KIND_ARCHIVE }, { "cbr", FILE_KIND_ARCHIVE },
    { "deb", FILE_KIND_ARCHIVE }, { "a", FILE_KIND_ARCHIVE }, { "iso", FILE_KIND_ARCHIVE },
    { "cpio", FILE_KIND_ARCHIVE }, { "rpm", FILE_KIND_ARCHIVE }, { "cab", FILE_KIND_ARCHIVE },
    { "lzh", FILE_KIND_ARCHIVE }, { "lha", FILE_KIND_ARCHIVE }, { "xar", FILE_KIND_ARCHIVE },
    { "lz4", FILE_KIND_ARCHIVE }, { "Z", FILE_KIND_ARCHIVE },
    { "ttf", FILE_KIND_FONT }, { "otf", FILE_KIND_FONT }, { "ttc", FILE_KIND_FONT },
    { "otc", FILE_KIND_FONT }, { "woff", FILE_KIND_FONT }, { "woff2", FILE_KIND_FONT },
    { "sqlite", FILE_KIND_DATABASE }, { "sqlite3", FILE_KIND_DATABASE },
    { "db", FILE_KIND_DATABASE }, { "db3", FILE_KIND_DATABASE },
    { "gpkg", FILE_KIND_DATABASE }, { "mbtiles", FILE_KIND_DATABASE },
    { "pem", FILE_KIND_CERTIFICATE }, { "crt", FILE_KIND_CERTIFICATE },
    { "cer", FILE_KIND_CERTIFICATE }, { "der", FILE_KIND_CERTIFICATE },
    { "csr", FILE_KIND_CERTIFICATE }, { "key", FILE_KIND_CERTIFICATE },
    { "csv", FILE_KIND_CSV }, { "tsv", FILE_KIND_CSV }, { "tab", FILE_KIND_CSV },
    { "psv", FILE_KIND_CSV },
    { "json", FILE_KIND_JSON }, { "jsonl", FILE_KIND_JSON }, { "ndjson", FILE_KIND_JSON },
    { "geojson", FILE_KIND_JSON }, { "jsonc", FILE_KIND_JSON }, { "json5", FILE_KIND_JSON },
    { "webmanifest", FILE_KIND_JSON }, { "har", FILE_KIND_JSON },
    { "ipynb", FILE_KIND_NOTEBOOK },
    { "diff", FILE_KIND_DIFF }, { "patch", FILE_KIND_DIFF }, { "rej", FILE_KIND_DIFF },
    { "log", FILE_KIND_LOG },
    { "ics", FILE_KIND_CALENDAR }, { "ical", FILE_KIND_CALENDAR }, { "ifb", FILE_KIND_CALENDAR },
    { "vcs", FILE_KIND_CALENDAR },
    { "vcf", FILE_KIND_CONTACT }, { "vcard", FILE_KIND_CONTACT },
    { "desktop", FILE_KIND_DESKTOP },
    { "rtf", FILE_KIND_RTF },
    { "eml", FILE_KIND_MAIL }, { "emlx", FILE_KIND_MAIL }, { "mbox", FILE_KIND_MAIL },
    { "mbx", FILE_KIND_MAIL }, { "msf", FILE_KIND_MAIL }, { "msg", FILE_KIND_OLE },
    { "pst", FILE_KIND_MAIL }, { "ost", FILE_KIND_MAIL }, { "dbx", FILE_KIND_MAIL },
    { "doc", FILE_KIND_OLE }, { "dot", FILE_KIND_OLE }, { "xls", FILE_KIND_OLE }, { "xlt", FILE_KIND_OLE },
    { "ppt", FILE_KIND_OLE }, { "pps", FILE_KIND_OLE }, /* .pot is a gettext template by name; its content decides */
    { "po", FILE_KIND_TRANSLATION }, { "pot", FILE_KIND_TRANSLATION }, { "mo", FILE_KIND_TRANSLATION },
    { "gmo", FILE_KIND_TRANSLATION },
};

/* Manual page section suffix: "1".."9", optionally followed by letters ("3pm", "1ssl") */
static gboolean is_man_section(const gchar* ext) {
    if (ext[0] < '1' || ext[0] > '9') {
        return FALSE;
    }
    gsize n = 1;
    while (g_ascii_isalpha(ext[n])) {
        n++;
    }
    return ext[n] == '\0' && n <= 4;
}

/* Kind of the name without a compression suffix: "ls.1.gz" -> manual page */
static gboolean is_manpage_name(const gchar* name) {
    const gchar* ext = file_extension(name);
    if (is_man_section(ext)) {
        return TRUE;
    }
    if (g_ascii_strcasecmp(ext, "gz") != 0 && g_ascii_strcasecmp(ext, "bz2") != 0 &&
        g_ascii_strcasecmp(ext, "xz") != 0 && g_ascii_strcasecmp(ext, "zst") != 0) {
        return FALSE;
    }
    gchar* base = g_strndup(name, (gsize)(ext - 1 - name));
    gboolean man = is_man_section(file_extension(base));
    g_free(base);
    return man;
}

/* Rotated logs: "syslog.log.1", "app.log.2.gz" */
static gboolean is_rotated_log_name(const gchar* name) {
    const gchar* p = strstr(name, ".log.");
    return p != NULL && g_ascii_isdigit(p[5]);
}

FileKind file_kind_from_name(const gchar* name) {
    if (is_markdown_file(name)) return FILE_KIND_MARKDOWN;
    if (is_html_file(name)) return FILE_KIND_HTML;
    if (is_org_file(name)) return FILE_KIND_ORG;
    if (is_image_file(name)) return FILE_KIND_IMAGE;
    if (is_pdf_file(name)) return FILE_KIND_PDF;
    if (is_video_file(name)) return FILE_KIND_VIDEO;
    if (is_audio_file(name)) return FILE_KIND_AUDIO;
    if (is_word_document_file(name)) return FILE_KIND_DOCUMENT;
    if (name == NULL) return FILE_KIND_TEXT;
    if (is_rotated_log_name(name)) return FILE_KIND_LOG;
    if (is_manpage_name(name)) return FILE_KIND_MANPAGE;
    gchar* base = g_path_get_basename(name);
    gboolean tnef = g_ascii_strcasecmp(base, "winmail.dat") == 0;
    g_free(base);
    if (tnef) return FILE_KIND_MAIL; /* TNEF attachment of Outlook mails */
    const gchar* ext = file_extension(name);
    for (gsize i = 0; i < G_N_ELEMENTS(KIND_EXTENSIONS); i++) {
        if (g_ascii_strcasecmp(ext, KIND_EXTENSIONS[i].ext) == 0) {
            return KIND_EXTENSIONS[i].kind;
        }
    }
    return FILE_KIND_TEXT;
}

gboolean file_kind_is_media(FileKind kind) {
    return kind == FILE_KIND_IMAGE || kind == FILE_KIND_PDF ||
           kind == FILE_KIND_VIDEO || kind == FILE_KIND_AUDIO;
}

gboolean file_kind_is_renderable(FileKind kind) {
    return kind != FILE_KIND_TEXT && kind != FILE_KIND_BINARY;
}

gboolean file_kind_is_text(FileKind kind) {
    switch (kind) {
    case FILE_KIND_TEXT:
    case FILE_KIND_MARKDOWN:
    case FILE_KIND_HTML:
    case FILE_KIND_ORG:
    case FILE_KIND_CSV:
    case FILE_KIND_JSON:
    case FILE_KIND_NOTEBOOK:
    case FILE_KIND_DIFF:
    case FILE_KIND_LOG:
    case FILE_KIND_CALENDAR:
    case FILE_KIND_CONTACT:
    case FILE_KIND_DESKTOP:
    case FILE_KIND_MANPAGE:
    case FILE_KIND_RTF:
    case FILE_KIND_MAIL:         /* messages and mailboxes; m shows their source */
        return TRUE;
    case FILE_KIND_TRANSLATION: /* .po text; .mo files are binary */
        return TRUE;
    default:
        return FALSE;
    }
}

const gchar* file_kind_label(FileKind kind) {
    switch (kind) {
    case FILE_KIND_TEXT:         return NULL;
    case FILE_KIND_MARKDOWN:     return "Markdown";
    case FILE_KIND_HTML:         return "HTML";
    case FILE_KIND_ORG:          return "Org-mode";
    case FILE_KIND_IMAGE:        return "Image";
    case FILE_KIND_PDF:          return "PDF";
    case FILE_KIND_VIDEO:        return "Video";
    case FILE_KIND_AUDIO:        return "Audio";
    case FILE_KIND_BINARY:       return "Binary";
    case FILE_KIND_EXECUTABLE:   return "ELF";
    case FILE_KIND_DOCUMENT:     return "Document";
    case FILE_KIND_SPREADSHEET:  return "Spreadsheet";
    case FILE_KIND_PRESENTATION: return "Presentation";
    case FILE_KIND_EBOOK:        return "EPUB";
    case FILE_KIND_ARCHIVE:      return "Archive";
    case FILE_KIND_FONT:         return "Font";
    case FILE_KIND_DATABASE:     return "SQLite";
    case FILE_KIND_CERTIFICATE:  return "Certificate";
    case FILE_KIND_CSV:          return "CSV";
    case FILE_KIND_JSON:         return "JSON";
    case FILE_KIND_NOTEBOOK:     return "Jupyter";
    case FILE_KIND_DIFF:         return "Diff";
    case FILE_KIND_LOG:          return "Log";
    case FILE_KIND_CALENDAR:     return "Calendar";
    case FILE_KIND_CONTACT:      return "vCard";
    case FILE_KIND_DESKTOP:      return "Desktop Entry";
    case FILE_KIND_MANPAGE:      return "Man Page";
    case FILE_KIND_RTF:          return "Rich Text";
    case FILE_KIND_OLE:          return "Office 97-2003";
    case FILE_KIND_MAIL:         return "Mail";
    case FILE_KIND_TRANSLATION:  return "Translations";
    }
    return NULL;
}

/* TRUE if head starts with the given bytes at offset off */
static gboolean has_bytes(const guchar* head, gsize len, gsize off, const void* magic, gsize mlen) {
    return len >= off + mlen && memcmp(head + off, magic, mlen) == 0;
}
#define HAS(off, lit) has_bytes(head, len, (off), (lit), sizeof(lit) - 1)

/* Case-insensitive search of needle in the first len bytes */
static const guchar* find_ci(const guchar* head, gsize len, const gchar* needle) {
    gsize nlen = strlen(needle);
    for (gsize i = 0; i + nlen <= len; i++) {
        if (g_ascii_strncasecmp((const gchar*)head + i, needle, nlen) == 0) {
            return head + i;
        }
    }
    return NULL;
}

/* TRUE if the bytes look like text: no NUL, valid UTF-8 (a sequence cut by
 * the end of the buffer is fine) or few control characters (legacy 8-bit text). */
static gboolean looks_like_text(const guchar* head, gsize len) {
    if (HAS(0, "\xFF\xFE") || HAS(0, "\xFE\xFF")) {
        return TRUE; /* UTF-16 byte order mark */
    }
    if (g_str_has_prefix(text_encoding_detect(head, len), "UTF-16")) {
        return TRUE; /* UTF-16 without a mark */
    }
    if (memchr(head, '\0', len) != NULL) {
        return FALSE;
    }
    const gchar* end = NULL;
    if (g_utf8_validate((const gchar*)head, (gssize)len, &end) ||
        (gsize)((const guchar*)end - head) + 4 > len) {
        return TRUE;
    }
    gsize controls = 0;
    for (gsize i = 0; i < len; i++) {
        guchar c = head[i];
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' &&
            c != '\b' && c != 0x1b) {
            controls++;
        }
    }
    return controls * 32 <= len;
}

gboolean file_content_is_text(const guchar* head, gsize len) {
    return head == NULL || len == 0 || looks_like_text(head, len);
}

/* Offset of the first non-blank byte, skipping a UTF-8 byte order mark */
static gsize skip_blank(const guchar* head, gsize len) {
    gsize i = HAS(0, "\xEF\xBB\xBF") ? 3 : 0;
    while (i < len && g_ascii_isspace(head[i])) {
        i++;
    }
    return i;
}

/* Magic numbers that cannot be mistaken for text */
static FileKind detect_binary_magic(const guchar* head, gsize len, FileKind by_name) {
    gboolean name_media_av = (by_name == FILE_KIND_AUDIO || by_name == FILE_KIND_VIDEO);

    if (HAS(0, "\x89PNG\r\n\x1a\n") || HAS(0, "\xFF\xD8\xFF") ||
        HAS(0, "GIF87a") || HAS(0, "GIF89a") ||
        (HAS(0, "RIFF") && HAS(8, "WEBP")) ||
        HAS(0, "II*\0") || HAS(0, "MM\0*") ||
        ((HAS(0, "\0\0\1\0") || HAS(0, "\0\0\2\0")) && len >= 6 && (head[4] | head[5]) != 0) || /* ICO, CUR */
        HAS(0, "icns") || (HAS(0, "\x1F\x8B") && by_name == FILE_KIND_IMAGE) /* svgz */ ||
        (HAS(4, "ftyp") && (HAS(8, "avif") || HAS(8, "avis") || HAS(8, "heic") || HAS(8, "mif1"))) ||
        HAS(0, "\xFF\x0A") || HAS(0, "\0\0\0\x0CJXL ")) {
        return FILE_KIND_IMAGE;
    }
    if (HAS(0, "FUJIFILMCCD-RAW") || HAS(0, "IIRO") || (HAS(0, "IIU\0") && by_name == FILE_KIND_IMAGE)) {
        return FILE_KIND_IMAGE; /* camera RAW photos */
    }
    if (HAS(0, "\x7F" "ELF")) {
        return FILE_KIND_EXECUTABLE;
    }
    if (HAS(0, "%PDF-")) {
        return FILE_KIND_PDF;
    }
    /* ZIP: an office document or e-book if the name says so, or if its
     * first entries (local file names are not compressed) show the parts */
    if (HAS(0, "PK\x03\x04") || HAS(0, "PK\x05\x06")) {
        if (by_name == FILE_KIND_DOCUMENT || by_name == FILE_KIND_SPREADSHEET ||
            by_name == FILE_KIND_PRESENTATION || by_name == FILE_KIND_EBOOK) {
            return by_name;
        }
        if (find_ci(head, len, "application/epub+zip") != NULL) {
            return FILE_KIND_EBOOK;
        }
        if (find_ci(head, len, "application/vnd.oasis.opendocument.text") != NULL) {
            return FILE_KIND_DOCUMENT;
        }
        if (find_ci(head, len, "application/vnd.oasis.opendocument.spreadsheet") != NULL) {
            return FILE_KIND_SPREADSHEET;
        }
        if (find_ci(head, len, "application/vnd.oasis.opendocument.presentation") != NULL) {
            return FILE_KIND_PRESENTATION;
        }
        gboolean ooxml = find_ci(head, len, "[Content_Types].xml") != NULL;
        if (find_ci(head, len, "word/document.xml") != NULL ||
            (ooxml && find_ci(head, len, "word/") != NULL)) {
            return FILE_KIND_DOCUMENT;
        }
        if (find_ci(head, len, "xl/workbook.xml") != NULL || (ooxml && find_ci(head, len, "xl/") != NULL)) {
            return FILE_KIND_SPREADSHEET;
        }
        if (find_ci(head, len, "ppt/presentation.xml") != NULL || (ooxml && find_ci(head, len, "ppt/") != NULL)) {
            return FILE_KIND_PRESENTATION;
        }
        return FILE_KIND_ARCHIVE;
    }
    if (HAS(0, "SQLite format 3")) {
        return FILE_KIND_DATABASE;
    }
    if (HAS(0, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1")) {
        return FILE_KIND_OLE; /* compound file: the preview reads its streams */
    }
    if (HAS(0, "\x78\x9F\x3E\x22")) {
        return FILE_KIND_MAIL; /* TNEF winmail.dat */
    }
    if (HAS(0, "!BDN") || HAS(0, "\xCF\xAD\x12\xFE")) {
        return FILE_KIND_MAIL; /* Outlook PST / OST, Outlook Express DBX */
    }
    if (HAS(0, "\xDE\x12\x04\x95") || HAS(0, "\x95\x04\x12\xDE")) {
        return FILE_KIND_TRANSLATION; /* gettext .mo */
    }
    if (HAS(0, "OTTO") || HAS(0, "ttcf") || HAS(0, "wOFF") || HAS(0, "wOF2") ||
        ((HAS(0, "\0\1\0\0") || HAS(0, "true")) && by_name == FILE_KIND_FONT)) {
        return FILE_KIND_FONT;
    }
    if (HAS(0, "\x1F\x8B")) {
        /* gzip: a compressed manual page, image (svgz, above) or anything else */
        return by_name == FILE_KIND_MANPAGE ? FILE_KIND_MANPAGE : FILE_KIND_ARCHIVE;
    }
    if (HAS(257, "ustar") || HAS(0, "\xFD" "7zXZ") || HAS(0, "\x28\xB5\x2F\xFD") ||
        HAS(0, "7z\xBC\xAF\x27\x1C") || HAS(0, "Rar!\x1A\x07") || HAS(0, "LZIP") ||
        HAS(0, "!<arch>\n") || HAS(0, "\xED\xAB\xEE\xDB") || HAS(0, "MSCF\0\0\0\0") ||
        HAS(0, "\x04\x22\x4D\x18") || (HAS(0, "BZh") && len > 3 && head[3] >= '1' && head[3] <= '9')) {
        return FILE_KIND_ARCHIVE;
    }
    if (HAS(0, "fLaC") || HAS(0, "ID3") || HAS(0, "MThd") ||
        (HAS(0, "RIFF") && HAS(8, "WAVE")) ||
        (HAS(0, "FORM") && (HAS(8, "AIFF") || HAS(8, "AIFC"))) ||
        (HAS(4, "ftyp") && (HAS(8, "M4A ") || HAS(8, "M4B ") || HAS(8, "M4P ") || HAS(8, "F4A ")))) {
        return FILE_KIND_AUDIO;
    }
    if ((HAS(0, "RIFF") && HAS(8, "AVI ")) || HAS(0, "FLV\x01") ||
        HAS(0, "\0\0\x01\xBA") || HAS(0, "\0\0\x01\xB3")) {
        return FILE_KIND_VIDEO;
    }
    /* Containers that hold audio or video: trust the name if it says which */
    gboolean ogg = HAS(0, "OggS");
    if (ogg || HAS(0, "\x1A\x45\xDF\xA3") || HAS(4, "ftyp") ||
        HAS(0, "\x30\x26\xB2\x75\x8E\x66\xCF\x11")) {
        if (name_media_av) {
            return by_name;
        }
        if (ogg) {
            return (find_ci(head, len, "theora") != NULL || find_ci(head, len, "\x01video") != NULL)
                       ? FILE_KIND_VIDEO : FILE_KIND_AUDIO;
        }
        return FILE_KIND_VIDEO;
    }
    return FILE_KIND_TEXT; /* no match */
}

/* Weak signatures, only checked on content that is not text */
static FileKind detect_weak_magic(const guchar* head, gsize len, FileKind by_name) {
    /* DER certificate: an ASN.1 SEQUENCE, known by its name */
    if (by_name == FILE_KIND_CERTIFICATE && head[0] == 0x30) {
        return FILE_KIND_CERTIFICATE;
    }
    /* MPEG transport stream: a sync byte every 188 bytes */
    if (len > 376 && head[0] == 0x47 && head[188] == 0x47 && head[376] == 0x47) {
        return FILE_KIND_VIDEO;
    }
    /* MPEG audio frame or AAC ADTS header */
    if (len >= 2 && head[0] == 0xFF && (head[1] & 0xE0) == 0xE0) {
        return FILE_KIND_AUDIO;
    }
    if (HAS(0, "BM") && len >= 14) {
        return FILE_KIND_IMAGE;
    }
    return FILE_KIND_BINARY;
}

/* TRUE if the root element of an XML document is <svg>: skips the XML
 * declaration, comments and the doctype before the first element. */
static gboolean is_svg_document(const guchar* head, gsize len, gsize i) {
    while (i < len && head[i] == '<') {
        const gchar* close = NULL;
        if (HAS(i, "<?")) {
            close = "?>";
        } else if (HAS(i, "<!--")) {
            close = "-->";
        } else if (HAS(i, "<!")) {
            close = ">";
        } else {
            return HAS(i, "<svg") && i + 4 < len &&
                   (g_ascii_isspace(head[i + 4]) || head[i + 4] == '>');
        }
        const gchar* end = g_strstr_len((const gchar*)head + i, (gssize)(len - i), close);
        if (end == NULL) {
            return FALSE;
        }
        i = (gsize)((const guchar*)end - head) + strlen(close);
        while (i < len && g_ascii_isspace(head[i])) {
            i++;
        }
    }
    return FALSE;
}

/* Text images: SVG (any name), XPM / XBM / ASCII PNM (when the name says image) */
static gboolean is_text_image(const guchar* head, gsize len, gsize start, FileKind by_name) {
    if (is_svg_document(head, len, start)) {
        return TRUE;
    }
    if (by_name != FILE_KIND_IMAGE) {
        return FALSE;
    }
    return HAS(start, "/* XPM */") || HAS(start, "#define") ||
           (len >= start + 3 && head[start] == 'P' && head[start + 1] >= '1' &&
            head[start + 1] <= '3' && g_ascii_isspace(head[start + 2]));
}

/* Start of a line in the first len bytes with this prefix */
static gboolean has_line_prefix(const guchar* head, gsize len, const gchar* prefix) {
    gsize plen = strlen(prefix);
    for (gsize i = 0; i + plen <= len; i++) {
        if ((i == 0 || head[i - 1] == '\n') && memcmp(head + i, prefix, plen) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/* First non-blank byte after offset i, or 0 */
static guchar next_non_blank(const guchar* head, gsize len, gsize i) {
    while (i < len && g_ascii_isspace(head[i])) {
        i++;
    }
    return i < len ? head[i] : 0;
}

/* Text formats recognized by their content (start is the first non-blank byte) */
/* A mail message: header lines up to a blank line, with at least two
 * headers mail clients write (an emlx file starts with a byte count line) */
static gboolean looks_like_message(const guchar* head, gsize len, gsize start) {
    static const gchar* const KNOWN[] = {"from:", "to:", "subject:", "date:", "received:", "return-path:",
                                         "mime-version:", "message-id:", "delivered-to:", "x-mailer:", "reply-to:"};
    gsize p = start;
    gsize digits = 0;
    while (p + digits < len && g_ascii_isdigit(head[p + digits])) digits++;
    if (digits > 0 && p + digits < len && head[p + digits] == '\n') p += digits + 1;
    /* mbox: a "From " separator line first */
    if (len - p > 5 && memcmp(head + p, "From ", 5) == 0) {
        const guchar* nl = memchr(head + p, '\n', len - p);
        if (nl == NULL) return FALSE;
        p = (gsize)(nl - head) + 1;
    }
    gint known = 0, lines = 0;
    while (p < len) {
        const guchar* nl = memchr(head + p, '\n', len - p);
        gsize ll = nl != NULL ? (gsize)(nl - head) - p : len - p;
        if (ll > 0 && head[p + ll - 1] == '\r') ll--;
        if (ll == 0) break; /* end of the headers */
        if (head[p] != ' ' && head[p] != '\t') {
            const guchar* colon = memchr(head + p, ':', ll);
            if (colon == NULL || colon == head + p) return FALSE;
            for (const guchar* c = head + p; c < colon; c++) {
                if (*c <= ' ' || *c > '~') return FALSE;
            }
            gsize n = (gsize)(colon - head) - p + 1;
            for (gsize k = 0; k < G_N_ELEMENTS(KNOWN); k++) {
                if (n == strlen(KNOWN[k]) && g_ascii_strncasecmp((const gchar*)head + p, KNOWN[k], n) == 0) known++;
            }
            lines++;
        }
        if (nl == NULL) break;
        p = (gsize)(nl - head) + 1;
    }
    return known >= 2 && lines >= 2;
}

static FileKind detect_text_format(const gchar* name, const guchar* head, gsize len, gsize start,
                                   FileKind by_name) {
    /* A name that already says which text format wins over content guesses */
    if (by_name == FILE_KIND_MARKDOWN || by_name == FILE_KIND_ORG || by_name == FILE_KIND_HTML ||
        by_name == FILE_KIND_CSV || by_name == FILE_KIND_JSON || by_name == FILE_KIND_LOG) {
        return by_name;
    }
    if (HAS(start, "-----BEGIN ")) {
        return FILE_KIND_CERTIFICATE;
    }
    if (HAS(start, "{\\rtf")) {
        return FILE_KIND_RTF;
    }
    if (find_ci(head + start, MIN(len - start, 15), "BEGIN:VCALENDAR") == head + start) {
        return FILE_KIND_CALENDAR;
    }
    if (find_ci(head + start, MIN(len - start, 11), "BEGIN:VCARD") == head + start) {
        return FILE_KIND_CONTACT;
    }
    if (by_name == FILE_KIND_DESKTOP || HAS(start, "[Desktop Entry]")) {
        return HAS(start, "[") || HAS(start, "#") ? FILE_KIND_DESKTOP : FILE_KIND_TEXT;
    }
    if (by_name == FILE_KIND_NOTEBOOK && HAS(start, "{")) {
        return FILE_KIND_NOTEBOOK;
    }
    /* Manual page: roff requests (a manN name, or a .TH title line) */
    if ((by_name == FILE_KIND_MANPAGE && (HAS(start, ".") || HAS(start, "'\\\""))) ||
        has_line_prefix(head, len, ".TH ")) {
        return FILE_KIND_MANPAGE;
    }
    /* Unified diff, git diff / format-patch, svn diff */
    if ((HAS(start, "diff --git ") || HAS(start, "diff -") ||
         (HAS(start, "From ") && find_ci(head, MIN(len, 80), " Mon Sep 17 00:00:00 2001") != NULL) ||
         (HAS(start, "--- ") && has_line_prefix(head, len, "+++ ")) ||
         (HAS(start, "Index: ") && has_line_prefix(head, len, "====")))) {
        return FILE_KIND_DIFF;
    }
    if (by_name == FILE_KIND_MAIL || looks_like_message(head, len, start)) {
        return FILE_KIND_MAIL;
    }
    /* JSON without a JSON name: only for names without an extension */
    if (by_name == FILE_KIND_TEXT && file_extension(name)[0] == '\0') {
        guchar next = next_non_blank(head, len, start + 1);
        if ((HAS(start, "{") && (next == '"' || next == '}')) ||
            (HAS(start, "[") && (next == '{' || next == '[' || next == '"' || next == ']'))) {
            return FILE_KIND_JSON;
        }
    }
    return FILE_KIND_TEXT;
}

FileKind file_kind_detect(const gchar* name, const guchar* head, gsize len) {
    FileKind by_name = file_kind_from_name(name);
    if (head == NULL || len == 0) {
        /* Empty file: nothing to show but text */
        return file_kind_is_text(by_name) ? by_name : FILE_KIND_TEXT;
    }

    FileKind magic = detect_binary_magic(head, len, by_name);
    if (magic != FILE_KIND_TEXT) {
        return magic;
    }
    if (!looks_like_text(head, len)) {
        FileKind weak = detect_weak_magic(head, len, by_name);
        if (weak == FILE_KIND_BINARY && (file_kind_is_media(by_name) || by_name == FILE_KIND_ARCHIVE)) {
            return by_name; /* a format without a signature at its start (TGA, WMA, ISO...) */
        }
        return weak;
    }

    gsize start = skip_blank(head, len);
    if (is_text_image(head, len, start, by_name)) {
        return FILE_KIND_IMAGE;
    }
    if (HAS(start, "<") &&
        (find_ci(head + start, MIN(len - start, 16), "<!doctype html") == head + start ||
         find_ci(head + start, MIN(len - start, 6), "<html") == head + start ||
         (HAS(start, "<?xml") && find_ci(head, len, "<html") != NULL))) {
        return FILE_KIND_HTML;
    }
    FileKind text_kind = detect_text_format(name, head, len, start, by_name);
    if (text_kind != FILE_KIND_TEXT) {
        return text_kind;
    }
    /* Markdown, Org, HTML fragments and the other text formats below are only
     * known by their name */
    switch (by_name) {
    case FILE_KIND_MARKDOWN:
    case FILE_KIND_ORG:
    case FILE_KIND_HTML:
    case FILE_KIND_CSV:
    case FILE_KIND_JSON:
    case FILE_KIND_DIFF:
    case FILE_KIND_LOG:
    case FILE_KIND_TRANSLATION:
        return by_name;
    default:
        return FILE_KIND_TEXT;
    }
}
#undef HAS

FileKind file_kind_of_path(const gchar* path, const gchar* name) {
    if (path == NULL) {
        return file_kind_from_name(name);
    }
    FILE* fp = g_fopen(path, "rb");
    if (fp == NULL) {
        return file_kind_from_name(name);
    }
    guchar head[FILE_SNIFF_BYTES];
    gsize len = fread(head, 1, sizeof(head), fp);
    fclose(fp);
    return file_kind_detect(name, head, len);
}

