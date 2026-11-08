/* Audio and video tags (see media_tags.h). */
#include "media/media_tags.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

/* Bytes read at the start of a file (ID3v2 tags with covers can be larger:
 * their size is read from the header) */
#define HEAD_BYTES (256 * 1024)
#define MAX_TAG_BYTES (16 * 1024 * 1024)
#define MAX_MOOV (16 * 1024 * 1024)

static guint32 be32(const guchar* p) {
    return ((guint32)p[0] << 24) | ((guint32)p[1] << 16) | ((guint32)p[2] << 8) | p[3];
}

static guint32 le32(const guchar* p) {
    return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24);
}

static guint64 be64(const guchar* p) {
    return ((guint64)be32(p) << 32) | be32(p + 4);
}

/* First occurrence of needle in data (no memmem: not portable) */
static const guchar* find_bytes(const guchar* data, gsize len, const void* needle, gsize nlen) {
    for (gsize i = 0; i + nlen <= len; i++) {
        if (memcmp(data + i, needle, nlen) == 0) {
            return data + i;
        }
    }
    return NULL;
}

static void set_field(gchar** field, gchar* value) {
    if (value != NULL) {
        g_strstrip(value);
    }
    if (value == NULL || value[0] == '\0' || *field != NULL) {
        g_free(value);
        return;
    }
    *field = value;
}

static void set_cover(MediaTags* t, const guchar* data, gsize len, gboolean front) {
    if (len == 0 || len > MEDIA_TAGS_MAX_COVER || (t->cover != NULL && !front)) {
        return;
    }
    if (t->cover != NULL) {
        g_bytes_unref(t->cover);
    }
    t->cover = g_bytes_new(data, len);
}

/* --- Vorbis comments (FLAC, Ogg) --- */

static void vorbis_comment(MediaTags* t, const gchar* key, const gchar* value, gsize len) {
    gchar* v = g_utf8_make_valid(value, (gssize)len);
    if (g_ascii_strcasecmp(key, "TITLE") == 0) set_field(&t->title, v);
    else if (g_ascii_strcasecmp(key, "ARTIST") == 0) set_field(&t->artist, v);
    else if (g_ascii_strcasecmp(key, "ALBUM") == 0) set_field(&t->album, v);
    else if (g_ascii_strcasecmp(key, "ALBUMARTIST") == 0) set_field(&t->album_artist, v);
    else if (g_ascii_strcasecmp(key, "DATE") == 0 || g_ascii_strcasecmp(key, "YEAR") == 0) set_field(&t->year, v);
    else if (g_ascii_strcasecmp(key, "TRACKNUMBER") == 0) set_field(&t->track, v);
    else if (g_ascii_strcasecmp(key, "GENRE") == 0) set_field(&t->genre, v);
    else if (g_ascii_strcasecmp(key, "COMMENT") == 0 || g_ascii_strcasecmp(key, "DESCRIPTION") == 0) set_field(&t->comment, v);
    else g_free(v);
}

static void parse_flac_picture(MediaTags* t, const guchar* p, gsize len);

/* Vorbis comment block: vendor, then "KEY=value" entries (little-endian sizes) */
static void parse_vorbis_comments(MediaTags* t, const guchar* p, gsize len) {
    if (len < 8) {
        return;
    }
    gsize pos = 4 + le32(p);
    if (pos + 4 > len) {
        return;
    }
    guint32 n = le32(p + pos);
    pos += 4;
    for (guint32 i = 0; i < n && pos + 4 <= len; i++) {
        guint32 clen = le32(p + pos);
        pos += 4;
        if (clen > len - pos) {
            break;
        }
        const gchar* c = (const gchar*)p + pos;
        const gchar* eq = memchr(c, '=', clen);
        if (eq != NULL) {
            gchar* key = g_strndup(c, (gsize)(eq - c));
            if (g_ascii_strcasecmp(key, "METADATA_BLOCK_PICTURE") == 0) {
                gsize plen = 0;
                gchar* b64 = g_strndup(eq + 1, clen - (gsize)(eq - c) - 1);
                guchar* pic = g_base64_decode(b64, &plen);
                parse_flac_picture(t, pic, plen);
                g_free(pic);
                g_free(b64);
            } else {
                vorbis_comment(t, key, eq + 1, clen - (gsize)(eq - c) - 1);
            }
            g_free(key);
        }
        pos += clen;
    }
}

