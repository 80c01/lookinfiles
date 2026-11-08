/* Unit tests for src/office/docx.c and src/office/docx_styles.c: Word documents as styled previews. */
#include "office/docx.h"
#include "zip_builder.h"

#include <string.h>

#define W_NS "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" " \
             "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" " \
             "xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" " \
             "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" " \
             "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" " \
             "xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\""

static const gchar* PACKAGE_RELS =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
    "</Relationships>";

static const gchar* DOC_RELS =
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
    "<Relationship Id=\"rIdNum\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>"
    "<Relationship Id=\"rIdLink\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"https://example.com/a?b=1&amp;c=&quot;2&quot;\" TargetMode=\"External\"/>"
    "<Relationship Id=\"rIdPng\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/image1.png\"/>"
    "<Relationship Id=\"rIdEmf\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"/word/media/image2.emf\"/>"
    "<Relationship Id=\"rIdTheme\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" Target=\"theme/theme1.xml\"/>"
    "<Relationship Id=\"rIdHead\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\" Target=\"header1.xml\"/>"
    "<Relationship Id=\"rIdFirst\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\" Target=\"header2.xml\"/>"
    "<Relationship Id=\"rIdFoot\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer1.xml\"/>"
    "</Relationships>";

static const gchar* THEME =
    "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"><a:themeElements><a:fontScheme>"
    "<a:majorFont><a:latin typeface=\"Caladea\"/></a:majorFont><a:minorFont><a:latin typeface=\"Carlito\"/></a:minorFont>"
    "</a:fontScheme></a:themeElements></a:theme>";

static const gchar* HEADER =
    "<w:hdr " W_NS "><w:p><w:pPr><w:jc w:val=\"right\"/></w:pPr><w:r><w:t>Confidential</w:t></w:r></w:p></w:hdr>";
static const gchar* FIRST_HEADER = "<w:hdr " W_NS "><w:p><w:r><w:t>First page only</w:t></w:r></w:p></w:hdr>";
static const gchar* FOOTER =
    "<w:ftr " W_NS "><w:p><w:r><w:t xml:space=\"preserve\">Page </w:t></w:r><w:fldSimple w:instr=\"PAGE\">"
    "<w:r><w:t>1</w:t></w:r></w:fldSimple></w:p></w:ftr>";

/* French Word: style ids are translated, names are not */
static const gchar* STYLES =
    "<w:styles " W_NS ">"
    "<w:style w:type=\"paragraph\" w:styleId=\"Titre1\"><w:name w:val=\"heading 1\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"MonTitre\"><w:name w:val=\"My Heading\"/><w:basedOn w:val=\"Titre1\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Plan\"><w:name w:val=\"Outline\"/><w:pPr><w:outlineLvl w:val=\"2\"/></w:pPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Puces\"><w:name w:val=\"List Bullet\"/><w:pPr><w:numPr><w:numId w:val=\"1\"/></w:numPr></w:pPr></w:style>"
    "<w:style w:type=\"character\" w:styleId=\"Fort\"><w:name w:val=\"Strong\"/><w:rPr><w:b/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/>"
    "<w:pPr><w:spacing w:after=\"160\"/></w:pPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Accent\"><w:name w:val=\"Accent\"/><w:basedOn w:val=\"Normal\"/>"
    "<w:pPr><w:jc w:val=\"center\"/><w:spacing w:before=\"240\" w:after=\"0\"/></w:pPr>"
    "<w:rPr><w:rFonts w:asciiTheme=\"majorHAnsi\"/><w:color w:val=\"1F3864\"/><w:sz w:val=\"44\"/></w:rPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Quote\"><w:name w:val=\"Quote\"/><w:basedOn w:val=\"Accent\"/>"
    "<w:pPr><w:jc w:val=\"left\"/></w:pPr><w:rPr><w:i/></w:rPr></w:style>"
    "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:asciiTheme=\"minorHAnsi\"/><w:sz w:val=\"22\"/></w:rPr></w:rPrDefault>"
    "<w:pPrDefault><w:pPr><w:spacing w:after=\"100\"/></w:pPr></w:pPrDefault></w:docDefaults>"
    "</w:styles>";

