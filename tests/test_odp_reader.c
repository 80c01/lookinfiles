/* Unit tests for src/office/odp_reader.c, odp_styles.c, odp_geometry.c and
 * odp_text.c: OpenDocument presentations as slides. */
#include "office/odp_reader.h"
#include "office/odp_geometry.h"
#include "office/odp_styles.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#include "zip_builder.h"

#define NS "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" " \
           "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" " \
           "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" " \
           "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" " \
           "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" " \
           "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" " \
           "xmlns:xlink=\"http://www.w3.org/1999/xlink\" " \
           "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" " \
           "xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\""

/* A 1x1 PNG */
static const guint8 PNG[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
    0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
    0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static const gchar STYLES[] =
    "<office:document-styles " NS ">"
    "<office:font-face-decls><style:font-face style:name=\"Body1\" svg:font-family=\"'DejaVu Sans', Arial\"/>"
    "</office:font-face-decls>"
    "<office:styles>"
    "<style:default-style style:family=\"graphic\"><style:graphic-properties draw:fill-color=\"#729fcf\"/>"
    "<style:text-properties style:font-name=\"Body1\" fo:font-size=\"24pt\" style:use-window-font-color=\"true\"/>"
    "</style:default-style>"
    "<style:style style:name=\"standard\" style:family=\"graphic\"><style:graphic-properties draw:stroke=\"solid\" "
    "svg:stroke-color=\"#3465a4\" draw:fill=\"solid\" draw:fill-color=\"#729fcf\"/></style:style>"
    "<draw:fill-image draw:name=\"Dots\" xlink:href=\"Pictures/dot.png\"/>"
    "<table:table-template table:name=\"blue\"><table:first-row table:style-name=\"head\"/>"
    "<table:body table:style-name=\"cell\"/></table:table-template>"
    "<style:style style:name=\"head\" style:family=\"table-cell\"><style:graphic-properties draw:fill=\"solid\" "
    "draw:fill-color=\"#5983b0\"/><style:paragraph-properties fo:border=\"0.03cm solid #ffffff\"/>"
    "<style:text-properties fo:color=\"#ffffff\"/></style:style>"
    "<style:style style:name=\"cell\" style:family=\"table-cell\"><style:graphic-properties draw:fill=\"solid\" "
    "draw:fill-color=\"#dee6ef\"/></style:style>"
    "</office:styles>"
    "<office:automatic-styles>"
    "<style:page-layout style:name=\"PM1\"><style:page-layout-properties fo:page-width=\"25.4cm\" "
    "fo:page-height=\"19.05cm\"/></style:page-layout>"
    "<style:style style:name=\"Mdp1\" style:family=\"drawing-page\"><style:drawing-page-properties draw:fill=\"solid\" "
    "draw:fill-color=\"#102030\" presentation:display-footer=\"true\" presentation:display-page-number=\"true\"/>"
    "</style:style>"
    "<style:style style:name=\"Mgr1\" style:family=\"graphic\" style:parent-style-name=\"standard\">"
    "<style:graphic-properties draw:stroke=\"none\" draw:fill-color=\"#ff0000\" draw:opacity=\"20%\"/></style:style>"
    "<style:style style:name=\"Mgr2\" style:family=\"graphic\" style:parent-style-name=\"standard\">"
    "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"bitmap\" draw:fill-image-name=\"Dots\" "
    "style:repeat=\"repeat\" draw:fill-image-width=\"0.254cm\" draw:fill-image-height=\"0.254cm\" "
    "draw:shadow=\"visible\" draw:shadow-opacity=\"50%\"/></style:style>"
    "</office:automatic-styles>"
    "<office:master-styles><style:master-page style:name=\"Deep\" style:page-layout-name=\"PM1\" "
    "draw:style-name=\"Mdp1\">"
    "<draw:rect draw:style-name=\"Mgr1\" svg:x=\"0cm\" svg:y=\"0cm\" svg:width=\"2.54cm\" svg:height=\"2.54cm\"/>"
    "<draw:custom-shape draw:style-name=\"Mgr2\" svg:x=\"2.54cm\" svg:y=\"0cm\" svg:width=\"2.54cm\" "
    "svg:height=\"2.54cm\"><draw:enhanced-geometry svg:viewBox=\"0 0 21600 21600\" draw:type=\"ellipse\" "
    "draw:enhanced-path=\"U 10800 10800 10800 10800 0 360 Z N\"/></draw:custom-shape>"
    "<draw:frame presentation:style-name=\"Deep-title\" svg:x=\"1cm\" svg:y=\"1cm\" svg:width=\"20cm\" "
    "svg:height=\"3cm\" presentation:class=\"title\" presentation:placeholder=\"true\"><draw:text-box/></draw:frame>"
    "<draw:frame presentation:style-name=\"Deep-outline1\" svg:x=\"2cm\" svg:y=\"5cm\" svg:width=\"21cm\" "
    "svg:height=\"12cm\" presentation:class=\"outline\" presentation:placeholder=\"true\"><draw:text-box/>"
    "</draw:frame>"
    "<draw:frame presentation:style-name=\"Deep-title\" svg:x=\"1cm\" svg:y=\"18cm\" svg:width=\"10cm\" "
    "svg:height=\"1cm\" presentation:class=\"footer\"><draw:text-box><text:p><presentation:footer/></text:p>"
    "</draw:text-box></draw:frame>"
    "<draw:frame presentation:style-name=\"Deep-title\" svg:x=\"20cm\" svg:y=\"18cm\" svg:width=\"4cm\" "
    "svg:height=\"1cm\" presentation:class=\"page-number\"><draw:text-box><text:p><text:page-number>&lt;number&gt;"
    "</text:page-number></text:p></draw:text-box></draw:frame>"
    "</style:master-page></office:master-styles>"
    "</office:document-styles>";

static const gchar CONTENT[] =
    "<office:document-content " NS ">"
    "<office:automatic-styles>"
    "<style:style style:name=\"Deep-title\" style:family=\"presentation\"><style:graphic-properties "
    "draw:stroke=\"none\" draw:fill=\"none\" draw:textarea-vertical-align=\"middle\" style:shrink-to-fit=\"true\"/>"
    "<style:paragraph-properties fo:text-align=\"center\"/><style:text-properties fo:font-size=\"40pt\" "
    "fo:font-weight=\"bold\"/></style:style>"
    "<style:style style:name=\"Deep-outline1\" style:family=\"presentation\"><style:graphic-properties "
    "draw:stroke=\"none\" draw:fill=\"none\"><text:list-style style:name=\"Deep-outline1\">"
    "<text:list-level-style-bullet text:level=\"1\" text:bullet-char=\"●\"><style:list-level-properties "
    "text:space-before=\"0.3cm\" text:min-label-width=\"0.9cm\"/><style:text-properties fo:font-size=\"45%\"/>"
    "</text:list-level-style-bullet><text:list-level-style-bullet text:level=\"2\" text:bullet-char=\"–\">"
    "<style:list-level-properties text:space-before=\"1.5cm\" text:min-label-width=\"0.9cm\"/>"
    "</text:list-level-style-bullet></text:list-style></style:graphic-properties>"
    "<style:paragraph-properties fo:margin-top=\"0.5cm\"/><style:text-properties fo:font-size=\"32pt\" "
    "fo:color=\"#eeeeee\"/></style:style>"
    "<style:style style:name=\"Deep-outline2\" style:family=\"presentation\" style:parent-style-name=\"Deep-outline1\">"
    "<style:text-properties fo:font-size=\"80%\"/></style:style>"
    "<style:style style:name=\"pr1\" style:family=\"presentation\" style:parent-style-name=\"Deep-title\"/>"
    "<style:style style:name=\"pr2\" style:family=\"presentation\" style:parent-style-name=\"Deep-outline1\"/>"
    "<style:style style:name=\"T1\" style:family=\"text\"><style:text-properties fo:font-style=\"italic\" "
    "fo:color=\"#ffcc00\" style:text-position=\"super 58%\"/></style:style>"
    "<style:style style:name=\"gr1\" style:family=\"graphic\" style:parent-style-name=\"standard\">"
    "<style:graphic-properties draw:fill-color=\"#000080\" draw:stroke=\"dash\" svg:stroke-width=\"0.1cm\"/>"
    "</style:style>"
    "<style:style style:name=\"gr2\" style:family=\"graphic\" style:parent-style-name=\"standard\"/>"
    "<style:style style:name=\"gr3\" style:family=\"graphic\"><style:graphic-properties draw:stroke=\"none\" "
    "draw:fill=\"none\"/></style:style>"
    "<text:list-style style:name=\"N1\"><text:list-level-style-number text:level=\"1\" style:num-format=\"i\" "
    "style:num-suffix=\".\" text:start-value=\"3\"><style:list-level-properties text:min-label-width=\"1cm\"/>"
    "</text:list-level-style-number></text:list-style>"
    "</office:automatic-styles>"
    "<office:body><office:presentation>"
    "<presentation:footer-decl presentation:name=\"ftr1\">Lookin talks</presentation:footer-decl>"
    "<draw:page draw:name=\"one\" draw:master-page-name=\"Deep\" presentation:use-footer-name=\"ftr1\">"
    "<draw:frame presentation:style-name=\"pr1\" svg:x=\"1cm\" svg:y=\"1cm\" svg:width=\"20cm\" svg:height=\"3cm\" "
    "presentation:class=\"title\"><draw:text-box><text:p>Deep <text:span text:style-name=\"T1\">dive</text:span>"
    "</text:p></draw:text-box></draw:frame>"
    "<draw:frame presentation:style-name=\"pr2\" svg:x=\"2cm\" svg:y=\"5cm\" svg:width=\"21cm\" svg:height=\"12cm\" "
    "presentation:class=\"outline\"><draw:text-box><text:list><text:list-item><text:p>First   point</text:p>"
    "<text:list><text:list-item><text:p>Detail<text:s text:c=\"2\"/>here</text:p></text:list-item></text:list>"
    "</text:list-item></text:list><text:list text:style-name=\"N1\"><text:list-item><text:p>Third</text:p>"
    "</text:list-item><text:list-item><text:p>Fourth</text:p></text:list-item></text:list>"
    "<text:list text:style-name=\"N1\" text:continue-numbering=\"true\"><text:list-item><text:p>Fifth</text:p>"
    "</text:list-item></text:list><text:list text:style-name=\"N1\"><text:list-item><text:p>Again</text:p>"
    "</text:list-item></text:list></draw:text-box>"
    "</draw:frame>"
    "<draw:custom-shape draw:style-name=\"gr1\" svg:width=\"4cm\" svg:height=\"2cm\" "
    "draw:transform=\"rotate (-1.5707963267949) translate (10cm 2cm)\"><text:p>Turned</text:p>"
    "<draw:enhanced-geometry svg:viewBox=\"0 0 21600 21600\" draw:type=\"isosceles-triangle\" "
    "draw:modifiers=\"5400\" draw:enhanced-path=\"M ?f0 0 L 21600 21600 0 21600 Z N\">"
    "<draw:equation draw:name=\"f0\" draw:formula=\"$0 \"/></draw:enhanced-geometry></draw:custom-shape>"
    "<draw:path draw:style-name=\"gr2\" svg:x=\"1cm\" svg:y=\"15cm\" svg:width=\"3cm\" svg:height=\"1cm\" "
    "svg:viewBox=\"0 0 3000 1000\" svg:d=\"M0 1000c1000-1000 2000-1000 3000 0\"/>"
    "<draw:line draw:style-name=\"gr2\" svg:x1=\"5cm\" svg:y1=\"16cm\" svg:x2=\"3cm\" svg:y2=\"15cm\"/>"
    "<draw:frame draw:style-name=\"gr2\" svg:x=\"12cm\" svg:y=\"10cm\" svg:width=\"8cm\" svg:height=\"2cm\">"
    "<table:table table:template-name=\"blue\" table:use-first-row-styles=\"true\">"
    "<table:table-column table:number-columns-repeated=\"2\"/>"
    "<table:table-row><table:table-cell table:number-columns-spanned=\"2\"><text:p>Head</text:p></table:table-cell>"
    "<table:covered-table-cell/></table:table-row>"
    "<table:table-row><table:table-cell><text:p>a</text:p></table:table-cell><table:table-cell><text:p>b</text:p>"
    "</table:table-cell></table:table-row></table:table></draw:frame>"
    "<draw:frame draw:style-name=\"gr3\" svg:x=\"20cm\" svg:y=\"14cm\" svg:width=\"2cm\" svg:height=\"2cm\">"
    "<draw:image xlink:href=\"Pictures/chart.svm\"/><draw:image xlink:href=\"Pictures/dot.png\"/></draw:frame>"
    "<presentation:notes><draw:page-thumbnail/><draw:frame presentation:class=\"notes\"><draw:text-box>"
    "<text:p>Say hello</text:p></draw:text-box></draw:frame></presentation:notes>"
    "</draw:page>"
    "</office:presentation></office:body></office:document-content>";

static gchar* write_odp(const gchar* content, const gchar* styles) {
    ZipBuilder* zb = zip_builder_new(FALSE);
    zip_builder_add(zb, "mimetype", "application/vnd.oasis.opendocument.presentation", FALSE);
    zip_builder_add(zb, "content.xml", content, TRUE);
    if (styles != NULL) {
        zip_builder_add(zb, "styles.xml", styles, TRUE);
    }
    zip_builder_add_full(zb, "Pictures/dot.png", PNG, sizeof PNG, FALSE, 0);
    GBytes* bytes = zip_builder_finish(zb);
    gchar* path = NULL;
    gint fd = g_file_open_tmp("odp-XXXXXX.odp", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(g_file_set_contents(path, g_bytes_get_data(bytes, NULL), (gssize)g_bytes_get_size(bytes), NULL));
    g_bytes_unref(bytes);
    return path;
}

static const SlideShape* shape_with_text(const Slide* slide, const gchar* prefix) {
    for (guint i = 0; i < slide->shapes->len; i++) {
        const SlideShape* s = g_ptr_array_index(slide->shapes, i);
        gchar* text = slide_shape_text(s);
        gboolean match = g_str_has_prefix(text, prefix);
        g_free(text);
        if (match) {
            return s;
        }
    }
    return NULL;
}

static const SlideParagraph* para(const SlideShape* s, guint i) {
    return g_ptr_array_index(s->paragraphs, i);
}

static const SlideRun* run(const SlideShape* s, guint p, guint r) {
    return g_ptr_array_index(para(s, p)->runs, r);
}

/* ---- Values and styles ---- */

static void test_values(void) {
    g_assert_cmpfloat_with_epsilon(odp_length("2.54cm", 0), 72, 1e-9);
    g_assert_cmpfloat_with_epsilon(odp_length("25.4mm", 0), 72, 1e-9);
    g_assert_cmpfloat_with_epsilon(odp_length("1in", 0), 72, 1e-9);
    g_assert_cmpfloat(odp_length("12pt", 0), ==, 12);
    g_assert_cmpfloat(odp_length("wide", 5), ==, 5);
    g_assert_cmpfloat(odp_length(NULL, 7), ==, 7);
    g_assert_cmphex(odp_color("#1a2B3c", 0), ==, 0x1A2B3C);
    g_assert_cmphex(odp_color("red", 9), ==, 9);
    g_assert_cmpfloat(odp_percent("45%"), ==, 0.45);
    g_assert_cmpfloat(odp_percent("12pt"), ==, -1);
    gchar* family = odp_first_family("'Noto Sans', Arial");
    g_assert_cmpstr(family, ==, "Noto Sans");
    g_free(family);
}

static void test_styles(void) {
    GError* error = NULL;
    g_autoptr(OdpNode) root = odp_node_parse_text(STYLES, -1, &error);
    g_assert_no_error(error);
    g_autoptr(OdpNode) content = odp_node_parse_text(CONTENT, -1, &error);
    g_assert_no_error(error);
    g_autoptr(OdpStyles) styles = odp_styles_new();
    odp_styles_add(styles, root);
    odp_styles_add(styles, content);
    gdouble w = 0, h = 0;
    g_assert_true(odp_styles_page_size(styles, "Deep", &w, &h));
    g_assert_cmpfloat_with_epsilon(w, 720, 1e-9);
    g_assert_cmpfloat_with_epsilon(h, 540, 1e-9);
    g_assert_cmpstr(odp_styles_font(styles, "Body1"), ==, "DejaVu Sans");
    g_assert_cmpstr(styles->first_master, ==, "Deep");
    g_assert_cmpstr(odp_styles_template_part(styles, "blue", "first-row"), ==, "head");

    /* Parents, then the defaults; relative sizes */
    OdpCascade c = { { 0 }, 0 };
    odp_cascade_add(&c, styles, "presentation", "Deep-outline2");
    odp_cascade_add_default(&c, styles, "graphic");
    g_assert_cmpfloat_with_epsilon(odp_cascade_font_size(&c, 18), 25.6, 1e-9);
    g_assert_cmpstr(odp_cascade_get(&c, "fo:color"), ==, "#eeeeee");
    g_assert_cmpstr(odp_cascade_get(&c, "style:font-name"), ==, "Body1");
    g_assert_null(odp_cascade_get_explicit(&c, "style:font-name"));
    const OdpList* list = odp_styles_style_list(styles, "presentation", "Deep-outline2");
    g_assert_nonnull(list);
    g_assert_cmpstr(list->levels[1].bullet, ==, "–");
    g_assert_cmpfloat(list->levels[0].size_percent, ==, 0.45);

    gchar* text = odp_node_text(odp_node_find(content, "presentation:notes"));
    g_assert_cmpstr(text, ==, "Say hello");
    g_free(text);
}

/* ---- Geometry ---- */

static void test_svg_path(void) {
    GArray* steps = odp_geometry_svg_path("M10 20 h30 v10 l-5 5 c1 1 2 2 3 3 z", 10, 20);
    g_assert_nonnull(steps);
    g_assert_cmpuint(steps->len, ==, 6);
    const SlidePathStep* s = &g_array_index(steps, SlidePathStep, 2);
    g_assert_cmpint(s->op, ==, SLIDE_PATH_LINE);
    g_assert_cmpfloat(s->x[0], ==, 30);
    g_assert_cmpfloat(s->y[0], ==, 10);
    s = &g_array_index(steps, SlidePathStep, 4);
    g_assert_cmpint(s->op, ==, SLIDE_PATH_CURVE);
    g_assert_cmpfloat(s->x[2], ==, 28);
    g_assert_cmpint(g_array_index(steps, SlidePathStep, 5).op, ==, SLIDE_PATH_CLOSE);
    g_array_free(steps, TRUE);

    /* An arc ends where it says, numbers without separators */
    steps = odp_geometry_svg_path("M0 0A50 50 0 0 1 100 0", 0, 0);
    const SlidePathStep* last = &g_array_index(steps, SlidePathStep, steps->len - 1);
    g_assert_cmpfloat_with_epsilon(last->x[2], 100, 1e-6);
    g_assert_cmpfloat_with_epsilon(last->y[2], 0, 1e-6);
    g_array_free(steps, TRUE);
    steps = odp_geometry_svg_path("M0 2094c2342 0 2287-2094 2287-2094", 0, 0);
    g_assert_cmpfloat(g_array_index(steps, SlidePathStep, 1).y[1], ==, -2094 + 2094);
    g_array_free(steps, TRUE);
    g_assert_null(odp_geometry_svg_path("M5 5", 0, 0));

    steps = odp_geometry_points("0,5726 5646,0 11373,5648", 0, 0, TRUE);
    g_assert_cmpuint(steps->len, ==, 4);
    g_array_free(steps, TRUE);
    g_assert_null(odp_geometry_points("1,1", 0, 0, TRUE));

    gdouble box[4];
    g_assert_true(odp_geometry_viewbox("0 0 21600 21600", box));
    g_assert_false(odp_geometry_viewbox("0 0 0 10", box));
}

static void test_enhanced(void) {
    static const gchar* const names[] = { "f0", "f1", "f2" };
    static const gchar* const formulas[] = { "$0 *2", "?f0 +if(-1, 1, 10)", "max(abs(-3), sqrt(16))+?f2" };
    OdpEnhanced g = { "M ?f1 0 L 21600 21600 0 21600 Z N", "100 7", names, formulas, 3,
                      { 0, 0, 21600, 21600 }, 72, 36 };
    g_assert_cmpfloat(odp_geometry_formula(&g, "?f1"), ==, 210);
    g_assert_cmpfloat(odp_geometry_formula(&g, "$1 *(2+3)-right/21600"), ==, 34);
    g_assert_cmpfloat(odp_geometry_formula(&g, "?f2"), ==, 4);   /* a cycle counts as 0 */
    g_assert_cmpfloat_with_epsilon(odp_geometry_formula(&g, "logwidth"), 2540, 1e-9);
    GArray* steps = odp_geometry_enhanced(&g);
    g_assert_cmpuint(steps->len, ==, 4);
    g_assert_cmpfloat(g_array_index(steps, SlidePathStep, 0).x[0], ==, 210);
    g_array_free(steps, TRUE);

    /* A full ellipse: four quarter curves back to the start */
    OdpEnhanced e = { "U 10800 10800 10800 10800 0 360 Z N", NULL, NULL, NULL, 0, { 0, 0, 21600, 21600 }, 72, 72 };
    steps = odp_geometry_enhanced(&e);
    g_assert_cmpuint(steps->len, ==, 6);
    g_assert_cmpfloat_with_epsilon(g_array_index(steps, SlidePathStep, 0).x[0], 21600, 1e-6);
    g_assert_cmpfloat_with_epsilon(g_array_index(steps, SlidePathStep, 4).x[2], 21600, 1e-6);
    g_assert_cmpfloat_with_epsilon(g_array_index(steps, SlidePathStep, 1).y[2], 0, 1e-6);  /* counter-clockwise */
    g_array_free(steps, TRUE);

    /* Arcs and quadrants */
    OdpEnhanced a = { "M 0 10 X 10 0 Y 20 10 W 0 0 20 20 20 10 0 10", NULL, NULL, NULL, 0, { 0, 0, 20, 20 }, 1, 1 };
    steps = odp_geometry_enhanced(&a);
    /* X leaves horizontally and arrives vertically */
    g_assert_cmpfloat(g_array_index(steps, SlidePathStep, 1).y[0], ==, 10);
    g_assert_cmpfloat(g_array_index(steps, SlidePathStep, 1).x[1], ==, 10);
    const SlidePathStep* end = &g_array_index(steps, SlidePathStep, steps->len - 1);
    g_assert_cmpfloat_with_epsilon(end->x[2], 0, 1e-6);
    g_assert_cmpfloat_with_epsilon(end->y[2], 10, 1e-6);
    g_array_free(steps, TRUE);
}

/* ---- Decks ---- */

static void test_fixture(void) {
    /* No styles, no positions: Impress' layout and sizes */
    GError* error = NULL;
    g_autoptr(SlideDeck) deck = odp_read_deck("tests/fixtures/preview/roadmap.odp", &error);
    g_assert_no_error(error);
    g_assert_cmpuint(deck->slides->len, ==, 2);
    const Slide* s1 = g_ptr_array_index(deck->slides, 0);
    g_assert_cmpstr(s1->title, ==, "Lookin roadmap");
    const SlideShape* title = shape_with_text(s1, "Lookin roadmap");
    g_assert_cmpfloat(run(title, 0, 0)->size, ==, 44);
    g_assert_cmpfloat(title->w, >, deck->width / 2);
    const Slide* s2 = g_ptr_array_index(deck->slides, 1);
    g_assert_cmpstr(s2->notes, ==, "Speaker notes are hidden");
    const SlideShape* body = shape_with_text(s2, "Archives");
    g_assert_cmpuint(body->paragraphs->len, ==, 2);
    g_assert_cmpstr(para(body, 0)->bullet, ==, "•");
    g_assert_cmpfloat(body->y, >, title->y);
}

static void test_deck(void) {
    gchar* path = write_odp(CONTENT, STYLES);
    GError* error = NULL;
    g_autoptr(SlideDeck) deck = odp_read_deck(path, &error);
    g_assert_no_error(error);
    g_assert_cmpfloat_with_epsilon(deck->width, 720, 1e-9);
    g_assert_cmpfloat_with_epsilon(deck->height, 540, 1e-9);
    g_assert_cmpstr(deck->font, ==, "DejaVu Sans");
    const Slide* s = g_ptr_array_index(deck->slides, 0);
    g_assert_cmpstr(s->title, ==, "Deep dive");
    g_assert_cmpstr(s->notes, ==, "Say hello");
    g_assert_cmphex(s->background, ==, 0x102030);   /* the master's */

    /* The master's shapes first, without its placeholders */
    const SlideShape* square = g_ptr_array_index(s->shapes, 0);
    g_assert_cmphex(square->fill, ==, 0xFF0000);
    g_assert_cmpfloat(square->fill_opacity, ==, 0.2);
    g_assert_cmphex(square->line, ==, SLIDE_NO_COLOR);
    g_assert_cmpfloat_with_epsilon(square->w, 72, 1e-9);
    const SlideShape* dots = g_ptr_array_index(s->shapes, 1);
    g_assert_cmpint(dots->geometry, ==, SLIDE_GEOM_ELLIPSE);
    g_assert_nonnull(dots->picture);
    g_assert_cmpfloat_with_epsilon(dots->picture_tile_w, 7.2, 1e-9);
    g_assert_cmphex(dots->shadow, ==, 0x808080);
    g_assert_cmpfloat(dots->shadow_opacity, ==, 0.5);
    /* Fields: the footer text and the page number */
    g_assert_nonnull(shape_with_text(s, "Lookin talks"));
    g_assert_nonnull(shape_with_text(s, "1"));

    /* Title: presentation style, span, automatic colour on the dark page */
    const SlideShape* title = shape_with_text(s, "Deep");
    g_assert_cmpint(title->anchor, ==, SLIDE_ANCHOR_MIDDLE);
    g_assert_true(title->shrink_to_fit);
    g_assert_cmpint(para(title, 0)->align, ==, SLIDE_ALIGN_CENTER);
    g_assert_cmpstr(run(title, 0, 0)->text, ==, "Deep ");
    g_assert_cmpfloat(run(title, 0, 0)->size, ==, 40);
    g_assert_true(run(title, 0, 0)->bold);
    g_assert_cmpstr(run(title, 0, 0)->font, ==, "DejaVu Sans");
    g_assert_cmphex(run(title, 0, 0)->color, ==, 0xFFFFFF);
    g_assert_true(run(title, 0, 1)->italic);
    g_assert_cmphex(run(title, 0, 1)->color, ==, 0xFFCC00);
    g_assert_cmpint(run(title, 0, 1)->baseline, ==, 1);

    /* Outline: list levels from the presentation styles, numbers */
    const SlideShape* body = shape_with_text(s, "First");
    g_assert_cmpuint(body->paragraphs->len, ==, 6);
    g_assert_cmpstr(run(body, 0, 0)->text, ==, "First point");
    g_assert_cmpstr(para(body, 0)->bullet, ==, "●");
    g_assert_cmpfloat(para(body, 0)->bullet_scale, ==, 0.45);
    g_assert_cmpfloat_with_epsilon(para(body, 0)->left, 1.2 * 72 / 2.54, 1e-9);
    g_assert_cmpfloat_with_epsilon(para(body, 0)->indent, -0.9 * 72 / 2.54, 1e-9);
    g_assert_cmpfloat(run(body, 0, 0)->size, ==, 32);
    g_assert_cmphex(run(body, 0, 0)->color, ==, 0xEEEEEE);
    g_assert_cmpstr(para(body, 1)->bullet, ==, "–");
    g_assert_cmpfloat_with_epsilon(run(body, 1, 0)->size, 25.6, 1e-9);
    g_assert_cmpstr(run(body, 1, 1)->text, ==, "  ");
    g_assert_cmpstr(para(body, 2)->bullet, ==, "iii.");
    g_assert_cmpstr(para(body, 3)->bullet, ==, "iv.");
    g_assert_cmpstr(para(body, 4)->bullet, ==, "v.");     /* continued */
    g_assert_cmpstr(para(body, 5)->bullet, ==, "iii.");   /* restarted */

    /* A custom shape turned a quarter clockwise, its path evaluated */
    const SlideShape* turned = shape_with_text(s, "Turned");
    g_assert_cmpfloat_with_epsilon(turned->rotation, 90, 1e-6);
    g_assert_cmpfloat_with_epsilon(turned->x + turned->w / 2, 9 * 72 / 2.54, 1e-6);
    g_assert_cmpint(turned->geometry, ==, SLIDE_GEOM_PATH);
    g_assert_cmpfloat(g_array_index(turned->path, SlidePathStep, 0).x[0], ==, 5400);
    g_assert_cmphex(turned->fill, ==, 0x000080);
    g_assert_true(turned->line_dashed);
    g_assert_cmpfloat_with_epsilon(turned->line_width, 0.1 * 72 / 2.54, 1e-9);

    /* Open path and line: not filled; the line keeps its direction */
    const SlideShape* curve = g_ptr_array_index(s->shapes, 7);
    g_assert_cmpint(curve->geometry, ==, SLIDE_GEOM_PATH);
    g_assert_cmphex(curve->fill, ==, SLIDE_NO_COLOR);
    g_assert_cmphex(curve->line, ==, 0x3465A4);
    const SlideShape* line = g_ptr_array_index(s->shapes, 8);
    g_assert_cmpint(line->geometry, ==, SLIDE_GEOM_LINE);
    g_assert_true(line->flip_h && line->flip_v);
    g_assert_cmphex(line->fill, ==, SLIDE_NO_COLOR);

    /* Table: the template's styles, spans, no frame fill */
    const SlideShape* table = shape_with_text(s, "Head");
    g_assert_nonnull(table->table);
    g_assert_cmphex(table->fill, ==, SLIDE_NO_COLOR);
    g_assert_cmpuint(table->table->rows, ==, 2);
    g_assert_cmpuint(table->table->cols, ==, 2);
    const SlideCell* head = slide_table_cell(table->table, 0, 0);
    g_assert_cmpuint(head->col_span, ==, 2);
    g_assert_true(slide_table_cell(table->table, 0, 1)->merged);
    g_assert_cmphex(head->fill, ==, 0x5983B0);
    g_assert_cmphex(table->table->border, ==, 0xFFFFFF);
    const SlideParagraph* hp = g_ptr_array_index(head->paragraphs, 0);
    g_assert_cmphex(((SlideRun*)g_ptr_array_index(hp->runs, 0))->color, ==, 0xFFFFFF);
    const SlideCell* body_cell = slide_table_cell(table->table, 1, 0);
    g_assert_cmphex(body_cell->fill, ==, 0xDEE6EF);
    hp = g_ptr_array_index(body_cell->paragraphs, 0);
    g_assert_cmphex(((SlideRun*)g_ptr_array_index(hp->runs, 0))->color, ==, 0x000000);
    g_assert_cmpfloat_with_epsilon(table->table->col_widths[0], 4 * 72 / 2.54, 1e-9);

    /* Picture: the PNG, not the metafile */
    const SlideShape* pic = g_ptr_array_index(s->shapes, s->shapes->len - 1);
    g_assert_nonnull(pic->picture);
    g_assert_cmpuint(g_bytes_get_size(pic->picture), ==, sizeof PNG);

    /* Every slide can be drawn */
    GBytes* png = slide_render_png(deck, s, 320);
    g_assert_nonnull(png);
    g_bytes_unref(png);
    g_unlink(path);
    g_free(path);
}

static void test_preview(void) {
    guint n = 0;
    GError* error = NULL;
    g_autoptr(PreviewDoc) doc = odp_file_preview("tests/fixtures/preview/roadmap.odp", "roadmap.odp", "3 kB", &n,
                                                 1.0, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(n, ==, 2);
    g_assert_true(g_str_has_prefix(doc->text->str, "roadmap.odp\nOpenDocument presentation · 2 slides · 3 kB"));
    g_assert_cmpuint(doc->images->len, ==, 2);
    g_assert_nonnull(strstr(doc->text->str, "Slide 2 · Previews"));
}

static void test_errors(void) {
    GError* error = NULL;
    g_assert_null(odp_read_deck("tests/fixtures/preview/sample.zip", &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    gchar* path = write_odp("<office:document-content " NS "><office:body><office:text/></office:body>"
                            "</office:document-content>", NULL);
    g_assert_null(odp_read_deck(path, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_unlink(path);
    g_free(path);
    path = write_odp("<office:document-content", NULL);
    g_assert_null(odp_read_deck(path, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_unlink(path);
    g_free(path);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/odp/values", test_values);
    g_test_add_func("/odp/styles", test_styles);
    g_test_add_func("/odp/svg_path", test_svg_path);
    g_test_add_func("/odp/enhanced", test_enhanced);
    g_test_add_func("/odp/fixture", test_fixture);
    g_test_add_func("/odp/deck", test_deck);
    g_test_add_func("/odp/preview", test_preview);
    g_test_add_func("/odp/errors", test_errors);
    return g_test_run();
}
