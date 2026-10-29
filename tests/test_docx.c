/* Unit tests for src/docx.c: Word document to HTML conversion. */
#include "docx.h"
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
    "</Relationships>";

/* French Word: style ids are translated, names are not */
static const gchar* STYLES =
    "<w:styles " W_NS ">"
    "<w:style w:type=\"paragraph\" w:styleId=\"Titre1\"><w:name w:val=\"heading 1\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"MonTitre\"><w:name w:val=\"My Heading\"/><w:basedOn w:val=\"Titre1\"/></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Plan\"><w:name w:val=\"Outline\"/><w:pPr><w:outlineLvl w:val=\"2\"/></w:pPr></w:style>"
    "<w:style w:type=\"paragraph\" w:styleId=\"Puces\"><w:name w:val=\"List Bullet\"/><w:pPr><w:numPr><w:numId w:val=\"1\"/></w:numPr></w:pPr></w:style>"
    "<w:style w:type=\"character\" w:styleId=\"Fort\"><w:name w:val=\"Strong\"/><w:rPr><w:b/></w:rPr></w:style>"
    "</w:styles>";

static const gchar* NUMBERING =
    "<w:numbering " W_NS ">"
    "<w:abstractNum w:abstractNumId=\"0\">"
    "<w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"bullet\"/></w:lvl>"
    "<w:lvl w:ilvl=\"1\"><w:numFmt w:val=\"bullet\"/></w:lvl>"
    "</w:abstractNum>"
    "<w:abstractNum w:abstractNumId=\"1\">"
    "<w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/></w:lvl>"
    "<w:lvl w:ilvl=\"1\"><w:start w:val=\"1\"/><w:numFmt w:val=\"lowerLetter\"/></w:lvl>"
    "</w:abstractNum>"
    "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>"
    "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>"
    "</w:numbering>";

static const gchar* CORE =
    "<cp:coreProperties xmlns:cp=\"x\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
    "<dc:title>Ignored</dc:title><dc:creator>Ada &lt;\"Countess\"&gt; Lovelace</dc:creator>"
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
                                 "<w:document " W_NS "><w:body>%s<w:sectPr><w:pgSz w:w=\"11906\"/></w:sectPr>"
                                 "</w:body></w:document>", body);
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "[Content_Types].xml", "<Types/>", TRUE);
    zip_builder_add(zb, "_rels/.rels", PACKAGE_RELS, TRUE);
    zip_builder_add(zb, "word/document.xml", doc, TRUE);
    if (with_parts) {
        zip_builder_add(zb, "word/_rels/document.xml.rels", DOC_RELS, TRUE);
        zip_builder_add(zb, "word/styles.xml", STYLES, TRUE);
        zip_builder_add(zb, "word/numbering.xml", NUMBERING, TRUE);
        zip_builder_add(zb, "docProps/core.xml", CORE, TRUE);
        zip_builder_add_full(zb, "word/media/image1.png", PNG, sizeof PNG, FALSE, 0);
        zip_builder_add(zb, "word/media/image2.emf", "EMF data", FALSE);
    }
    g_free(doc);
    return zip_builder_finish(zb);
}

static gchar* convert_full(const gchar* body, gboolean with_parts, gsize max_text) {
    GBytes* bytes = make_docx(body, with_parts);
    GError* err = NULL;
    ZipReader* zip = zip_reader_new_from_bytes(bytes, &err);
    g_assert_no_error(err);
    g_assert_true(docx_is_document(zip));
    gchar* html = docx_to_html(zip, max_text, &err);
    g_assert_no_error(err);
    g_assert_nonnull(html);
    zip_reader_free(zip);
    g_bytes_unref(bytes);
    return html;
}

/* The <body> part of the HTML, without the newlines between blocks */
static gchar* convert(const gchar* body) {
    gchar* html = convert_full(body, TRUE, 1 << 20);
    const gchar* start = strstr(html, "<body>");
    const gchar* end = strstr(html, "</body>");
    g_assert_nonnull(start);
    g_assert_nonnull(end);
    GString* out = g_string_new(NULL);
    for (const gchar* p = start + 6; p < end; p++) {
        if (*p != '\n') {
            g_string_append_c(out, *p);
        }
    }
    g_free(html);
    return g_string_free(out, FALSE);
}