static const gchar* NUMBERING =
    "<w:numbering " W_NS ">"
    "<w:abstractNum w:abstractNumId=\"0\">"
    "<w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"\xEF\x82\xB7\"/>"
    "<w:pPr><w:ind w:left=\"720\" w:hanging=\"360\"/></w:pPr><w:rPr><w:rFonts w:ascii=\"Symbol\"/></w:rPr></w:lvl>"
    "<w:lvl w:ilvl=\"1\"><w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"o\"/><w:rPr><w:rFonts w:ascii=\"Courier New\"/></w:rPr></w:lvl>"
    "</w:abstractNum>"
    "<w:abstractNum w:abstractNumId=\"1\">"
    "<w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/></w:lvl>"
    "<w:lvl w:ilvl=\"1\"><w:start w:val=\"1\"/><w:numFmt w:val=\"lowerLetter\"/></w:lvl>"
    "</w:abstractNum>"
    "<w:abstractNum w:abstractNumId=\"2\">"
    "<w:lvl w:ilvl=\"0\"><w:start w:val=\"3\"/><w:numFmt w:val=\"upperRoman\"/><w:lvlText w:val=\"%1)\"/></w:lvl>"
    "<w:lvl w:ilvl=\"1\"><w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%1.%2.\"/></w:lvl>"
    "</w:abstractNum>"
    "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>"
    "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>"
    "<w:num w:numId=\"3\"><w:abstractNumId w:val=\"2\"/></w:num>"
    "<w:num w:numId=\"4\"><w:abstractNumId w:val=\"2\"/><w:lvlOverride w:ilvl=\"0\"><w:startOverride w:val=\"10\"/></w:lvlOverride></w:num>"
    "</w:numbering>";

static const gchar* CORE =
    "<cp:coreProperties xmlns:cp=\"x\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
    "<dc:title>Caf\xC3\xA9 report</dc:title><dc:creator>Ada &lt;\"Countess\"&gt; Lovelace</dc:creator>"
    "</cp:coreProperties>";

/* 1x1 PNG */
static const guchar PNG[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
    0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
    0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static GBytes* make_docx(const gchar* body, gboolean with_parts) {
    gchar* doc = g_strdup_printf("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                                 "<w:document " W_NS "><w:body>%s<w:sectPr>"
                                 "<w:headerReference w:type=\"first\" r:id=\"rIdFirst\"/>"
                                 "<w:headerReference w:type=\"default\" r:id=\"rIdHead\"/>"
                                 "<w:footerReference w:type=\"default\" r:id=\"rIdFoot\"/>"
                                 "<w:pgSz w:w=\"11906\"/></w:sectPr></w:body></w:document>", body);
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "[Content_Types].xml", "<Types/>", TRUE);
    zip_builder_add(zb, "_rels/.rels", PACKAGE_RELS, TRUE);
    zip_builder_add(zb, "word/document.xml", doc, TRUE);
    if (with_parts) {
        zip_builder_add(zb, "word/_rels/document.xml.rels", DOC_RELS, TRUE);
        zip_builder_add(zb, "word/styles.xml", STYLES, TRUE);
        zip_builder_add(zb, "word/numbering.xml", NUMBERING, TRUE);
        zip_builder_add(zb, "word/theme/theme1.xml", THEME, TRUE);
        zip_builder_add(zb, "word/header1.xml", HEADER, TRUE);
        zip_builder_add(zb, "word/header2.xml", FIRST_HEADER, TRUE);
        zip_builder_add(zb, "word/footer1.xml", FOOTER, TRUE);
        zip_builder_add(zb, "docProps/core.xml", CORE, TRUE);
        zip_builder_add_full(zb, "word/media/image1.png", PNG, sizeof PNG, FALSE, 0);
        zip_builder_add(zb, "word/media/image2.emf", "EMF data", FALSE);
    }
    g_free(doc);
    return zip_builder_finish(zb);
}

