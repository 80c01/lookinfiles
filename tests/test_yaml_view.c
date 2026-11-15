/* Unit tests for src/formats/yaml_view.c (YAML preview). */
#include "formats/yaml_view.h"
#include "formats/text_outline.h"

#include <string.h>

static PreviewDoc* view(const gchar* yaml, gboolean cut, const PreviewFold* fold, YamlViewInfo* info) {
    PreviewDoc* doc = preview_doc_new();
    GError* error = NULL;
    gboolean ok = yaml_view_append(doc, yaml, strlen(yaml), cut, fold, info, &error);
    g_assert_no_error(error);
    g_assert_true(ok);
    return doc;
}

/* Path of the value on the line holding text */
static const gchar* path_of(const PreviewDoc* doc, const gchar* text) {
    const gchar* at = strstr(doc->text->str, text);
    g_assert_nonnull(at);
    return preview_doc_path_at(doc, (gint)g_utf8_pointer_to_offset(doc->text->str, at));
}

/* The text styled with style, joined by "|" */
static gchar* styled(const PreviewDoc* doc, PreviewStyle style) {
    GString* out = g_string_new(NULL);
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* span = &g_array_index(doc->spans, PreviewSpan, i);
        if (span->style == style) {
            const gchar* a = g_utf8_offset_to_pointer(doc->text->str, span->start);
            const gchar* b = g_utf8_offset_to_pointer(doc->text->str, span->end);
            g_string_append_printf(out, "%s%.*s", out->len > 0 ? "|" : "", (gint)(b - a), a);
        }
    }
    return g_string_free(out, FALSE);
}

static gchar* fold_ids(const PreviewDoc* doc) {
    GString* out = g_string_new(NULL);
    for (guint i = 0; i < doc->folds->len; i++) {
        g_string_append_printf(out, "%s%s", i > 0 ? " " : "", g_array_index(doc->folds, PreviewFoldPoint, i).id);
    }
    return g_string_free(out, FALSE);
}

static const gchar* CI =
    "# CI\n"
    "name: build & test\n"
    "on:\n"
    "  push:\n"
    "    branches: [main, \"release/*\"]\n"
    "defaults: &defaults\n"
    "  timeout: 10.5\n"
    "  retry: ~\n"
    "jobs:\n"
    "  build:\n"
    "    <<: *defaults\n"
    "    steps:\n"
    "    - uses: actions/checkout@v4\n"
    "    - name: Build\n"
    "      run: |\n"
    "        make\n"
    "\n"
    "        make check # not a comment\n"
    "    - name: 'It''s done'\n"
    "      if: false\n"
    "  docs:\n"
    "    summary: >-\n"
    "      Builds the guide\n"
    "    needs:\n"
    "      - build\n";

static void test_text_and_styles(void) {
    YamlViewInfo info;
    g_autoptr(PreviewDoc) doc = view(CI, FALSE, NULL, &info);
    g_assert_cmpstr(doc->text->str, ==, CI); /* as written */
    g_assert_cmpuint(info.n_documents, ==, 1);
    g_assert_cmpint(info.root, ==, 'm');
    g_assert_cmpuint(info.n_root, ==, 4);
    g_autofree gchar* keys = styled(doc, PREVIEW_STYLE_KEY);
    g_assert_cmpstr(keys, ==, "name|on|push|branches|defaults|timeout|retry|jobs|build|<<|steps|uses|name|run|name|"
                              "if|docs|summary|needs");
    g_autofree gchar* strings = styled(doc, PREVIEW_STYLE_STRING);
    g_assert_cmpstr(strings, ==, "build & test|main|\"release/*\"|actions/checkout@v4|Build|make|"
                                 "make check # not a comment|'It''s done'|Builds the guide|build");
    g_autofree gchar* keywords = styled(doc, PREVIEW_STYLE_KEYWORD);
    g_assert_cmpstr(keywords, ==, "&defaults|~|*defaults|||false|>-");
    g_autofree gchar* numbers = styled(doc, PREVIEW_STYLE_NUMBER);
    g_assert_cmpstr(numbers, ==, "10.5");
    g_autofree gchar* comments = styled(doc, PREVIEW_STYLE_COMMENT);
    g_assert_cmpstr(comments, ==, "# CI");
    /* The root keys and the keys holding a block below them go to the tagbar */
    g_autofree gchar* outline1 = styled(doc, PREVIEW_STYLE_OUTLINE1);
    g_autofree gchar* outline2 = styled(doc, PREVIEW_STYLE_OUTLINE2);
    g_autofree gchar* outline3 = styled(doc, PREVIEW_STYLE_OUTLINE3);
    g_assert_cmpstr(outline1, ==, "name|on|defaults|jobs");
    g_assert_cmpstr(outline2, ==, "push|build|docs");
    g_assert_cmpstr(outline3, ==, "steps|needs");
}

