/* EXIF photo details (see exif.h). */
#include "exif.h"
#include "core/i18n.h"

#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const guchar* data;
    gsize len;
    gboolean big_endian;
} Tiff;

static guint16 rd16(const Tiff* t, gsize off) {
    if (off + 2 > t->len) {
        return 0;
    }
    const guchar* p = t->data + off;
    return t->big_endian ? (guint16)((p[0] << 8) | p[1]) : (guint16)(p[0] | (p[1] << 8));
}

static guint32 rd32(const Tiff* t, gsize off) {
    if (off + 4 > t->len) {
        return 0;
    }
    const guchar* p = t->data + off;
    return t->big_endian ? ((guint32)p[0] << 24) | ((guint32)p[1] << 16) | ((guint32)p[2] << 8) | p[3]
                         : (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static const guint TYPE_SIZE[] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };

/* Offset of the value of an IFD entry (inline when it fits in 4 bytes) */
static gboolean value_offset(const Tiff* t, gsize entry, gsize* off, guint* type, guint32* count) {
    *type = rd16(t, entry + 2);
    *count = rd32(t, entry + 4);
    if (*type == 0 || *type >= G_N_ELEMENTS(TYPE_SIZE)) {
        return FALSE;
    }
    guint64 size = (guint64)TYPE_SIZE[*type] * *count;
    if (size > t->len) {
        return FALSE;
    }
    *off = size <= 4 ? entry + 8 : rd32(t, entry + 8);
    return *off + size <= t->len;
}

static gchar* ascii_value(const Tiff* t, gsize entry) {
    gsize off;
    guint type;
    guint32 count;
    if (!value_offset(t, entry, &off, &type, &count) || (type != 2 && type != 7) || count == 0) {
        return NULL;
    }
    gsize n = strnlen((const gchar*)t->data + off, count);
    gchar* s = g_utf8_make_valid((const gchar*)t->data + off, (gssize)n);
    g_strstrip(s);
    if (s[0] == '\0') {
        g_free(s);
        return NULL;
    }
    return s;
}

static gdouble number_value(const Tiff* t, gsize entry, guint index) {
    gsize off;
    guint type;
    guint32 count;
    if (!value_offset(t, entry, &off, &type, &count) || index >= count) {
        return 0;
    }
    switch (type) {
    case 1: case 7: return t->data[off + index];
    case 3: return rd16(t, off + index * 2);
    case 4: return rd32(t, off + index * 4);
    case 9: return (gint32)rd32(t, off + index * 4);
    case 5: {
        guint32 den = rd32(t, off + index * 8 + 4);
        return den != 0 ? (gdouble)rd32(t, off + index * 8) / den : 0;
    }
    case 10: {
        gint32 den = (gint32)rd32(t, off + index * 8 + 4);
        return den != 0 ? (gdouble)(gint32)rd32(t, off + index * 8) / den : 0;
    }
    default: return 0;
    }
}

/* Degrees from a GPS (degrees, minutes, seconds) rational triple */
static gdouble gps_degrees(const Tiff* t, gsize entry) {
    return number_value(t, entry, 0) + number_value(t, entry, 1) / 60.0 + number_value(t, entry, 2) / 3600.0;
}

#define TAG_MAKE 0x010F
#define TAG_MODEL 0x0110
#define TAG_ORIENTATION 0x0112
#define TAG_SOFTWARE 0x0131
#define TAG_DATETIME 0x0132
#define TAG_ARTIST 0x013B
#define TAG_COPYRIGHT 0x8298
#define TAG_EXIF_IFD 0x8769
#define TAG_GPS_IFD 0x8825
#define TAG_EXPOSURE 0x829A
#define TAG_FNUMBER 0x829D
#define TAG_ISO 0x8827
#define TAG_DATE_ORIGINAL 0x9003
#define TAG_FLASH 0x9209
#define TAG_FOCAL 0x920A
#define TAG_FOCAL_35 0xA405
#define TAG_LENS 0xA434

typedef enum { IFD_MAIN, IFD_EXIF, IFD_GPS } IfdKind;

static void read_ifd(ExifInfo* info, const Tiff* t, gsize off, IfdKind kind, guint depth) {
    if (depth > 3 || off < 8 || off + 2 > t->len) {
        return;
    }
    guint n = rd16(t, off);
    gchar lat_ref = 'N', lon_ref = 'E';
    gsize lat_entry = 0, lon_entry = 0;
    guint alt_ref = 0;
    gsize alt_entry = 0;
    for (guint i = 0; i < n && i < 512; i++) {
        gsize e = off + 2 + (gsize)i * 12;
        if (e + 12 > t->len) {
            break;
        }
        guint tag = rd16(t, e);
        if (kind == IFD_GPS) {
            gchar* ref;
            switch (tag) {
            case 1: ref = ascii_value(t, e); lat_ref = ref != NULL ? ref[0] : 'N'; g_free(ref); break;
            case 2: lat_entry = e; break;
            case 3: ref = ascii_value(t, e); lon_ref = ref != NULL ? ref[0] : 'E'; g_free(ref); break;
            case 4: lon_entry = e; break;
            case 5: alt_ref = (guint)number_value(t, e, 0); break;
            case 6: alt_entry = e; break;
            default: break;
            }
            continue;
        }
        switch (tag) {
        case TAG_MAKE: g_free(info->make); info->make = ascii_value(t, e); break;
        case TAG_MODEL: g_free(info->model); info->model = ascii_value(t, e); break;
        case TAG_SOFTWARE: g_free(info->software); info->software = ascii_value(t, e); break;
        case TAG_ARTIST: g_free(info->artist); info->artist = ascii_value(t, e); break;
        case TAG_COPYRIGHT: g_free(info->copyright); info->copyright = ascii_value(t, e); break;
        case TAG_ORIENTATION: info->orientation = (gint)number_value(t, e, 0); break;
        case TAG_DATETIME:
            if (info->date_taken == NULL) {
                info->date_taken = ascii_value(t, e);
            }
            break;
        case TAG_DATE_ORIGINAL: g_free(info->date_taken); info->date_taken = ascii_value(t, e); break;
        case TAG_EXPOSURE: info->exposure_time = number_value(t, e, 0); break;
        case TAG_FNUMBER: info->f_number = number_value(t, e, 0); break;
        case TAG_ISO: info->iso = (gint)number_value(t, e, 0); break;
        case TAG_FLASH: info->flash = (gint)number_value(t, e, 0); break;
        case TAG_FOCAL: info->focal_length = number_value(t, e, 0); break;
        case TAG_FOCAL_35: info->focal_length_35mm = (gint)number_value(t, e, 0); break;
        case TAG_LENS: g_free(info->lens); info->lens = ascii_value(t, e); break;
        case TAG_EXIF_IFD: read_ifd(info, t, rd32(t, e + 8), IFD_EXIF, depth + 1); break;
        case TAG_GPS_IFD: read_ifd(info, t, rd32(t, e + 8), IFD_GPS, depth + 1); break;
        default: break;
        }
    }
    if (kind == IFD_GPS && lat_entry != 0 && lon_entry != 0) {
        info->has_gps = TRUE;
        info->latitude = gps_degrees(t, lat_entry) * (lat_ref == 'S' ? -1 : 1);
        info->longitude = gps_degrees(t, lon_entry) * (lon_ref == 'W' ? -1 : 1);
        if (alt_entry != 0) {
            info->has_altitude = TRUE;
            info->altitude = number_value(t, alt_entry, 0) * (alt_ref == 1 ? -1 : 1);
        }
    }
}

ExifInfo* exif_parse_tiff(const guchar* data, gsize len) {
    if (len < 8) {
        return NULL;
    }
    Tiff t = { data, len, FALSE };
    if (memcmp(data, "II*\0", 4) == 0) {
        t.big_endian = FALSE;
    } else if (memcmp(data, "MM\0*", 4) == 0) {
        t.big_endian = TRUE;
    } else if (memcmp(data, "IIRO", 4) == 0 || memcmp(data, "IIU\0", 4) == 0) {
        t.big_endian = FALSE; /* Olympus ORF, Panasonic RW2 */
    } else {
        return NULL;
    }
    ExifInfo* info = g_new0(ExifInfo, 1);
    info->flash = -1;
    read_ifd(info, &t, rd32(&t, 4), IFD_MAIN, 0);
    return info;
}

static ExifInfo* parse_jpeg(const guchar* data, gsize len) {
    gsize pos = 2;
    while (pos + 4 <= len) {
        if (data[pos] != 0xFF) {
            return NULL;
        }
        guchar marker = data[pos + 1];
        if (marker == 0xFF) {
            pos++;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) {
            return NULL; /* image data: no EXIF before it */
        }
        gsize seg = ((gsize)data[pos + 2] << 8) | data[pos + 3];
        if (seg < 2) {
            return NULL;
        }
        if (marker == 0xE1 && seg >= 8 && pos + 2 + seg <= len && memcmp(data + pos + 4, "Exif\0\0", 6) == 0) {
            return exif_parse_tiff(data + pos + 10, seg - 8);
        }
        pos += 2 + seg;
    }
    return NULL;
}

static ExifInfo* parse_png(const guchar* data, gsize len) {
    gsize pos = 8;
    while (pos + 12 <= len) {
        guint32 n = ((guint32)data[pos] << 24) | ((guint32)data[pos + 1] << 16) |
                    ((guint32)data[pos + 2] << 8) | data[pos + 3];
        if (memcmp(data + pos + 4, "eXIf", 4) == 0 && pos + 8 + n <= len) {
            return exif_parse_tiff(data + pos + 8, n);
        }
        if (memcmp(data + pos + 4, "IDAT", 4) == 0 || (guint64)pos + 12 + n > len) {
            return NULL;
        }
        pos += 12 + n;
    }
    return NULL;
}

static ExifInfo* parse_webp(const guchar* data, gsize len) {
    gsize pos = 12;
    while (pos + 8 <= len) {
        guint32 n = (guint32)data[pos + 4] | ((guint32)data[pos + 5] << 8) |
                    ((guint32)data[pos + 6] << 16) | ((guint32)data[pos + 7] << 24);
        if (memcmp(data + pos, "EXIF", 4) == 0 && pos + 8 + n <= len) {
            const guchar* exif = data + pos + 8;
            gsize elen = n;
            if (elen > 6 && memcmp(exif, "Exif\0\0", 6) == 0) {
                exif += 6;
                elen -= 6;
            }
            return exif_parse_tiff(exif, elen);
        }
        pos += 8 + n + (n & 1);
    }
    return NULL;
}

ExifInfo* exif_parse(const guchar* data, gsize len) {
    if (len >= 4 && data[0] == 0xFF && data[1] == 0xD8) {
        return parse_jpeg(data, len);
    }
    if (len >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        return parse_png(data, len);
    }
    if (len >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) {
        return parse_webp(data, len);
    }
    return exif_parse_tiff(data, len);
}

ExifInfo* exif_read_file(const gchar* path) {
    FILE* fp = g_fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    guchar* data = g_malloc(EXIF_READ_BYTES);
    gsize len = fread(data, 1, EXIF_READ_BYTES, fp);
    fclose(fp);
    ExifInfo* info = exif_parse(data, len);
    g_free(data);
    return info;
}

/* JPEG previews of an IFD and of the IFDs it points to (SubIFDs, next IFD) */
static void find_previews(const Tiff* t, gsize off, guint depth, gsize* best_off, gsize* best_len) {
    for (guint chain = 0; off >= 8 && off + 2 <= t->len && depth < 4 && chain < 8; chain++) {
        guint n = rd16(t, off);
        gsize jpeg_off = 0, jpeg_len = 0, strip_off = 0, strip_len = 0;
        guint compression = 0;
        for (guint i = 0; i < n && i < 512; i++) {
            gsize e = off + 2 + (gsize)i * 12;
            if (e + 12 > t->len) {
                break;
            }
            guint tag = rd16(t, e);
            switch (tag) {
            case 0x0201: jpeg_off = (gsize)number_value(t, e, 0); break;
            case 0x0202: jpeg_len = (gsize)number_value(t, e, 0); break;
            case 0x0103: compression = (guint)number_value(t, e, 0); break;
            case 0x0111: strip_off = (gsize)number_value(t, e, 0); break;
            case 0x0117: strip_len = (gsize)number_value(t, e, 0); break;
            case 0x014A: {
                gsize voff;
                guint type;
                guint32 count;
                if (value_offset(t, e, &voff, &type, &count)) {
                    for (guint32 k = 0; k < count && k < 8; k++) {
                        find_previews(t, (gsize)number_value(t, e, k), depth + 1, best_off, best_len);
                    }
                }
                break;
            }
            default:
                break;
            }
        }
        if ((compression == 6 || compression == 7) && strip_len > 0 && jpeg_len == 0) {
            jpeg_off = strip_off;
            jpeg_len = strip_len;
        }
        if (jpeg_len > *best_len && jpeg_off + jpeg_len <= t->len && jpeg_off + 2 <= t->len &&
            t->data[jpeg_off] == 0xFF && t->data[jpeg_off + 1] == 0xD8) {
            *best_off = jpeg_off;
            *best_len = jpeg_len;
        }
        gsize next_pos = off + 2 + (gsize)n * 12;
        off = next_pos + 4 <= t->len ? rd32(t, next_pos) : 0;
    }
}

gboolean exif_find_preview(const guchar* data, gsize len, gsize* offset, gsize* length) {
    /* Fujifilm RAF: the JPEG offset and length are in the header */
    if (len >= 92 && memcmp(data, "FUJIFILMCCD-RAW", 15) == 0) {
        guint32 off = ((guint32)data[84] << 24) | ((guint32)data[85] << 16) | ((guint32)data[86] << 8) | data[87];
        guint32 n = ((guint32)data[88] << 24) | ((guint32)data[89] << 16) | ((guint32)data[90] << 8) | data[91];
        if ((guint64)off + n <= len && n > 2 && data[off] == 0xFF && data[off + 1] == 0xD8) {
            *offset = off;
            *length = n;
            return TRUE;
        }
        return FALSE;
    }
    if (len < 8) {
        return FALSE;
    }
    Tiff t = { data, len, FALSE };
    if (memcmp(data, "MM\0*", 4) == 0 || memcmp(data, "MM\0\x2A", 4) == 0) {
        t.big_endian = TRUE;
    } else if (memcmp(data, "II*\0", 4) != 0 && memcmp(data, "IIRO", 4) != 0 && memcmp(data, "IIU\0", 4) != 0) {
        return FALSE; /* TIFF, Olympus ORF, Panasonic RW2 headers */
    }
    gsize best_off = 0, best_len = 0;
    find_previews(&t, rd32(&t, 4), 0, &best_off, &best_len);
    if (best_len == 0) {
        return FALSE;
    }
    *offset = best_off;
    *length = best_len;
    return TRUE;
}

void exif_info_free(ExifInfo* info) {
    if (info == NULL) {
        return;
    }
    g_free(info->make);
    g_free(info->model);
    g_free(info->lens);
    g_free(info->software);
    g_free(info->artist);
    g_free(info->copyright);
    g_free(info->date_taken);
    g_free(info);
}

/* "1/250 s", "2 s", "0.5 s" */
static gchar* format_exposure(gdouble t) {
    if (t >= 1.0 || t <= 0) {
        return g_strdup_printf("%g s", t);
    }
    return g_strdup_printf("1/%.0f s", 1.0 / t);
}

gchar** exif_summary_lines(const ExifInfo* info) {
    GPtrArray* lines = g_ptr_array_new();
    /* Camera: the model usually repeats the make ("Canon" + "Canon EOS R5") */
    GString* camera = g_string_new(NULL);
    if (info->model != NULL) {
        if (info->make != NULL && g_ascii_strncasecmp(info->model, info->make, strlen(info->make)) != 0) {
            g_string_append_printf(camera, "%s ", info->make);
        }
        g_string_append(camera, info->model);
    } else if (info->make != NULL) {
        g_string_append(camera, info->make);
    }
    if (info->lens != NULL) {
        g_string_append_printf(camera, "%s%s", camera->len > 0 ? " · " : "", info->lens);
    }
    if (camera->len > 0) {
        g_ptr_array_add(lines, g_string_free(camera, FALSE));
    } else {
        g_string_free(camera, TRUE);
    }

    GString* exposure = g_string_new(NULL);
    if (info->exposure_time > 0) {
        gchar* t = format_exposure(info->exposure_time);
        g_string_append(exposure, t);
        g_free(t);
    }
    if (info->f_number > 0) {
        g_string_append_printf(exposure, "%sf/%g", exposure->len > 0 ? " · " : "",
                               round(info->f_number * 10) / 10);
    }
    if (info->iso > 0) {
        g_string_append_printf(exposure, "%sISO %d", exposure->len > 0 ? " · " : "", info->iso);
    }
    if (info->focal_length > 0) {
        g_string_append_printf(exposure, "%s%g mm", exposure->len > 0 ? " · " : "",
                               round(info->focal_length * 10) / 10);
        if (info->focal_length_35mm > 0 && fabs(info->focal_length_35mm - info->focal_length) > 0.5) {
            g_string_append_printf(exposure, " (%d mm equiv.)", info->focal_length_35mm);
        }
    }
    if (info->flash > 0 && (info->flash & 1)) {
        g_string_append_printf(exposure, "%s%s", exposure->len > 0 ? " · " : "", _("flash"));
    }
    if (exposure->len > 0) {
        g_ptr_array_add(lines, g_string_free(exposure, FALSE));
    } else {
        g_string_free(exposure, TRUE);
    }

    if (info->date_taken != NULL) {
        gint y, mo, d, h, mi;
        if (sscanf(info->date_taken, "%d:%d:%d %d:%d", &y, &mo, &d, &h, &mi) == 5) {
            g_ptr_array_add(lines, g_strdup_printf(_("Taken %1$04d-%2$02d-%3$02d %4$02d:%5$02d"), y, mo, d, h, mi));
        } else {
            g_ptr_array_add(lines, g_strdup_printf(_("Taken %s"), info->date_taken));
        }
    }
    if (info->has_gps) {
        gchar* place = g_strdup_printf("%.5f° %c, %.5f° %c", fabs(info->latitude), info->latitude < 0 ? 'S' : 'N',
                                       fabs(info->longitude), info->longitude < 0 ? 'W' : 'E');
        if (info->has_altitude) {
            gchar* with_alt = g_strdup_printf("%s · %.0f m", place, info->altitude);
            g_free(place);
            place = with_alt;
        }
        g_ptr_array_add(lines, place);
    }
    GString* other = g_string_new(NULL);
    if (info->software != NULL) {
        g_string_append_printf(other, _("Edited with %s"), info->software);
    }
    if (info->artist != NULL) {
        if (other->len > 0) g_string_append(other, " · ");
        g_string_append_printf(other, _("By %s"), info->artist);
    }
    if (info->copyright != NULL) {
        g_string_append_printf(other, "%s%s%s", other->len > 0 ? " · " : "",
                               g_str_has_prefix(info->copyright, "©") ? "" : "© ", info->copyright);
    }
    if (other->len > 0) {
        g_ptr_array_add(lines, g_string_free(other, FALSE));
    } else {
        g_string_free(other, TRUE);
    }
    g_ptr_array_add(lines, NULL);
    return (gchar**)g_ptr_array_free(lines, FALSE);
}