static PreviewDoc* convert_full(const gchar* body, gboolean with_parts, gsize max_text) {
    GBytes* bytes = make_docx(body, with_parts);
    GError* err = NULL;
    ZipReader* zip = zip_reader_new_from_bytes(bytes, &err);
    g_assert_no_error(err);
    g_assert_true(docx_is_document(zip));
    PreviewDoc* doc = docx_to_preview(zip, "test.docx", "4 kB", max_text, &err);
    g_assert_no_error(err);
    g_assert_nonnull(doc);
    zip_reader_free(zip);
    g_bytes_unref(bytes);
    return doc;
}

/* Offset (in characters) of the body: after the title, the subtitle, the
 * blank line, and the header with its rule */
static gint body_start(const PreviewDoc* doc) {
    const gchar* p = strstr(doc->text->str, "\n\n") + 2;
    if (g_str_has_prefix(p, "Confidential\n")) {
        p = strchr(p + strlen("Confidential\n"), '\n') + 1;
    }
    return (gint)g_utf8_pointer_to_offset(doc->text->str, p);
}

/* The body text: without the title, header and footer */
static gchar* body_text(const PreviewDoc* doc) {
    const gchar* start = g_utf8_offset_to_pointer(doc->text->str, body_start(doc));
    const gchar* foot = strstr(start, "────────────────────────\nPage 1");
    return foot != NULL ? g_strndup(start, (gsize)(foot - start)) : g_strdup(start);
}

/* First span of a style covering the text "what" (NULL if none), searched
 * from the body on, unless it is only in the title or header */
static const PreviewSpan* span_on(const PreviewDoc* doc, PreviewStyle style, const gchar* what) {
    const gchar* at = strstr(g_utf8_offset_to_pointer(doc->text->str, body_start(doc)), what);
    if (at == NULL) {
        at = strstr(doc->text->str, what);
    }
    g_assert_nonnull(at);
    gint start = (gint)g_utf8_pointer_to_offset(doc->text->str, at);
    gint end = start + (gint)g_utf8_strlen(what, -1);
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* s = &g_array_index(doc->spans, PreviewSpan, i);
        if (s->style == style && s->start <= start && s->end >= end) {
            return s;
        }
    }
    return NULL;
}

static const gchar* span_arg(const PreviewDoc* doc, PreviewStyle style, const gchar* what) {
    const PreviewSpan* s = span_on(doc, style, what);
    return s != NULL ? s->arg : NULL;
}

static void assert_body(const gchar* body, const gchar* expected) {
    PreviewDoc* doc = convert_full(body, TRUE, 1 << 20);
    gchar* text = body_text(doc);
    g_assert_cmpstr(text, ==, expected);
    g_free(text);
    preview_doc_free(doc);
}

#define P(text) "<w:p><w:r><w:t>" text "</w:t></w:r></w:p>"
#define STYLED(style, text) "<w:p><w:pPr><w:pStyle w:val=\"" style "\"/></w:pPr><w:r><w:t>" text "</w:t></w:r></w:p>"
#define ITEM(num, lvl, text) "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"" #lvl "\"/><w:numId w:val=\"" #num "\"/></w:numPr></w:pPr>" \
                             "<w:r><w:t>" text "</w:t></w:r></w:p>"
#define GAP "\xE2\x80\x82"

static void test_paragraphs_and_escaping(void) {
    assert_body(P("Hello") "<w:p/>" "<w:p><w:r><w:t xml:space=\"preserve\">a &lt;b&gt; &amp; c </w:t></w:r></w:p>",
                "Hello\n\na <b> & c \n");
}