#define P(text) "<w:p><w:r><w:t>" text "</w:t></w:r></w:p>"
#define STYLED(style, text) "<w:p><w:pPr><w:pStyle w:val=\"" style "\"/></w:pPr><w:r><w:t>" text "</w:t></w:r></w:p>"
#define ITEM(num, lvl, text) "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"" #lvl "\"/><w:numId w:val=\"" #num "\"/></w:numPr></w:pPr>" \
                             "<w:r><w:t>" text "</w:t></w:r></w:p>"

static void assert_converts(const gchar* body, const gchar* expected) {
    gchar* html = convert(body);
    g_assert_cmpstr(html, ==, expected);
    g_free(html);
}

static void test_paragraphs_and_escaping(void) {
    assert_converts(P("Hello") "<w:p/>" "<w:p><w:r><w:t xml:space=\"preserve\">a &lt;b&gt; &amp; c </w:t></w:r></w:p>",
                    "<p>Hello</p><p>a &lt;b&gt; &amp; c </p>");
}

static void test_headings(void) {
    assert_converts(STYLED("Titre1", "One") STYLED("MonTitre", "Inherited") STYLED("Plan", "Outline")
                    "<w:p><w:pPr><w:outlineLvl w:val=\"1\"/></w:pPr><w:r><w:t>Direct</w:t></w:r></w:p>"
                    STYLED("Unknown", "Body"),
                    "<h1>One</h1><h1>Inherited</h1><h3>Outline</h3><h2>Direct</h2><p>Body</p>");
}

static void test_run_formatting(void) {
    assert_converts("<w:p>"
                    "<w:r><w:rPr><w:b/><w:i/></w:rPr><w:t>bi</w:t></w:r>"
                    "<w:r><w:rPr><w:b w:val=\"0\"/><w:u w:val=\"single\"/></w:rPr><w:t>u</w:t></w:r>"
                    "<w:r><w:rPr><w:u w:val=\"none\"/><w:strike/></w:rPr><w:t>s</w:t></w:r>"
                    "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:t>2</w:t></w:r>"
                    "<w:r><w:rPr><w:rStyle w:val=\"Fort\"/></w:rPr><w:t>strong</w:t></w:r>"
                    "<w:r><w:rPr><w:vanish/></w:rPr><w:t>hidden</w:t></w:r>"
                    "<w:r><w:t>a</w:t><w:tab/><w:t>b</w:t><w:br/><w:t>c</w:t><w:br w:type=\"page\"/></w:r>"
                    "</w:p>",
                    "<p><b><i>bi</i></b><u>u</u><s>s</s><sup>2</sup><b>strong</b>a&#9;b<br>c</p>");
}

static void test_paragraph_mark_properties_ignored(void) {
    /* The rPr of pPr formats the paragraph mark, not the text */
    assert_converts("<w:p><w:pPr><w:tabs><w:tab w:val=\"left\" w:pos=\"720\"/></w:tabs><w:rPr><w:b/></w:rPr></w:pPr>"
                    "<w:r><w:t>plain</w:t></w:r></w:p>",
                    "<p>plain</p>");
}

static void test_lists(void) {
    assert_converts(ITEM(1, 0, "a") ITEM(1, 1, "a.1") ITEM(1, 0, "b") STYLED("Puces", "styled") P("after"),
                    "<ul><li>a</li><ul><li>a.1</li></ul><li>b</li><li>styled</li></ul><p>after</p>");
}

static void test_numbered_list_continues(void) {
    assert_converts(ITEM(2, 0, "one") ITEM(2, 1, "sub") ITEM(2, 0, "two") P("break") ITEM(2, 0, "three")
                    ITEM(2, 1, "restarted"),
                    "<ol type=\"1\" start=\"1\"><li>one</li><ol type=\"a\" start=\"1\"><li>sub</li></ol>"
                    "<li>two</li></ol><p>break</p>"
                    "<ol type=\"1\" start=\"3\"><li>three</li><ol type=\"a\" start=\"1\"><li>restarted</li></ol></ol>");
}