static void test_paths_and_folds(void) {
    g_autoptr(PreviewDoc) doc = view(CI, FALSE, NULL, NULL);
    g_assert_null(path_of(doc, "# CI"));
    g_assert_cmpstr(path_of(doc, "branches"), ==, "on.push.branches");
    g_assert_cmpstr(path_of(doc, "<<"), ==, "jobs.build.<<");
    g_assert_cmpstr(path_of(doc, "- uses"), ==, "jobs.build.steps[0].uses");
    g_assert_cmpstr(path_of(doc, "make check"), ==, "jobs.build.steps[1].run");
    g_assert_cmpstr(path_of(doc, "'It''s"), ==, "jobs.build.steps[2].name");
    g_assert_cmpstr(path_of(doc, "Builds the guide"), ==, "jobs.docs.summary");
    g_assert_cmpstr(path_of(doc, "- build"), ==, "jobs.docs.needs[0]");
    g_autofree gchar* ids = fold_ids(doc);
    g_assert_cmpstr(ids, ==, "on.push on defaults jobs.build.steps[1] jobs.build.steps[2] jobs.build.steps "
                             "jobs.build jobs.docs.needs jobs.docs jobs");

    g_autoptr(PreviewFold) fold = preview_fold_new(2);
    preview_fold_toggle(fold, "on");
    g_autoptr(PreviewDoc) folded = view(CI, FALSE, fold, NULL);
    g_assert_cmpstr(folded->text->str, ==,
                    "# CI\n"
                    "name: build & test\n"
                    "on:  1 key\n"
                    "defaults: &defaults\n"
                    "  timeout: 10.5\n"
                    "  retry: ~\n"
                    "jobs:\n"
                    "  build:  2 keys\n"
                    "  docs:  2 keys\n");
}

static void test_documents_and_sequences(void) {
    /* Kubernetes style: several documents, compact mappings in sequences */
    const gchar* k8s =
        "%YAML 1.2\n"
        "---\n"
        "kind: Service\n"
        "spec:\n"
        "  ports:\n"
        "  - port: 80\n"
        "    targetPort: 8080\n"
        "---\n"
        "kind: Deployment\n"
        "spec:\n"
        "  containers:\n"
        "    - name: web\n"
        "      args: [\"--port\",\n"
        "             \"8080\"]\n"
        "...\n";
    YamlViewInfo info;
    g_autoptr(PreviewDoc) doc = view(k8s, FALSE, NULL, &info);
    g_assert_cmpuint(info.n_documents, ==, 2);
    g_assert_cmpstr(path_of(doc, "kind: Service"), ==, "[0].kind");
    g_assert_cmpstr(path_of(doc, "targetPort"), ==, "[0].spec.ports[0].targetPort");
    g_assert_cmpstr(path_of(doc, "- name: web"), ==, "[1].spec.containers[0].name");
    g_assert_cmpstr(path_of(doc, "\"8080\"]"), ==, "[1].spec.containers[0].args");
    g_autofree gchar* ids = fold_ids(doc);
    g_assert_cmpstr(ids, ==, "[0].spec.ports[0] [0].spec.ports [0].spec [1].spec.containers[0].args "
                             "[1].spec.containers[0] [1].spec.containers [1].spec");
    g_autofree gchar* keywords = styled(doc, PREVIEW_STYLE_KEYWORD);
    g_assert_cmpstr(keywords, ==, "%YAML 1.2|---|---|...");

    /* Ansible style: a root sequence; - - nested; a value on the next line */
    g_autoptr(PreviewDoc) play = view("- hosts: all\n  tasks:\n  - name:\n      install it\n  - - a\n    - b\n",
                                      FALSE, NULL, &info);
    g_assert_cmpint(info.root, ==, 's');
    g_assert_cmpuint(info.n_root, ==, 1);
    g_assert_cmpstr(path_of(play, "install"), ==, "[0].tasks[0].name");
    g_assert_cmpstr(path_of(play, "- - a"), ==, "[0].tasks[1][0]");
    g_assert_cmpstr(path_of(play, "- b"), ==, "[0].tasks[1][1]");
}