static void test_title_header_footer(void) {
    PreviewDoc* doc = convert_full(P("Body"), TRUE, 1 << 20);
    /* The document's title and author; the default header, not the first-page one */
    g_assert_true(g_str_has_prefix(doc->text->str, "Café report\nWord document · Ada <\"Countess\"> Lovelace · 4 kB\n\n"
                                                   "Confidential\n────"));
    g_assert_null(strstr(doc->text->str, "First page only"));
    g_assert_true(g_str_has_suffix(doc->text->str, "Body\n────────────────────────\nPage 1\n"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_DIM, "Confidential"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_DIM, "Page 1"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_DIM, "Body"));
    PreviewParagraph layout;
    g_assert_true(preview_paragraph_parse(span_arg(doc, PREVIEW_STYLE_PARAGRAPH, "Confidential"), &layout));
    g_assert_cmpint(layout.align, ==, PREVIEW_ALIGN_RIGHT);
    preview_doc_free(doc);
}

static void test_headings(void) {
    /* Heading styles without formatting: Word's default sizes, inherited */
    PreviewDoc* doc = convert_full(STYLED("Titre1", "One") STYLED("MonTitre", "Inherited") STYLED("Plan", "Outline")
                                   STYLED("Unknown", "Body"), TRUE, 1 << 20);
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_SCALE, "One"), ==, "1.455"); /* 16 pt over 11 pt */
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_BOLD, "One"));
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_SCALE, "Inherited"), ==, "1.455");
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_SCALE, "Outline"), ==, "1.091");
    g_assert_null(span_on(doc, PREVIEW_STYLE_SCALE, "Body"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_BOLD, "Body"));
    /* Outline levels for the tagbar: from the style, inherited, or w:outlineLvl */
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_OUTLINE1, "One"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_OUTLINE1, "Inherited"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_OUTLINE3, "Outline"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_OUTLINE1, "Body"));
    preview_doc_free(doc);

    /* Direct w:outlineLvl: makes a heading, or body text (9) of a heading style */
    doc = convert_full("<w:p><w:pPr><w:outlineLvl w:val=\"1\"/></w:pPr><w:r><w:t>Direct</w:t></w:r></w:p>"
                       "<w:p><w:pPr><w:pStyle w:val=\"Titre1\"/><w:outlineLvl w:val=\"9\"/></w:pPr>"
                       "<w:r><w:t>Demoted</w:t></w:r></w:p>",
                       TRUE, 1 << 20);
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_OUTLINE2, "Direct"));
    for (gint st = PREVIEW_STYLE_OUTLINE1; st <= PREVIEW_STYLE_OUTLINE6; st++) {
        g_assert_null(span_on(doc, st, "Demoted"));
    }
    preview_doc_free(doc);
}