static void test_list_change_of_type(void) {
    assert_converts(ITEM(1, 0, "bullet") ITEM(2, 0, "number") ITEM(0, 0, "numId 0 is no list"),
                    "<ul><li>bullet</li></ul><ol type=\"1\" start=\"1\"><li>number</li></ol>"
                    "<p>numId 0 is no list</p>");
}

static void test_hyperlink(void) {
    assert_converts("<w:p><w:hyperlink r:id=\"rIdLink\"><w:r><w:rPr><w:b/></w:rPr><w:t>link</w:t></w:r></w:hyperlink>"
                    "<w:hyperlink w:anchor=\"_Toc1\"><w:r><w:t> internal</w:t></w:r></w:hyperlink></w:p>",
                    "<p><a href=\"https://example.com/a?b=1&c='2'\"><b>link</b></a> internal</p>");
}

static void test_table(void) {
    assert_converts("<w:tbl><w:tblPr/>"
                    "<w:tr><w:trPr><w:tblHeader/></w:trPr><w:tc><w:p><w:r><w:t>H1</w:t></w:r></w:p></w:tc>"
                    "<w:tc><w:p><w:r><w:t>H2</w:t></w:r></w:p></w:tc></w:tr>"
                    "<w:tr><w:tc><w:tcPr><w:gridSpan w:val=\"2\"/></w:tcPr>"
                    "<w:p><w:r><w:t>wide</w:t></w:r></w:p><w:p><w:r><w:t>two paras</w:t></w:r></w:p></w:tc></w:tr>"
                    "<w:tr><w:tc>" ITEM(1, 0, "item in cell") "</w:tc><w:tc>"
                    "<w:tbl><w:tr><w:tc>" P("n1") "</w:tc><w:tc>" P("n2") "</w:tc></w:tr></w:tbl></w:tc></w:tr>"
                    "</w:tbl>" P("after"),
                    "<table><tr><th>H1</th><th>H2</th></tr>"
                    "<tr><td>wide two paras</td><td></td></tr>"
                    "<tr><td>item in cell</td><td>n1 n2</td></tr></table><p>after</p>");
}

static void test_images(void) {
    gchar* html = convert("<w:p><w:r><w:drawing><wp:inline><wp:docPr id=\"1\" name=\"Picture 1\" descr=\"A &quot;red&quot; dot\"/>"
                          "<a:graphic><a:graphicData><a:blip r:embed=\"rIdPng\"/></a:graphicData></a:graphic>"
                          "</wp:inline></w:drawing></w:r></w:p>"
                          "<w:p><w:r><w:drawing><wp:docPr id=\"2\" name=\"Chart 2\"/><a:blip r:embed=\"rIdEmf\"/>"
                          "<w:txbxContent><w:p><w:r><w:t>text box</w:t></w:r></w:p></w:txbxContent></w:drawing></w:r></w:p>");
    gchar* b64 = g_base64_encode(PNG, sizeof PNG);
    gchar* expected = g_strdup_printf("<p><img src=\"data:image/png;base64,%s\" alt=\"A 'red' dot\"></p>"
                                      "<p><img src=\"data:image/none;base64,\" alt=\"Chart 2\"></p>", b64);
    g_assert_cmpstr(html, ==, expected);
    g_free(expected);
    g_free(b64);
    g_free(html);
}

static void test_alternate_content_and_revisions(void) {
    assert_converts("<w:p><mc:AlternateContent><mc:Choice Requires=\"wps\"><w:r><w:t>new</w:t></w:r></mc:Choice>"
                    "<mc:Fallback><w:r><w:t>old</w:t></w:r></mc:Fallback></mc:AlternateContent>"
                    "<w:ins><w:r><w:t> inserted</w:t></w:r></w:ins>"
                    "<w:del><w:r><w:delText> deleted</w:delText></w:r></w:del>"
                    "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText> PAGE </w:instrText></w:r>"
                    "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t> 7</w:t></w:r>"
                    "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>",
                    "<p>new inserted 7</p>");
}