/* FLAC PICTURE block (also base64 in Vorbis comments) */
static void parse_flac_picture(MediaTags* t, const guchar* p, gsize len) {
    if (len < 32) {
        return;
    }
    guint32 type = be32(p);
    gsize pos = 4;
    guint32 mime_len = be32(p + pos);
    pos += 4 + mime_len;
    if (pos + 4 > len) {
        return;
    }
    guint32 desc_len = be32(p + pos);
    pos += 4 + desc_len + 16;
    if (pos + 4 > len) {
        return;
    }
    guint32 data_len = be32(p + pos);
    pos += 4;
    if (data_len <= len - pos) {
        set_cover(t, p + pos, data_len, type == 3);
    }
}

static void parse_flac(MediaTags* t, const guchar* d, gsize len) {
    t->codec = g_strdup("FLAC");
    gsize pos = 4;
    while (pos + 4 <= len) {
        guchar header = d[pos];
        guint32 blen = ((guint32)d[pos + 1] << 16) | ((guint32)d[pos + 2] << 8) | d[pos + 3];
        pos += 4;
        if (blen > len - pos) {
            break;
        }
        const guchar* b = d + pos;
        switch (header & 0x7F) {
        case 0: /* STREAMINFO */
            if (blen >= 18) {
                t->sample_rate = ((guint)b[10] << 12) | ((guint)b[11] << 4) | (b[12] >> 4);
                t->channels = ((b[12] >> 1) & 0x07) + 1;
                t->bits_per_sample = (((guint)(b[12] & 1) << 4) | (b[13] >> 4)) + 1;
                guint64 samples = ((guint64)(b[13] & 0x0F) << 32) | be32(b + 14);
                if (t->sample_rate > 0) {
                    t->duration = (gdouble)samples / t->sample_rate;
                }
            }
            break;
        case 4:
            parse_vorbis_comments(t, b, blen);
            break;
        case 6:
            parse_flac_picture(t, b, blen);
            break;
        default:
            break;
        }
        pos += blen;
        if (header & 0x80) {
            break; /* last block */
        }
    }
}

/* --- Ogg --- */

/* Concatenated payload of the first Ogg pages (headers of the stream) */
static GByteArray* ogg_packets(const guchar* d, gsize len, guint64* last_granule) {
    GByteArray* out = g_byte_array_new();
    gsize pos = 0;
    *last_granule = 0;
    while (pos + 27 <= len && memcmp(d + pos, "OggS", 4) == 0) {
        guint segments = d[pos + 26];
        if (pos + 27 + segments > len) {
            break;
        }
        gsize body = 0;
        for (guint i = 0; i < segments; i++) {
            body += d[pos + 27 + i];
        }
        gsize start = pos + 27 + segments;
        if (start + body > len) {
            break;
        }
        g_byte_array_append(out, d + start, (guint)body);
        pos = start + body;
        if (out->len > 4 * 1024 * 1024) {
            break;
        }
    }
    return out;
}