static void test_fonts_colours_layout(void) {
    PreviewDoc* doc = convert_full(
        STYLED("Accent", "Accent title") STYLED("Quote", "A quote") P("Normal text")
        "<w:p><w:pPr><w:ind w:left=\"1440\" w:hanging=\"720\"/><w:spacing w:before=\"120\"/><w:jc w:val=\"both\"/></w:pPr>"
        "<w:r><w:rPr><w:rFonts w:ascii=\"DejaVu Serif\"/><w:color w:val=\"FF0000\"/><w:sz w:val=\"33\"/></w:rPr><w:t>red</w:t></w:r>"
        "<w:r><w:rPr><w:highlight w:val=\"yellow\"/><w:caps/></w:rPr><w:t> marked</w:t></w:r>"
        "<w:r><w:rPr><w:color w:val=\"auto\"/><w:shd w:fill=\"00B0F0\"/></w:rPr><w:t> shaded</w:t></w:r></w:p>",
        TRUE, 1 << 20);
    gchar* text = body_text(doc);
    g_assert_cmpstr(text, ==, "Accent title\nA quote\nNormal text\nred MARKED shaded\n");
    g_free(text);
    /* Style: theme major font, colour, size; centred, spacing */
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_FAMILY, "Accent title"), ==, "Caladea");
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_COLOR, "Accent title"), ==, "#1f3864");
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_SCALE, "Accent title"), ==, "2.000");
    PreviewParagraph layout;
    g_assert_true(preview_paragraph_parse(span_arg(doc, PREVIEW_STYLE_PARAGRAPH, "Accent title"), &layout));
    g_assert_cmpint(layout.align, ==, PREVIEW_ALIGN_CENTER);
    g_assert_cmpint(layout.above, ==, 16);
    g_assert_cmpint(layout.below, ==, 0);
    /* Based on it: inherits the colour, overrides the alignment */
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_COLOR, "A quote"), ==, "#1f3864");
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_ITALIC, "A quote"));
    g_assert_true(preview_paragraph_parse(span_arg(doc, PREVIEW_STYLE_PARAGRAPH, "A quote"), &layout));
    g_assert_cmpint(layout.align, ==, PREVIEW_ALIGN_LEFT);
    /* Default style and document defaults: theme minor font, 8 pt after */
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_FAMILY, "Normal text"), ==, "Carlito");
    g_assert_null(span_on(doc, PREVIEW_STYLE_SCALE, "Normal text"));
    g_assert_true(preview_paragraph_parse(span_arg(doc, PREVIEW_STYLE_PARAGRAPH, "Normal text"), &layout));
    g_assert_cmpint(layout.below, ==, 11);
    /* Direct formatting */
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_FAMILY, "red"), ==, "DejaVu Serif");
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_COLOR, "red"), ==, "#ff0000");
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_SCALE, "red"), ==, "1.500");
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_HIGHLIGHT, " MARKED"), ==, "#ffff00");
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_HIGHLIGHT, " shaded"), ==, "#00b0f0");
    g_assert_null(span_on(doc, PREVIEW_STYLE_COLOR, " shaded"));
    g_assert_true(preview_paragraph_parse(span_arg(doc, PREVIEW_STYLE_PARAGRAPH, "red"), &layout));
    g_assert_cmpint(layout.align, ==, PREVIEW_ALIGN_JUSTIFY);
    g_assert_cmpint(layout.left, ==, 96);
    g_assert_cmpint(layout.indent, ==, -48);
    g_assert_cmpint(layout.above, ==, 8);
    preview_doc_free(doc);
}

static void test_run_formatting(void) {
    PreviewDoc* doc = convert_full("<w:p>"
                                   "<w:r><w:rPr><w:b/><w:i/></w:rPr><w:t>bi</w:t></w:r>"
                                   "<w:r><w:rPr><w:b w:val=\"0\"/><w:u w:val=\"single\"/></w:rPr><w:t>u</w:t></w:r>"
                                   "<w:r><w:rPr><w:u w:val=\"none\"/><w:strike/></w:rPr><w:t>s</w:t></w:r>"
                                   "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:t>2</w:t></w:r>"
                                   "<w:r><w:rPr><w:rStyle w:val=\"Fort\"/></w:rPr><w:t>strong</w:t></w:r>"
                                   "<w:r><w:rPr><w:vanish/></w:rPr><w:t>hidden</w:t></w:r>"
                                   "<w:r><w:t>a</w:t><w:tab/><w:t>b</w:t><w:br/><w:t>c</w:t><w:br w:type=\"page\"/></w:r>"
                                   "<w:r><w:t>next page</w:t></w:r></w:p>"
                                   "<w:p><w:pPr><w:pageBreakBefore/></w:pPr><w:r><w:t>third</w:t></w:r></w:p>",
                                   TRUE, 1 << 20);
    gchar* text = body_text(doc);
    g_assert_cmpstr(text, ==, "bius2stronga\tb\xE2\x80\xA8" "c\n────────────────────────\nnext page\n"
                              "────────────────────────\nthird\n");
    g_free(text);
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_BOLD, "bi"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_ITALIC, "bi"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_BOLD, "us"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_UNDERLINE, "u"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_STRIKE, "s"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_UNDERLINE, "s2"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_SUPERSCRIPT, "2"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_BOLD, "strong"));
    preview_doc_free(doc);
}