static void test_errors(void) {
    const gchar* bad[] = {
        "", "\n  \n", "a:\n\tb: 1\n", "a: 1\n  b: 2\n", "a:\n  b: 1\n c: 2\n", "a: 1\nplain line\n",
        "? complex\n: key\n", "a: \"open\n", "a: [1, 2\n", "a: 'x' trailing\n", "- a\nb: 1\n",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_autoptr(PreviewDoc) doc = preview_doc_new();
        GError* error = NULL;
        g_assert_false(yaml_view_append(doc, bad[i], strlen(bad[i]), FALSE, NULL, NULL, &error));
        g_assert_nonnull(error);
        g_assert_cmpuint(doc->text->len, ==, 0);
        g_error_free(error);
    }
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    GError* error = NULL;
    g_assert_false(yaml_view_append(doc, "a:\n  b: 1\n    c: 2\n", 19, FALSE, NULL, NULL, &error));
    g_assert_cmpstr(error->message, ==, "line 3, column 5: bad indentation");
    g_error_free(error);
    /* Comments only, a scalar document, CRLF line ends, a byte order mark */
    g_autoptr(PreviewDoc) comments = view("# nothing yet\n", FALSE, NULL, NULL);
    YamlViewInfo info;
    g_autoptr(PreviewDoc) scalar = view("just some\ntext\n", FALSE, NULL, &info);
    g_assert_cmpint(info.root, ==, 0);
    g_autoptr(PreviewDoc) crlf = view("a:\r\n  b: 1\r\n", FALSE, NULL, NULL);
    g_assert_cmpstr(crlf->text->str, ==, "a:\n  b: 1\n");
    g_autoptr(PreviewDoc) bom = view("\xEF\xBB\xBF" "a: 1\n", FALSE, NULL, NULL);
    g_assert_cmpstr(path_of(bom, "a: 1"), ==, "a");
}

static void test_cut(void) {
    YamlViewInfo info;
    g_autoptr(PreviewDoc) line = view("a:\n  b: 1\n  c: lo", TRUE, NULL, &info);
    g_assert_cmpstr(line->text->str, ==, "a:\n  b: 1\n…\n");
    g_assert_false(info.cut);
    g_autoptr(PreviewDoc) flow = view("a:\n  list: [1,\n    2,\n    3", TRUE, NULL, &info);
    g_assert_cmpstr(flow->text->str, ==, "a:\n  list: [1,\n    2,\n…\n");
    g_assert_true(info.cut);
    g_assert_nonnull(preview_doc_fold_find(flow, "a"));
}

static void test_auto_level(void) {
    /* 2,000 items of 6 lines: 12,000 lines open; their maps folded: 4,000 */
    GString* big = g_string_new("items:\n");
    for (gint i = 0; i < 2000; i++) {
        g_string_append_printf(big, "- id: %d\n  meta:\n    a: 1\n    b: 2\n    c: 3\n  done: true\n", i);
    }
    YamlViewInfo info;
    g_autoptr(PreviewDoc) doc = view(big->str, FALSE, NULL, &info);
    g_assert_cmpint(info.level, ==, 3);
    g_assert_true(g_str_has_prefix(doc->text->str, "Long document"));
    g_assert_true(preview_doc_fold_find(doc, "items[7].meta")->folded);
    g_assert_false(preview_doc_fold_find(doc, "items[7]")->folded);
    g_string_free(big, TRUE);
}

static void test_build(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(yaml_view_build(doc, "a: 1\nb: 2\n", 10, FALSE, NULL, "Note.", NULL));
    g_assert_cmpstr(doc->text->str, ==, "YAML · 2 keys\nNote.\n\na: 1\nb: 2\n");
    g_assert_true(doc->monospace);
    g_autoptr(PreviewDoc) docs = preview_doc_new();
    g_assert_true(yaml_view_build(docs, "a: 1\n---\nb: 2\n", 14, FALSE, NULL, NULL, NULL));
    g_assert_true(g_str_has_prefix(docs->text->str, "YAML · 2 documents\n"));
    g_autoptr(PreviewDoc) items = preview_doc_new();
    g_assert_true(yaml_view_build(items, "- 1\n", 4, FALSE, NULL, NULL, NULL));
    g_assert_true(g_str_has_prefix(items->text->str, "YAML · 1 item\n"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/yaml_view/text_and_styles", test_text_and_styles);
    g_test_add_func("/yaml_view/paths_and_folds", test_paths_and_folds);
    g_test_add_func("/yaml_view/documents_and_sequences", test_documents_and_sequences);
    g_test_add_func("/yaml_view/errors", test_errors);
    g_test_add_func("/yaml_view/cut", test_cut);
    g_test_add_func("/yaml_view/auto_level", test_auto_level);
    g_test_add_func("/yaml_view/build", test_build);
    return g_test_run();
}