static void parse_ogg(MediaTags* t, const guchar* d, gsize len, const gchar* path, gint64 size) {
    guint64 unused;
    GByteArray* packets = ogg_packets(d, len, &unused);
    const guchar* p = packets->data;
    gsize n = packets->len;
    gboolean opus = n >= 19 && memcmp(p, "OpusHead", 8) == 0;
    gboolean vorbis = n >= 30 && memcmp(p, "\x01vorbis", 7) == 0;
    if (opus) {
        t->codec = g_strdup("Opus");
        t->channels = p[9];
        t->sample_rate = 48000;
        const guchar* tags = find_bytes(p, n, "OpusTags", 8);
        if (tags != NULL) {
            parse_vorbis_comments(t, tags + 8, n - (gsize)(tags + 8 - p));
        }
    } else if (vorbis) {
        t->codec = g_strdup("Vorbis");
        t->channels = p[11];
        t->sample_rate = le32(p + 12);
        guint32 nominal = le32(p + 20);
        if (nominal > 0 && nominal < 10000000) {
            t->bitrate = nominal / 1000;
        }
        const guchar* tags = find_bytes(p, n, "\x03vorbis", 7);
        if (tags != NULL) {
            parse_vorbis_comments(t, tags + 7, n - (gsize)(tags + 7 - p));
        }
    } else if (find_bytes(p, MIN(n, 64), "theora", 6) != NULL) {
        t->codec = g_strdup("Theora");
    }
    g_byte_array_unref(packets);
    /* Duration: granule position of the last page / sample rate */
    if ((opus || vorbis) && size > 0) {
        FILE* fp = g_fopen(path, "rb");
        if (fp != NULL) {
            gsize tail = (gsize)MIN(size, 65536);
            guchar* buf = g_malloc(tail);
            if (fseeko(fp, size - (gint64)tail, SEEK_SET) == 0 && fread(buf, 1, tail, fp) == tail) {
                for (gsize i = tail - 27; i > 0; i--) {
                    if (memcmp(buf + i, "OggS", 4) == 0) {
                        guint64 granule = (guint64)le32(buf + i + 6) | ((guint64)le32(buf + i + 10) << 32);
                        guint rate = opus ? 48000 : t->sample_rate;
                        if (rate > 0 && granule < G_MAXINT64) {
                            t->duration = (gdouble)granule / rate;
                        }
                        break;
                    }
                }
            }
            g_free(buf);
            fclose(fp);
        }
    }
}

/* --- ID3 --- */

static guint32 syncsafe(const guchar* p) {
    return ((guint32)(p[0] & 0x7F) << 21) | ((guint32)(p[1] & 0x7F) << 14) | ((guint32)(p[2] & 0x7F) << 7) |
           (p[3] & 0x7F);
}