static void test_paragraph_mark_properties_ignored(void) {
    /* The rPr of pPr formats the paragraph mark, not the text */
    PreviewDoc* doc = convert_full("<w:p><w:pPr><w:tabs><w:tab w:val=\"left\" w:pos=\"720\"/></w:tabs>"
                                   "<w:rPr><w:b/></w:rPr></w:pPr><w:r><w:t>plain</w:t></w:r></w:p>", TRUE, 1 << 20);
    g_assert_null(span_on(doc, PREVIEW_STYLE_BOLD, "plain"));
    preview_doc_free(doc);
}

static void test_lists(void) {
    PreviewDoc* doc = convert_full(ITEM(1, 0, "a") ITEM(1, 1, "a.1") ITEM(1, 0, "b") STYLED("Puces", "styled") P("after"),
                                   TRUE, 1 << 20);
    gchar* text = body_text(doc);
    /* Symbol and Courier New bullets as Unicode */
    g_assert_cmpstr(text, ==, "•" GAP "a\n◦" GAP "a.1\n•" GAP "b\n•" GAP "styled\nafter\n");
    g_free(text);
    PreviewParagraph layout;
    g_assert_true(preview_paragraph_parse(span_arg(doc, PREVIEW_STYLE_PARAGRAPH, "•" GAP "a"), &layout));
    g_assert_cmpint(layout.left, ==, 48);
    g_assert_cmpint(layout.indent, ==, -24);
    /* The label is not in the bullet font */
    g_assert_cmpstr(span_arg(doc, PREVIEW_STYLE_FAMILY, "◦"), ==, "Courier New");
    preview_doc_free(doc);
}

static void test_numbered_lists(void) {
    assert_body(ITEM(2, 0, "one") ITEM(2, 1, "sub") ITEM(2, 0, "two") P("break") ITEM(2, 0, "three")
                ITEM(2, 1, "restarted"),
                "1." GAP "one\na." GAP "sub\n2." GAP "two\nbreak\n3." GAP "three\na." GAP "restarted\n");
    /* Level texts with other levels, start values and overrides */
    assert_body(ITEM(3, 0, "x") ITEM(3, 1, "y") ITEM(3, 1, "z") ITEM(4, 0, "own") ITEM(1, 0, "bullet")
                ITEM(0, 0, "numId 0 is no list"),
                "III)" GAP "x\nIII.1." GAP "y\nIII.2." GAP "z\nX)" GAP "own\n•" GAP "bullet\nnumId 0 is no list\n");
}

static void test_hyperlink(void) {
    PreviewDoc* doc = convert_full("<w:p><w:hyperlink r:id=\"rIdLink\"><w:r><w:rPr><w:b/></w:rPr><w:t>link</w:t></w:r></w:hyperlink>"
                                   "<w:hyperlink w:anchor=\"_Toc1\"><w:r><w:t> internal</w:t></w:r></w:hyperlink></w:p>",
                                   TRUE, 1 << 20);
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_LINK, "link"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_BOLD, "link"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_LINK, " internal"));
    preview_doc_free(doc);
}