static void test_math(void) {
    assert_converts("<w:p><m:oMath><m:r><m:t>x</m:t></m:r><m:sSup><m:e><m:r><m:t>y</m:t></m:r></m:e>"
                    "<m:sup><m:r><m:t>2</m:t></m:r></m:sup></m:sSup>"
                    "<m:f><m:num><m:r><m:t>a</m:t></m:r></m:num><m:den><m:r><m:t>b</m:t></m:r></m:den></m:f>"
                    "</m:oMath></w:p>",
                    "<p><i>xy</i><sup><i>2</i></sup><i>a</i>/<i>b</i></p>");
}

static void test_author_meta(void) {
    gchar* html = convert_full(P("x"), TRUE, 1 << 20);
    g_assert_true(g_str_has_prefix(html, "<html><head><meta name=\"author\" content=\"Ada  'Countess'  Lovelace\"></head>"));
    g_assert_null(strstr(html, "Ignored"));
    g_free(html);
}

static void test_minimal_package(void) {
    /* No styles, numbering, relationships or properties */
    gchar* html = convert_full(STYLED("Heading1", "t") ITEM(5, 0, "x"), FALSE, 1 << 20);
    g_assert_cmpstr(html, ==, "<html><head></head><body>\n<p>t</p>\n<ul>\n<li>x</li>\n</ul>\n</body></html>\n");
    g_free(html);
}

static void test_truncation(void) {
    GString* body = g_string_new("<w:tbl><w:tr><w:tc>");
    for (gint i = 0; i < 200; i++) {
        g_string_append(body, P("0123456789"));
    }
    g_string_append(body, "</w:tc></w:tr></w:tbl>");
    gchar* html = convert_full(body->str, TRUE, 100);
    g_assert_nonnull(strstr(html, "<table>\n<tr><td>0123456789 0123456789"));
    g_assert_nonnull(strstr(html, "</td></tr>\n</table>\n<hr>\n<p><i>Document truncated"));
    g_assert_cmpuint(strlen(html), <, 600);
    g_free(html);
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
    g_assert_null(docx_to_html(zip, 1000, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&err);
    zip_reader_free(zip);
    g_bytes_unref(bytes);

    g_assert_null(docx_file_to_html("/nonexistent.docx", 1000, &err));
    g_assert_nonnull(err);
    g_clear_error(&err);
}

static void test_malformed_xml(void) {
    GBytes* bytes = make_docx("<w:p><w:r><w:t>unclosed</w:r></w:p>", FALSE);
    ZipReader* zip = zip_reader_new_from_bytes(bytes, NULL);
    GError* err = NULL;
    g_assert_null(docx_to_html(zip, 1000, &err));
    g_assert_error(err, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE);
    g_clear_error(&err);
    zip_reader_free(zip);
    g_bytes_unref(bytes);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/docx/paragraphs_and_escaping", test_paragraphs_and_escaping);
    g_test_add_func("/docx/headings", test_headings);
    g_test_add_func("/docx/run_formatting", test_run_formatting);
    g_test_add_func("/docx/paragraph_mark_properties_ignored", test_paragraph_mark_properties_ignored);
    g_test_add_func("/docx/lists", test_lists);
    g_test_add_func("/docx/numbered_list_continues", test_numbered_list_continues);
    g_test_add_func("/docx/list_change_of_type", test_list_change_of_type);
    g_test_add_func("/docx/hyperlink", test_hyperlink);
    g_test_add_func("/docx/table", test_table);
    g_test_add_func("/docx/images", test_images);
    g_test_add_func("/docx/alternate_content_and_revisions", test_alternate_content_and_revisions);
    g_test_add_func("/docx/math", test_math);
    g_test_add_func("/docx/author_meta", test_author_meta);
    g_test_add_func("/docx/minimal_package", test_minimal_package);
    g_test_add_func("/docx/truncation", test_truncation);
    g_test_add_func("/docx/not_a_document", test_not_a_document);
    g_test_add_func("/docx/malformed_xml", test_malformed_xml);
    return g_test_run();
}