/* Text of an ID3 text frame (encoding byte first) */
static gchar* id3_text(const guchar* p, gsize len) {
    if (len < 1) {
        return NULL;
    }
    guchar enc = p[0];
    const gchar* s = (const gchar*)p + 1;
    gsize n = len - 1;
    gchar* out = NULL;
    if (enc == 1 || enc == 2) {
        out = g_convert(s, (gssize)n, "UTF-8", enc == 1 ? "UTF-16" : "UTF-16BE", NULL, NULL, NULL);
    } else if (enc == 3) {
        out = g_utf8_make_valid(s, (gssize)n);
    } else {
        out = g_convert(s, (gssize)n, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
    }
    if (out != NULL) {
        /* Several values are separated by NULs: keep the first */
        out[strlen(out)] = '\0';
    }
    return out;
}

/* Skip a NUL-terminated string in an ID3 encoding; returns the offset after it */
static gsize skip_id3_string(const guchar* p, gsize len, gsize pos, guchar enc) {
    if (enc == 1 || enc == 2) {
        while (pos + 1 < len && (p[pos] != 0 || p[pos + 1] != 0)) {
            pos += 2;
        }
        return pos + 2;
    }
    while (pos < len && p[pos] != 0) {
        pos++;
    }
    return pos + 1;
}

static void id3_picture(MediaTags* t, const guchar* p, gsize len, gboolean v22) {
    if (len < 4) {
        return;
    }
    guchar enc = p[0];
    gsize pos;
    if (v22) {
        pos = 4; /* 3-letter image format */
    } else {
        pos = 1;
        while (pos < len && p[pos] != 0) {
            pos++; /* MIME type */
        }
        pos++;
    }
    if (pos >= len) {
        return;
    }
    guchar type = p[pos++];
    pos = skip_id3_string(p, len, pos, enc);
    if (pos < len) {
        set_cover(t, p + pos, len - pos, type == 3);
    }
}

static void parse_id3v2(MediaTags* t, const guchar* d, gsize len) {
    guint version = d[3];
    guchar flags = d[5];
    gsize size = syncsafe(d + 6);
    gsize end = MIN(len, 10 + size);
    gsize pos = 10;
    if (flags & 0x40 && version >= 3) {
        gsize ext = version == 4 ? syncsafe(d + 10) : be32(d + 10) + 4;
        pos += ext;
    }
    gboolean v22 = version == 2;
    gsize header = v22 ? 6 : 10;
    while (pos + header <= end) {
        if (d[pos] == 0) {
            break; /* padding */
        }
        gchar id[5] = { 0 };
        memcpy(id, d + pos, v22 ? 3 : 4);
        gsize fsize = v22 ? ((gsize)d[pos + 3] << 16) | ((gsize)d[pos + 4] << 8) | d[pos + 5]
                     : version == 4 ? syncsafe(d + pos + 4) : be32(d + pos + 4);
        pos += header;
        if (fsize > end - pos) {
            break;
        }
        const guchar* f = d + pos;
        if (strcmp(id, "TIT2") == 0 || strcmp(id, "TT2") == 0) set_field(&t->title, id3_text(f, fsize));
        else if (strcmp(id, "TPE1") == 0 || strcmp(id, "TP1") == 0) set_field(&t->artist, id3_text(f, fsize));
        else if (strcmp(id, "TALB") == 0 || strcmp(id, "TAL") == 0) set_field(&t->album, id3_text(f, fsize));
        else if (strcmp(id, "TPE2") == 0 || strcmp(id, "TP2") == 0) set_field(&t->album_artist, id3_text(f, fsize));
        else if (strcmp(id, "TDRC") == 0 || strcmp(id, "TYER") == 0 || strcmp(id, "TYE") == 0) set_field(&t->year, id3_text(f, fsize));
        else if (strcmp(id, "TRCK") == 0 || strcmp(id, "TRK") == 0) set_field(&t->track, id3_text(f, fsize));
        else if (strcmp(id, "TCON") == 0 || strcmp(id, "TCO") == 0) set_field(&t->genre, id3_text(f, fsize));
        else if (strcmp(id, "APIC") == 0 || strcmp(id, "PIC") == 0) id3_picture(t, f, fsize, v22);
        pos += fsize;
    }
}

/* ID3v1 at the end of the file: 128 bytes "TAG" title artist album year */
static void parse_id3v1(MediaTags* t, const guchar* tag) {
    if (memcmp(tag, "TAG", 3) != 0) {
        return;
    }
    set_field(&t->title, g_convert((const gchar*)tag + 3, (gssize)strnlen((const gchar*)tag + 3, 30), "UTF-8", "ISO-8859-1", NULL, NULL, NULL));
    set_field(&t->artist, g_convert((const gchar*)tag + 33, (gssize)strnlen((const gchar*)tag + 33, 30), "UTF-8", "ISO-8859-1", NULL, NULL, NULL));
    set_field(&t->album, g_convert((const gchar*)tag + 63, (gssize)strnlen((const gchar*)tag + 63, 30), "UTF-8", "ISO-8859-1", NULL, NULL, NULL));
    set_field(&t->year, g_strndup((const gchar*)tag + 93, 4));
    if (tag[125] == 0 && tag[126] != 0) {
        set_field(&t->track, g_strdup_printf("%u", tag[126]));
    }
}

/* First MPEG audio frame after the tag: bitrate, rate, channels, duration */
static void parse_mpeg_audio(MediaTags* t, const guchar* d, gsize len, gsize start, gint64 size) {
    static const guint BITRATES_V1_L3[] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
    static const guint BITRATES_V2_L3[] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
    static const guint RATES[] = { 44100, 48000, 32000 };
    for (gsize i = start; i + 4 <= len && i < start + 65536; i++) {
        if (d[i] != 0xFF || (d[i + 1] & 0xE0) != 0xE0) {
            continue;
        }
        guint version = (d[i + 1] >> 3) & 3;     /* 3: MPEG-1, 2: MPEG-2, 0: MPEG-2.5 */
        guint layer = (d[i + 1] >> 1) & 3;       /* 1: layer III */
        guint br_index = d[i + 2] >> 4;
        guint sr_index = (d[i + 2] >> 2) & 3;
        if (version == 1 || layer == 0 || br_index == 0 || br_index == 15 || sr_index == 3) {
            continue;
        }
        guint bitrate = version == 3 ? BITRATES_V1_L3[br_index] : BITRATES_V2_L3[br_index];
        guint rate = RATES[sr_index] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
        guint channels = (d[i + 3] >> 6) == 3 ? 1 : 2;
        t->codec = g_strdup(layer == 1 ? "MP3" : layer == 2 ? "MP2" : "MP1");
        t->sample_rate = rate;
        t->channels = channels;
        t->bitrate = bitrate;
        /* Xing / Info header: exact frame count (variable bitrate) */
        gsize side = version == 3 ? (channels == 1 ? 17 : 32) : (channels == 1 ? 9 : 17);
        gsize x = i + 4 + side;
        if (x + 12 <= len && (memcmp(d + x, "Xing", 4) == 0 || memcmp(d + x, "Info", 4) == 0) && (d[x + 7] & 1)) {
            guint32 frames = be32(d + x + 8);
            guint samples_per_frame = version == 3 ? 1152 : 576;
            t->duration = (gdouble)frames * samples_per_frame / rate;
            if (t->duration > 0 && size > 0) {
                t->bitrate = (guint)((gdouble)(size - (gint64)start) * 8 / t->duration / 1000);
            }
        } else if (bitrate > 0 && size > 0) {
            t->duration = (gdouble)(size - (gint64)start) * 8 / (bitrate * 1000.0);
        }
        return;
    }
}

/* --- MP4 --- */

static void mp4_ilst(MediaTags* t, const guchar* p, gsize len) {
    gsize pos = 0;
    while (pos + 8 <= len) {
        guint32 size = be32(p + pos);
        if (size < 8 || size > len - pos) {
            break;
        }
        const guchar* type = p + pos + 4;
        /* data atom inside: size, "data", type(4), locale(4), value */
        const guchar* data = find_bytes(p + pos + 8, size - 8, "data", 4);
        if (data != NULL && data >= p + pos + 12) {
            guint32 dsize = be32(data - 4);
            const guchar* value = data + 12;
            gsize vlen = dsize >= 16 ? dsize - 16 : 0;
            if (value + vlen <= p + pos + size) {
                gchar* text = g_utf8_make_valid((const gchar*)value, (gssize)vlen);
                if (memcmp(type, "\xA9nam", 4) == 0) set_field(&t->title, text);
                else if (memcmp(type, "\xA9" "ART", 4) == 0) set_field(&t->artist, text);
                else if (memcmp(type, "\xA9" "alb", 4) == 0) set_field(&t->album, text);
                else if (memcmp(type, "aART", 4) == 0) set_field(&t->album_artist, text);
                else if (memcmp(type, "\xA9" "day", 4) == 0) set_field(&t->year, text);
                else if (memcmp(type, "\xA9gen", 4) == 0) set_field(&t->genre, text);
                else if (memcmp(type, "\xA9" "cmt", 4) == 0) set_field(&t->comment, text);
                else if (memcmp(type, "trkn", 4) == 0 && vlen >= 6) {
                    guint n = ((guint)value[2] << 8) | value[3];
                    guint total = ((guint)value[4] << 8) | value[5];
                    g_free(text);
                    set_field(&t->track, total > 0 ? g_strdup_printf("%u/%u", n, total) : g_strdup_printf("%u", n));
                } else if (memcmp(type, "covr", 4) == 0) {
                    g_free(text);
                    set_cover(t, value, vlen, TRUE);
                } else {
                    g_free(text);
                }
            }
        }
        pos += size;
    }
}

/* Walk the boxes of moov: mvhd (duration), tkhd (video size), stsd
 * (codec), udta/meta/ilst (tags) */
static void mp4_boxes(MediaTags* t, const guchar* p, gsize len, guint depth) {
    gsize pos = 0;
    while (pos + 8 <= len && depth < 8) {
        guint64 size = be32(p + pos);
        gsize header = 8;
        if (size == 1 && pos + 16 <= len) {
            size = be64(p + pos + 8);
            header = 16;
        } else if (size == 0) {
            size = len - pos;
        }
        if (size < header || size > len - pos) {
            break;
        }
        const guchar* type = p + pos + 4;
        const guchar* body = p + pos + header;
        gsize blen = (gsize)size - header;
        if (memcmp(type, "mvhd", 4) == 0 && blen >= 20) {
            gboolean v1 = body[0] == 1;
            guint32 scale = v1 ? be32(body + 20) : be32(body + 12);
            guint64 dur = v1 ? (blen >= 32 ? be64(body + 24) : 0) : be32(body + 16);
            if (scale > 0) {
                t->duration = (gdouble)dur / scale;
            }
        } else if (memcmp(type, "tkhd", 4) == 0 && blen >= 84) {
            gsize off = body[0] == 1 ? 88 : 76;
            if (off + 8 <= blen) {
                guint w = be32(body + off) >> 16;
                guint h = be32(body + off + 4) >> 16;
                if (w > 0 && h > 0 && t->width == 0) {
                    t->width = w;
                    t->height = h;
                }
            }
        } else if (memcmp(type, "stsd", 4) == 0 && blen >= 16) {
            const guchar* fmt = body + 12;
            gchar* codec = NULL;
            if (memcmp(fmt, "mp4a", 4) == 0) codec = g_strdup("AAC");
            else if (memcmp(fmt, "alac", 4) == 0) codec = g_strdup("ALAC");
            else if (memcmp(fmt, "avc1", 4) == 0 || memcmp(fmt, "avc3", 4) == 0) codec = g_strdup("H.264");
            else if (memcmp(fmt, "hvc1", 4) == 0 || memcmp(fmt, "hev1", 4) == 0) codec = g_strdup("H.265");
            else if (memcmp(fmt, "av01", 4) == 0) codec = g_strdup("AV1");
            else if (memcmp(fmt, "vp09", 4) == 0) codec = g_strdup("VP9");
            else if (memcmp(fmt, "Opus", 4) == 0) codec = g_strdup("Opus");
            if (codec != NULL) {
                if (t->codec == NULL) {
                    t->codec = codec;
                } else if (strstr(t->codec, codec) == NULL) {
                    gchar* both = g_strdup_printf("%s / %s", t->codec, codec);
                    g_free(t->codec);
                    g_free(codec);
                    t->codec = both;
                } else {
                    g_free(codec);
                }
            }
            /* Audio sample entry: channels and rate */
            if ((memcmp(fmt, "mp4a", 4) == 0 || memcmp(fmt, "alac", 4) == 0) && blen >= 44) {
                const guchar* e = body + 8 + 8 + 16;
                t->channels = ((guint)e[0] << 8) | e[1];
                t->bits_per_sample = ((guint)e[2] << 8) | e[3];
                t->sample_rate = be32(e + 8) >> 16;
            }
        } else if (memcmp(type, "ilst", 4) == 0) {
            mp4_ilst(t, body, blen);
        } else if (memcmp(type, "meta", 4) == 0 && blen > 4) {
            /* iTunes: a full box (version and flags, then hdlr); QuickTime: hdlr first */
            gboolean full = blen >= 12 && memcmp(body + 8, "hdlr", 4) == 0;
            mp4_boxes(t, body + (full ? 4 : 0), blen - (full ? 4 : 0), depth + 1);
        } else if (memcmp(type, "trak", 4) == 0 || memcmp(type, "mdia", 4) == 0 ||
                   memcmp(type, "minf", 4) == 0 || memcmp(type, "stbl", 4) == 0 ||
                   memcmp(type, "udta", 4) == 0 || memcmp(type, "moov", 4) == 0) {
            mp4_boxes(t, body, blen, depth + 1);
        }
        pos += (gsize)size;
    }
}

/* Find the moov box in the file (it may follow a large mdat) and parse it */
static void parse_mp4(MediaTags* t, FILE* fp, gint64 size) {
    gint64 pos = 0;
    guchar h[16];
    while (pos + 8 <= size) {
        if (fseeko(fp, pos, SEEK_SET) != 0 || fread(h, 1, 8, fp) != 8) {
            return;
        }
        guint64 bsize = be32(h);
        gsize header = 8;
        if (bsize == 1) {
            if (fread(h + 8, 1, 8, fp) != 8) {
                return;
            }
            bsize = be64(h + 8);
            header = 16;
        } else if (bsize == 0) {
            bsize = (guint64)(size - pos);
        }
        if (bsize < header) {
            return;
        }
        if (memcmp(h + 4, "moov", 4) == 0) {
            if (bsize > MAX_MOOV) {
                return;
            }
            guchar* moov = g_malloc((gsize)bsize);
            if (fseeko(fp, pos + (gint64)header, SEEK_SET) == 0 &&
                fread(moov, 1, (gsize)bsize - header, fp) == (gsize)bsize - header) {
                mp4_boxes(t, moov, (gsize)bsize - header, 0);
            }
            g_free(moov);
            return;
        }
        pos += (gint64)bsize;
    }
}

/* --- WAVE --- */

static void parse_wave(MediaTags* t, const guchar* d, gsize len) {
    gsize pos = 12;
    guint32 byte_rate = 0;
    while (pos + 8 <= len) {
        guint32 csize = le32(d + pos + 4);
        const guchar* c = d + pos + 8;
        if (memcmp(d + pos, "fmt ", 4) == 0 && csize >= 16 && pos + 8 + 16 <= len) {
            guint fmt = c[0] | (c[1] << 8);
            t->codec = g_strdup(fmt == 1 || fmt == 0xFFFE ? "PCM" : fmt == 3 ? "PCM (float)" : fmt == 0x55 ? "MP3" : "WAVE");
            t->channels = c[2] | (c[3] << 8);
            t->sample_rate = le32(c + 4);
            byte_rate = le32(c + 8);
            t->bits_per_sample = c[14] | (c[15] << 8);
            t->bitrate = byte_rate * 8 / 1000;
        } else if (memcmp(d + pos, "data", 4) == 0) {
            if (byte_rate > 0) {
                t->duration = (gdouble)csize / byte_rate;
            }
            return;
        }
        pos += 8 + csize + (csize & 1);
    }
}

MediaTags* media_tags_read_file(const gchar* path) {
    FILE* fp = g_fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    GStatBuf st;
    gint64 size = g_stat(path, &st) == 0 ? (gint64)st.st_size : 0;
    guchar* d = g_malloc(HEAD_BYTES);
    gsize len = fread(d, 1, HEAD_BYTES, fp);
    MediaTags* t = g_new0(MediaTags, 1);

    if (len >= 10 && memcmp(d, "ID3", 3) == 0) {
        gsize tag = 10 + syncsafe(d + 6);
        if (tag > len && tag <= MAX_TAG_BYTES && (gint64)tag <= size) {
            d = g_realloc(d, tag + 65536);
            len += fread(d + len, 1, tag + 65536 - len, fp);
        }
        parse_id3v2(t, d, len);
        if (memcmp(d + MIN(tag, len - 4), "fLaC", 4) == 0) {
            parse_flac(t, d + tag, len - tag);
        } else {
            parse_mpeg_audio(t, d, len, MIN(tag, len), size);
        }
    } else if (len >= 4 && memcmp(d, "fLaC", 4) == 0) {
        parse_flac(t, d, len);
    } else if (len >= 4 && memcmp(d, "OggS", 4) == 0) {
        parse_ogg(t, d, len, path, size);
    } else if (len >= 12 && memcmp(d + 4, "ftyp", 4) == 0) {
        parse_mp4(t, fp, size);
    } else if (len >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WAVE", 4) == 0) {
        parse_wave(t, d, len);
    } else if (len >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0) {
        parse_mpeg_audio(t, d, len, 0, size);
    }
    /* ID3v1 at the end of MP3 files without (complete) ID3v2 tags */
    if (t->title == NULL && size >= 128 && fseeko(fp, size - 128, SEEK_SET) == 0) {
        guchar tag[128];
        if (fread(tag, 1, sizeof(tag), fp) == sizeof(tag)) {
            parse_id3v1(t, tag);
        }
    }
    fclose(fp);
    g_free(d);
    if (t->codec == NULL && t->title == NULL && t->cover == NULL && t->duration == 0) {
        media_tags_free(t);
        return NULL;
    }
    return t;
}

void media_tags_free(MediaTags* t) {
    if (t == NULL) {
        return;
    }
    g_free(t->title);
    g_free(t->artist);
    g_free(t->album);
    g_free(t->album_artist);
    g_free(t->year);
    g_free(t->track);
    g_free(t->genre);
    g_free(t->comment);
    g_free(t->codec);
    if (t->cover != NULL) {
        g_bytes_unref(t->cover);
    }
    g_free(t);
}

gchar* media_tags_format_duration(gdouble seconds) {
    gint64 s = (gint64)(seconds + 0.5);
    if (s >= 3600) {
        return g_strdup_printf("%" G_GINT64_FORMAT ":%02d:%02d", s / 3600, (gint)((s / 60) % 60), (gint)(s % 60));
    }
    return g_strdup_printf("%d:%02d", (gint)(s / 60), (gint)(s % 60));
}

gchar* media_tags_technical_line(const MediaTags* t) {
    GString* s = g_string_new(t->codec != NULL ? t->codec : "");
#define SEP() do { if (s->len > 0) g_string_append(s, " · "); } while (0)
    if (t->width > 0) {
        SEP();
        g_string_append_printf(s, "%u × %u", t->width, t->height);
    }
    if (t->bitrate > 0) {
        SEP();
        g_string_append_printf(s, "%u kbit/s", t->bitrate);
    }
    if (t->sample_rate > 0) {
        SEP();
        if (t->sample_rate % 1000 == 0) {
            g_string_append_printf(s, "%u kHz", t->sample_rate / 1000);
        } else {
            g_string_append_printf(s, "%.1f kHz", t->sample_rate / 1000.0);
        }
    }
    if (t->bits_per_sample > 0 && t->codec != NULL &&
        (strcmp(t->codec, "FLAC") == 0 || g_str_has_prefix(t->codec, "PCM") || strcmp(t->codec, "ALAC") == 0)) {
        SEP();
        g_string_append_printf(s, "%u-bit", t->bits_per_sample);
    }
    if (t->channels > 0) {
        SEP();
        g_string_append(s, t->channels == 1 ? "mono" : t->channels == 2 ? "stereo" : "");
        if (t->channels > 2) {
            g_string_append_printf(s, "%u channels", t->channels);
        }
    }
    if (t->duration > 0) {
        SEP();
        gchar* d = media_tags_format_duration(t->duration);
        g_string_append(s, d);
        g_free(d);
    }
#undef SEP
    return g_string_free(s, FALSE);
}