static void test_table(void) {
    PreviewDoc* doc = convert_full("<w:tbl><w:tblPr/>"
                                   "<w:tr><w:trPr><w:tblHeader/></w:trPr><w:tc><w:p><w:r><w:t>H1</w:t></w:r></w:p></w:tc>"
                                   "<w:tc><w:p><w:r><w:t>H2</w:t></w:r></w:p></w:tc></w:tr>"
                                   "<w:tr><w:tc><w:tcPr><w:gridSpan w:val=\"2\"/></w:tcPr>"
                                   "<w:p><w:r><w:t>wide</w:t></w:r></w:p><w:p><w:r><w:t>two paras</w:t></w:r></w:p></w:tc></w:tr>"
                                   "<w:tr><w:tc>" ITEM(2, 0, "item") "</w:tc><w:tc>"
                                   "<w:tbl><w:tr><w:tc>" P("n1") "</w:tc><w:tc>" P("n2") "</w:tc></w:tr></w:tbl></w:tc></w:tr>"
                                   "</w:tbl>" P("after"),
                                   TRUE, 1 << 20);
    gchar* text = body_text(doc);
    g_assert_cmpstr(text, ==,
                    "┌─────────┬────┐\n"
                    "│ H1      │ H2 │\n"
                    "├─────────┼────┤\n"
                    "│ wide         │\n"
                    "│ two paras    │\n"
                    "├─────────┼────┤\n"
                    "│ 1. item │ n1 │\n"
                    "│         │ n2 │\n"
                    "└─────────┴────┘\n"
                    "after\n");
    g_free(text);
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_BOLD, "H1"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_BOLD, "n1"));
    preview_doc_free(doc);
}

static void test_images(void) {
    PreviewDoc* doc = convert_full(
        "<w:p><w:r><w:drawing><wp:inline><wp:extent cx=\"952500\" cy=\"952500\"/>"
        "<wp:docPr id=\"1\" name=\"Picture 1\" descr=\"A &quot;red&quot; dot\"/>"
        "<a:graphic><a:graphicData><a:blip r:embed=\"rIdPng\"/></a:graphicData></a:graphic>"
        "</wp:inline></w:drawing></w:r></w:p>"
        "<w:p><w:r><w:drawing><wp:docPr id=\"2\" name=\"Chart 2\"/><a:blip r:embed=\"rIdEmf\"/>"
        "<w:txbxContent><w:p><w:r><w:t>text box</w:t></w:r></w:p></w:txbxContent></w:drawing></w:r></w:p>",
        TRUE, 1 << 20);
    gchar* text = body_text(doc);
    g_assert_cmpstr(text, ==, PREVIEW_DOC_IMAGE_CHAR "\n[Chart 2]\n");
    g_free(text);
    g_assert_cmpuint(doc->images->len, ==, 1);
    const PreviewImage* image = &g_array_index(doc->images, PreviewImage, 0);
    g_assert_cmpint(image->max_width, ==, 100); /* 75 pt */
    g_assert_cmpuint(g_bytes_get_size(image->data), ==, sizeof PNG);
    preview_doc_free(doc);
}

static void test_alternate_content_and_revisions(void) {
    assert_body("<w:p><mc:AlternateContent><mc:Choice Requires=\"wps\"><w:r><w:t>new</w:t></w:r></mc:Choice>"
                "<mc:Fallback><w:r><w:t>old</w:t></w:r></mc:Fallback></mc:AlternateContent>"
                "<w:ins><w:r><w:t> inserted</w:t></w:r></w:ins>"
                "<w:del><w:r><w:delText> deleted</w:delText></w:r></w:del>"
                "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText> PAGE </w:instrText></w:r>"
                "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t> 7</w:t></w:r>"
                "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>",
                "new inserted 7\n");
}

static void test_math(void) {
    PreviewDoc* doc = convert_full("<w:p><m:oMath><m:r><m:t>x</m:t></m:r><m:sSup><m:e><m:r><m:t>y</m:t></m:r></m:e>"
                                   "<m:sup><m:r><m:t>2</m:t></m:r></m:sup></m:sSup>"
                                   "<m:f><m:num><m:r><m:t>a</m:t></m:r></m:num><m:den><m:r><m:t>b</m:t></m:r></m:den></m:f>"
                                   "</m:oMath></w:p>", TRUE, 1 << 20);
    gchar* text = body_text(doc);
    g_assert_cmpstr(text, ==, "xy2a/b\n");
    g_free(text);
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_ITALIC, "xy"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_SUPERSCRIPT, "2"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_SUPERSCRIPT, "a/b"));
    preview_doc_free(doc);
}

static void test_minimal_package(void) {
    /* No styles, numbering, relationships or properties */
    PreviewDoc* doc = convert_full(STYLED("Heading1", "t") ITEM(5, 0, "x"), FALSE, 1 << 20);
    g_assert_cmpstr(doc->text->str, ==, "test.docx\nWord document · 4 kB\n\nt\nx\n");
    preview_doc_free(doc);
}

static void test_truncation(void) {
    GString* body = g_string_new("<w:tbl><w:tr><w:tc>");
    for (gint i = 0; i < 200; i++) {
        g_string_append(body, P("0123456789"));
    }
    g_string_append(body, "</w:tc></w:tr></w:tbl>");
    PreviewDoc* doc = convert_full(body->str, TRUE, 100);
    g_assert_nonnull(strstr(doc->text->str, "│ 0123456789 │\n│ 0123456789 │"));
    g_assert_nonnull(strstr(doc->text->str, "Document truncated"));
    g_assert_cmpuint(strlen(doc->text->str), <, 1200);
    preview_doc_free(doc);
    g_string_free(body, TRUE);
}

static void test_not_a_document(void) {
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "mimetype", "application/vnd.oasis.opendocument.text", FALSE);
    zip_builder_add(zb, "content.xml", "<office:document-content/>", TRUE);
    GBytes* bytes = zip_builder_finish(zb);
    ZipReader* zip = zip_reader_new_from_bytes(bytes, NULL);
    g_assert_nonnull(zip);
    g_assert_false(docx_is_document(zip));
    GError* err = NULL;
    g_assert_null(docx_to_preview(zip, "x", NULL, 1000, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&err);
    zip_reader_free(zip);
    g_bytes_unref(bytes);

    g_assert_null(docx_file_preview("/nonexistent.docx", "x", NULL, 1000, &err));
    g_assert_nonnull(err);
    g_clear_error(&err);
}

static void test_malformed_xml(void) {
    GBytes* bytes = make_docx("<w:p><w:r><w:t>unclosed</w:r></w:p>", FALSE);
    ZipReader* zip = zip_reader_new_from_bytes(bytes, NULL);
    GError* err = NULL;
    g_assert_null(docx_to_preview(zip, "x", NULL, 1000, &err));
    g_assert_error(err, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE);
    g_clear_error(&err);
    zip_reader_free(zip);
    g_bytes_unref(bytes);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/docx/paragraphs_and_escaping", test_paragraphs_and_escaping);
    g_test_add_func("/docx/title_header_footer", test_title_header_footer);
    g_test_add_func("/docx/headings", test_headings);
    g_test_add_func("/docx/fonts_colours_layout", test_fonts_colours_layout);
    g_test_add_func("/docx/run_formatting", test_run_formatting);
    g_test_add_func("/docx/paragraph_mark_properties_ignored", test_paragraph_mark_properties_ignored);
    g_test_add_func("/docx/lists", test_lists);
    g_test_add_func("/docx/numbered_lists", test_numbered_lists);
    g_test_add_func("/docx/hyperlink", test_hyperlink);
    g_test_add_func("/docx/table", test_table);
    g_test_add_func("/docx/images", test_images);
    g_test_add_func("/docx/alternate_content_and_revisions", test_alternate_content_and_revisions);
    g_test_add_func("/docx/math", test_math);
    g_test_add_func("/docx/minimal_package", test_minimal_package);
    g_test_add_func("/docx/truncation", test_truncation);
    g_test_add_func("/docx/not_a_document", test_not_a_document);
    g_test_add_func("/docx/malformed_xml", test_malformed_xml);
    return g_test_run();
}
