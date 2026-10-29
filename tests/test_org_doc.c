/* Unit tests for src/org_doc.c: Org-mode headings, structure editing, TODO
 * keywords, priorities, tags, planning, clock, lists, checkboxes,
 * statistics cookies, links and templates. "‸" marks the cursor. */
#include "org_doc.h"
#include "org_test_util.h"

typedef gboolean (*Op)(GPtrArray* lines, OrgPos* pos, gpointer data);

/* Run op on before; check the text (with cursor) after it */
static void check(const gchar* before, Op op, gpointer data, gboolean ok, const gchar* after) {
    OrgPos pos;
    GPtrArray* lines = doc_new(before, &pos);
    gboolean r = op(lines, &pos, data);
    g_assert_cmpint(r, ==, ok);
    gchar* got = doc_text(lines, &pos);
    g_assert_cmpstr(got, ==, after);
    g_free(got);
}

static GDateTime* fixed_now(void) {
    GTimeZone* tz = g_time_zone_new_utc();
    GDateTime* dt = g_date_time_new(tz, 2026, 10, 29, 9, 30, 0);
    g_time_zone_unref(tz);
    return dt;
}

/* ---- Operations wrapped for check() ---- */

static gboolean op_promote(GPtrArray* l, OrgPos* p, gpointer d) {
    return org_promote(l, p, -1, GPOINTER_TO_INT(d), NULL);
}
static gboolean op_demote(GPtrArray* l, OrgPos* p, gpointer d) {
    return org_promote(l, p, 1, GPOINTER_TO_INT(d), NULL);
}
static gboolean op_move(GPtrArray* l, OrgPos* p, gpointer d) {
    return org_move_subtree(l, p, GPOINTER_TO_INT(d), NULL, NULL);
}
static gboolean op_insert_heading(GPtrArray* l, OrgPos* p, gpointer d) {
    OrgTodoConfig* cfg = org_todo_config_new(l);
    gint flags = GPOINTER_TO_INT(d);
    gboolean r = org_insert_heading(l, p, cfg, (flags & 1) != 0, (flags & 2) != 0);
    org_todo_config_free(cfg);
    return r;
}
static gboolean op_todo(GPtrArray* l, OrgPos* p, gpointer d) {
    OrgTodoConfig* cfg = org_todo_config_new(l);
    GDateTime* now = fixed_now();
    gboolean r = org_todo_cycle(l, p, org_heading_at(l, p->line), cfg, GPOINTER_TO_INT(d), now, NULL);
    g_date_time_unref(now);
    org_todo_config_free(cfg);
    return r;
}
static gboolean op_priority(GPtrArray* l, OrgPos* p, gpointer d) {
    OrgTodoConfig* cfg = org_todo_config_new(l);
    gboolean r = org_priority(l, p, p->line, cfg, GPOINTER_TO_INT(d), 0);
    org_todo_config_free(cfg);
    return r;
}
static gboolean op_tags(GPtrArray* l, OrgPos* p, gpointer d) {
    OrgTodoConfig* cfg = org_todo_config_new(l);
    gboolean r = org_set_tags(l, p, p->line, cfg, d);
    org_todo_config_free(cfg);
    return r;
}
static gboolean op_schedule(GPtrArray* l, OrgPos* p, gpointer d) {
    return org_set_planning(l, p, org_heading_at(l, p->line), "SCHEDULED", d);
}
static gboolean op_deadline(GPtrArray* l, OrgPos* p, gpointer d) {
    return org_set_planning(l, p, org_heading_at(l, p->line), "DEADLINE", d);
}
static gboolean op_toggle_heading(GPtrArray* l, OrgPos* p, gpointer d) {
    (void)d;
    return org_toggle_heading(l, p);
}
static gboolean op_insert_item(GPtrArray* l, OrgPos* p, gpointer d) {
    return org_insert_item(l, p, GPOINTER_TO_INT(d));
}
static gboolean op_indent_item(GPtrArray* l, OrgPos* p, gpointer d) {
    return org_indent_item(l, p, GPOINTER_TO_INT(d), NULL);
}
static gboolean op_move_item(GPtrArray* l, OrgPos* p, gpointer d) {
    return org_move_item(l, p, GPOINTER_TO_INT(d), NULL, NULL);
}
static gboolean op_bullet(GPtrArray* l, OrgPos* p, gpointer d) {
    return org_cycle_bullet(l, p, GPOINTER_TO_INT(d));
}
static gboolean op_checkbox(GPtrArray* l, OrgPos* p, gpointer d) {
    (void)d;
    OrgTodoConfig* cfg = org_todo_config_new(l);
    gboolean r = org_toggle_checkbox(l, p, NULL);
    if (r) org_update_statistics(l, cfg);
    org_todo_config_free(cfg);
    return r;
}
static gboolean op_ctrl_c_minus(GPtrArray* l, OrgPos* p, gpointer d) {
    (void)d;
    return org_ctrl_c_minus(l, p);
}
static gboolean op_template(GPtrArray* l, OrgPos* p, gpointer d) {
    (void)d;
    return org_expand_template(l, p);
}
static gboolean op_clock_in(GPtrArray* l, OrgPos* p, gpointer d) {
    (void)d;
    GDateTime* now = fixed_now();
    gboolean r = org_clock_in(l, p, org_heading_at(l, p->line), now, NULL);
    g_date_time_unref(now);
    return r;
}
static gboolean op_clock_out(GPtrArray* l, OrgPos* p, gpointer d) {
    (void)d;
    GDateTime* now = fixed_now();
    GDateTime* later = g_date_time_add_minutes(now, 95);
    gboolean r = org_clock_out(l, p, later, NULL);
    g_date_time_unref(later);
    g_date_time_unref(now);
    return r;
}

/* ---- Tests ---- */

static void test_heading_parse(void) {
    GPtrArray* lines = org_lines_new("#+TODO: TODO NEXT(n) | DONE CANCELLED\n");
    OrgTodoConfig* cfg = org_todo_config_new(lines);
    OrgHeading h;
    g_assert_true(org_heading_parse("** NEXT [#A] Write tests   :work:urgent:", cfg, &h));
    g_assert_cmpint(h.level, ==, 2);
    g_assert_cmpstr(h.keyword, ==, "NEXT");
    g_assert_cmpint(h.priority, ==, 'A');
    g_assert_cmpstr(h.title, ==, "Write tests");
    g_assert_cmpstr(h.tags, ==, ":work:urgent:");
    org_heading_clear(&h);
    g_assert_true(org_heading_parse("* TODOS are not keywords", cfg, &h));
    g_assert_null(h.keyword);
    g_assert_cmpstr(h.title, ==, "TODOS are not keywords");
    org_heading_clear(&h);
    g_assert_false(org_heading_parse("*bold* text", cfg, &h));
    g_assert_true(org_todo_is_done(cfg, "CANCELLED"));
    g_assert_false(org_todo_is_done(cfg, "NEXT"));
    org_todo_config_free(cfg);
    g_ptr_array_unref(lines);
}

static void test_heading_build_tags_column(void) {
    OrgHeading h = {1, NULL, 0, (gchar*)"Title", (gchar*)":a:"};
    gchar* s = org_heading_build(&h);
    g_assert_cmpint(g_utf8_strlen(s, -1), ==, 77);
    g_assert_true(g_str_has_prefix(s, "* Title "));
    g_assert_true(g_str_has_suffix(s, " :a:"));
    g_free(s);
}

static void test_subtree_bounds(void) {
    GPtrArray* l = org_lines_new("* A\ntext\n** B\nb\n* C\n");
    g_assert_cmpint(org_subtree_end(l, 0), ==, 4);
    g_assert_cmpint(org_entry_end(l, 0), ==, 2);
    g_assert_cmpint(org_subtree_end(l, 4), ==, 5); /* final newline excluded */
    g_assert_cmpint(org_heading_parent(l, 2), ==, 0);
    g_assert_cmpint(org_heading_next(l, 1, 1, TRUE), ==, 4);
    g_assert_cmpint(org_heading_next(l, 2, 1, TRUE), ==, -1);
    g_assert_cmpint(org_heading_next(l, 4, -1, TRUE), ==, 0);
    gint last = -1;
    g_assert_cmpint(org_fold_kind(l, 0, &last), ==, ORG_FOLD_HEADING);
    g_assert_cmpint(last, ==, 3);
    g_ptr_array_unref(l);

    l = org_lines_new("* A\n:PROPERTIES:\n:ID: 1\n:END:\n#+begin_src c\nx;\n#+END_SRC\n");
    g_assert_cmpint(org_fold_kind(l, 1, &last), ==, ORG_FOLD_DRAWER);
    g_assert_cmpint(last, ==, 3);
    g_assert_cmpint(org_fold_kind(l, 4, &last), ==, ORG_FOLD_BLOCK);
    g_assert_cmpint(last, ==, 6);
    g_assert_cmpint(org_fold_kind(l, 2, &last), ==, ORG_FOLD_NONE);
    g_ptr_array_unref(l);
}

static void test_promote_demote(void) {
    check("** A‸\n*** B\n", op_promote, GINT_TO_POINTER(FALSE), TRUE, "* A‸\n*** B\n");
    check("** A‸\n*** B\n", op_promote, GINT_TO_POINTER(TRUE), TRUE, "* A‸\n** B\n");
    check("* A‸\n** B\n", op_demote, GINT_TO_POINTER(TRUE), TRUE, "** A‸\n*** B\n");
    check("* A‸\n", op_promote, GINT_TO_POINTER(FALSE), FALSE, "* A‸\n");
}

static void test_move_subtree(void) {
    check("* A\na\n* B‸\nb\n", op_move, GINT_TO_POINTER(-1), TRUE, "* B‸\nb\n* A\na\n");
    check("* A‸\na\n** A1\n* B\nb\n", op_move, GINT_TO_POINTER(1), TRUE, "* B\nb\n* A‸\na\n** A1\n");
    check("* A\n** B‸\n* C\n", op_move, GINT_TO_POINTER(-1), FALSE, "* A\n** B‸\n* C\n");
    check("* A‸\n", op_move, GINT_TO_POINTER(1), FALSE, "* A‸\n");
}

static void test_insert_heading(void) {
    /* After the subtree when on a heading */
    check("* A‸\na\n** A1\n* B\n", op_insert_heading, GINT_TO_POINTER(0), TRUE,
          "* A\na\n** A1\n* ‸\n* B\n");
    /* At column 0 of a heading: above */
    check("‸** A\n", op_insert_heading, GINT_TO_POINTER(0), TRUE, "** ‸\n** A\n");
    /* In body text: split the line */
    check("* A\nsome‸ text\n", op_insert_heading, GINT_TO_POINTER(0), TRUE, "* A\nsome\n* ‸text\n");
    /* Respect content, TODO */
    check("* A\nbo‸dy\n\n* B\n", op_insert_heading, GINT_TO_POINTER(3), TRUE,
          "* A\nbody\n* TODO ‸\n\n* B\n");
    /* No heading yet */
    check("‸", op_insert_heading, GINT_TO_POINTER(0), TRUE, "* ‸");
}

static void test_toggle_heading(void) {
    check("* A\ntext‸\n", op_toggle_heading, NULL, TRUE, "* A\n** text‸\n");
    check("** A‸\n", op_toggle_heading, NULL, TRUE, "A‸\n");
    check("- [X] done‸\n", op_toggle_heading, NULL, TRUE, "* DONE done‸\n");
}

static void test_todo_cycle(void) {
    check("* Task‸\n", op_todo, GINT_TO_POINTER(1), TRUE, "* TODO Task‸\n");
    check("* TODO Task‸\n", op_todo, GINT_TO_POINTER(1), TRUE, "* DONE Task‸\n");
    check("* DONE Task‸\n", op_todo, GINT_TO_POINTER(1), TRUE, "* Task‸\n");
    check("* Task‸\n", op_todo, GINT_TO_POINTER(-1), TRUE, "* DONE Task‸\n");
    /* Custom sequence, cursor anywhere in the entry */
    check("#+TODO: TODO NEXT | DONE\n* NEXT Task\nbody‸\n", op_todo, GINT_TO_POINTER(1), TRUE,
          "#+TODO: TODO NEXT | DONE\n* DONE Task\nbody‸\n");
    /* Tags stay aligned */
    gchar* want = g_strdup_printf("* TODO Task‸%*s:x:\n", 63, "");
    check("* Task‸ :x:\n", op_todo, GINT_TO_POINTER(1), TRUE, want);
    g_free(want);
}

static void test_todo_logdone_and_repeat(void) {
    check("#+STARTUP: logdone\n* TODO Task‸\n", op_todo, GINT_TO_POINTER(1), TRUE,
          "#+STARTUP: logdone\n* DONE Task‸\nCLOSED: [2026-10-29 Thu 09:30]\n");
    check("* DONE Task‸\nCLOSED: [2026-10-29 Thu 09:30] SCHEDULED: <2026-10-28 Wed>\n",
          op_todo, GINT_TO_POINTER(-1), TRUE, "* TODO Task‸\nSCHEDULED: <2026-10-28 Wed>\n");
    check("* DONE Task‸\nCLOSED: [2026-10-29 Thu 09:30]\nbody\n", op_todo, GINT_TO_POINTER(-1),
          TRUE, "* TODO Task‸\nbody\n");
    /* A repeating entry is not closed: its date moves on */
    check("* TODO Water plants‸\nSCHEDULED: <2026-10-27 Tue +1w>\n", op_todo, GINT_TO_POINTER(1),
          TRUE, "* TODO Water plants‸\nSCHEDULED: <2026-11-03 Tue +1w>\n");
}

static void test_priority(void) {
    check("* TODO Task‸\n", op_priority, GINT_TO_POINTER(1), TRUE, "* TODO [#A] Task‸\n");
    check("* [#A] Task‸\n", op_priority, GINT_TO_POINTER(1), TRUE, "* Task‸\n");
    check("* [#B] Task‸\n", op_priority, GINT_TO_POINTER(-1), TRUE, "* [#C] Task‸\n");
    check("* Task‸\n", op_priority, GINT_TO_POINTER(-1), TRUE, "* [#C] Task‸\n");
}

static void test_tags(void) {
    gchar* want = g_strdup_printf("* Task‸%*s:work:home:\n", 60, "");
    check("* Task‸\n", op_tags, (gpointer) "work home", TRUE, want);
    g_free(want);
    check("* Task‸ :a:\n", op_tags, (gpointer) "", TRUE, "* Task‸\n");
    GPtrArray* l = org_lines_new("* T :a:b:");
    gchar* t = org_get_tags(l, 0, NULL);
    g_assert_cmpstr(t, ==, ":a:b:");
    g_free(t);
    g_ptr_array_unref(l);
}

static void test_startup_visibility(void) {
    static const struct {
        const gchar* text;
        OrgStartupVisibility vis;
    } CASES[] = {
        {"* A\n", ORG_STARTUP_OVERVIEW},
        {"#+STARTUP: content\n* A\n", ORG_STARTUP_CONTENTS},
        {"#+startup: logdone showall\n", ORG_STARTUP_SHOW_ALL},
        {"#+STARTUP: nofold\n#+STARTUP: fold\n", ORG_STARTUP_OVERVIEW},
        {"#+STARTUP: showeverything\n", ORG_STARTUP_SHOW_ALL},
    };
    for (guint i = 0; i < G_N_ELEMENTS(CASES); i++) {
        GPtrArray* l = org_lines_new(CASES[i].text);
        g_assert_cmpint(org_startup_visibility(l), ==, CASES[i].vis);
        g_ptr_array_unref(l);
    }
}

static void test_collect_tags(void) {
    GPtrArray* l = org_lines_new("#+TAGS: { home work(w) } urgent\n* A :work:x:\n* B :x:\ntext :not:\n");
    GPtrArray* tags = org_collect_tags(l, NULL);
    g_assert_cmpint(tags->len, ==, 4);
    g_assert_cmpstr(g_ptr_array_index(tags, 0), ==, "home");
    g_assert_cmpstr(g_ptr_array_index(tags, 1), ==, "urgent");
    g_assert_cmpstr(g_ptr_array_index(tags, 2), ==, "work");
    g_assert_cmpstr(g_ptr_array_index(tags, 3), ==, "x");
    g_ptr_array_unref(tags);
    g_ptr_array_unref(l);
}

static void test_planning(void) {
    check("* Task‸\nbody\n", op_schedule, (gpointer) "<2026-11-02 Mon>", TRUE,
          "* Task‸\nSCHEDULED: <2026-11-02 Mon>\nbody\n");
    check("* Task\nSCHEDULED: <2026-11-02 Mon>\nbody‸\n", op_deadline, (gpointer) "<2026-11-05 Thu>",
          TRUE, "* Task\nDEADLINE: <2026-11-05 Thu> SCHEDULED: <2026-11-02 Mon>\nbody‸\n");
    check("* Task\nSCHEDULED: <2026-11-02 Mon>\nbody‸\n", op_schedule, NULL, TRUE, "* Task\nbody‸\n");
}

static void test_clock(void) {
    check("* Task‸\nbody\n", op_clock_in, NULL, TRUE,
          "* Task‸\n:LOGBOOK:\nCLOCK: [2026-10-29 Thu 09:30]\n:END:\nbody\n");
    check("* Task\n:LOGBOOK:\nCLOCK: [2026-10-29 Thu 09:30]\n:END:\nbody‸\n", op_clock_out, NULL,
          TRUE, "* Task\n:LOGBOOK:\nCLOCK: [2026-10-29 Thu 09:30]--[2026-10-29 Thu 11:05] =>  1:35\n:END:\nbody‸\n");
    check("* Task‸\n", op_clock_out, NULL, FALSE, "* Task‸\n");
    /* A second clock goes on top of the logbook */
    check("* Task‸\nSCHEDULED: <2026-11-02 Mon>\n:LOGBOOK:\nCLOCK: [2026-10-28 Wed 10:00]--[2026-10-28 Wed 11:00] =>  1:00\n:END:\n",
          op_clock_in, NULL, TRUE,
          "* Task‸\nSCHEDULED: <2026-11-02 Mon>\n:LOGBOOK:\nCLOCK: [2026-10-29 Thu 09:30]\nCLOCK: [2026-10-28 Wed 10:00]--[2026-10-28 Wed 11:00] =>  1:00\n:END:\n");
}

static void test_item_parse(void) {
    OrgItem it;
    g_assert_true(org_item_parse("  - [X] done", &it));
    g_assert_cmpint(it.indent, ==, 2);
    g_assert_cmpstr(it.bullet, ==, "-");
    g_assert_cmpint(it.checkbox, ==, 'X');
    g_assert_cmpint(it.content, ==, 8);
    g_assert_true(org_item_parse("12) twelve", &it));
    g_assert_true(it.ordered);
    g_assert_cmpint(it.number, ==, 12);
    g_assert_false(org_item_parse("* heading", &it));
    g_assert_true(org_item_parse("  * star item", &it));
    g_assert_false(org_item_parse("-----", &it));
    g_assert_false(org_item_parse("-1 is negative", &it));
}

static void test_insert_item(void) {
    check("- a‸\n- b\n", op_insert_item, GINT_TO_POINTER(FALSE), TRUE, "- a\n- ‸\n- b\n");
    check("1. a‸\n2. b\n", op_insert_item, GINT_TO_POINTER(FALSE), TRUE, "1. a\n2. ‸\n3. b\n");
    check("- [X] a‸\n  more\n", op_insert_item, GINT_TO_POINTER(FALSE), TRUE, "- [X] a\n  more\n- [ ] ‸\n");
    check("- first‸ second\n", op_insert_item, GINT_TO_POINTER(TRUE), TRUE, "- first\n- [ ] ‸second\n");
    check("‸- a\n", op_insert_item, GINT_TO_POINTER(FALSE), TRUE, "- ‸\n- a\n");
    check("plain‸\n", op_insert_item, GINT_TO_POINTER(FALSE), FALSE, "plain‸\n");
}

static void test_indent_item(void) {
    check("- a\n- b‸\n  - c\n", op_indent_item, GINT_TO_POINTER(1), TRUE, "- a\n  - b‸\n    - c\n");
    check("- a\n  - b‸\n", op_indent_item, GINT_TO_POINTER(-1), TRUE, "- a\n- b‸\n");
    check("- a‸\n", op_indent_item, GINT_TO_POINTER(1), FALSE, "- a‸\n");
    check("- a‸\n", op_indent_item, GINT_TO_POINTER(-1), FALSE, "- a‸\n");
    check("1. a\n2. b‸\n3. c\n", op_indent_item, GINT_TO_POINTER(1), TRUE, "1. a\n   1. b‸\n2. c\n");
}

static void test_move_item(void) {
    check("- a\n  sub\n- b‸\n", op_move_item, GINT_TO_POINTER(-1), TRUE, "- b‸\n- a\n  sub\n");
    check("1. a‸\n2. b\n", op_move_item, GINT_TO_POINTER(1), TRUE, "1. b\n2. a‸\n");
    check("- a‸\n", op_move_item, GINT_TO_POINTER(1), FALSE, "- a‸\n");
}

static void test_cycle_bullet(void) {
    check("- a‸\n- b\n", op_bullet, GINT_TO_POINTER(1), TRUE, "+ a‸\n+ b\n");
    check("+ a‸\n+ b\n", op_bullet, GINT_TO_POINTER(1), TRUE, "1. a‸\n2. b\n");
    check("  + a‸\n", op_bullet, GINT_TO_POINTER(1), TRUE, "  * a‸\n");
    check("1) a‸\n", op_bullet, GINT_TO_POINTER(1), TRUE, "- a‸\n");
    check("text‸\n", op_ctrl_c_minus, NULL, TRUE, "- text‸\n");
}

static void test_checkbox(void) {
    check("- [ ] a‸\n", op_checkbox, NULL, TRUE, "- [X] a‸\n");
    check("- [X] a‸\n", op_checkbox, NULL, TRUE, "- [ ] a‸\n");
    /* Parent follows its children */
    check("- [ ] p\n  - [X] a\n  - [ ] b‸\n", op_checkbox, NULL, TRUE, "- [X] p\n  - [X] a\n  - [X] b‸\n");
    check("- [X] p\n  - [X] a‸\n  - [X] b\n", op_checkbox, NULL, TRUE, "- [-] p\n  - [ ] a‸\n  - [X] b\n");
    /* Children follow their parent */
    check("- [ ] p‸\n  - [ ] a\n  - [X] b\n", op_checkbox, NULL, TRUE, "- [X] p‸\n  - [X] a\n  - [X] b\n");
    /* Cookies */
    check("* Tasks [/]\n- [ ] a‸\n- [ ] b\n", op_checkbox, NULL, TRUE, "* Tasks [1/2]\n- [X] a‸\n- [ ] b\n");
    check("- list [%]\n  - [ ] a‸\n  - [X] b\n  - [ ] c\n  - [ ] d\n", op_checkbox, NULL, TRUE,
          "- list [50%]\n  - [X] a‸\n  - [X] b\n  - [ ] c\n  - [ ] d\n");
    check("- no box‸\n", op_checkbox, NULL, FALSE, "- no box‸\n");
}

static void test_todo_statistics(void) {
    check("* Project [/]\n** TODO a‸\n** DONE b\n** c\n", op_todo, GINT_TO_POINTER(1), TRUE,
          "* Project [2/2]\n** DONE a‸\n** DONE b\n** c\n");
}

static void test_links(void) {
    const gchar* l = "See [[https://example.org][the site]] or http://x.y/z now";
    gchar* t = org_link_at(l, 6);
    g_assert_cmpstr(t, ==, "https://example.org");
    g_free(t);
    t = org_link_at(l, 45);
    g_assert_cmpstr(t, ==, "http://x.y/z");
    g_free(t);
    g_assert_null(org_link_at(l, 1));
    t = org_link_format("file:a.org", "A");
    g_assert_cmpstr(t, ==, "[[file:a.org][A]]");
    g_free(t);
    t = org_link_format("x", "");
    g_assert_cmpstr(t, ==, "[[x]]");
    g_free(t);

    GPtrArray* lines = org_lines_new("* TODO Intro :a:\n** Details\n:PROPERTIES:\n:CUSTOM_ID: det\n:END:\n<<here>> text\n#+NAME: tbl\n");
    g_assert_cmpint(org_link_find_internal(lines, NULL, "*Details"), ==, 1);
    OrgTodoConfig* cfg = org_todo_config_new(lines);
    g_assert_cmpint(org_link_find_internal(lines, cfg, "*Intro"), ==, 0);
    org_todo_config_free(cfg);
    g_assert_cmpint(org_link_find_internal(lines, NULL, "#det"), ==, 1);
    g_assert_cmpint(org_link_find_internal(lines, NULL, "here"), ==, 5);
    g_assert_cmpint(org_link_find_internal(lines, NULL, "tbl"), ==, 6);
    g_assert_cmpint(org_link_find_internal(lines, NULL, "nowhere"), ==, -1);
    g_ptr_array_unref(lines);
}

static void test_templates(void) {
    check("<s‸", op_template, NULL, TRUE, "#+begin_src ‸\n\n#+end_src");
    check("  <s python‸", op_template, NULL, TRUE, "  #+begin_src python\n  ‸\n  #+end_src");
    check("<q‸\n", op_template, NULL, TRUE, "#+begin_quote\n‸\n#+end_quote\n");
    check("<l‸", op_template, NULL, TRUE, "#+begin_export latex\n‸\n#+end_export");
    check("<z‸", op_template, NULL, FALSE, "<z‸");
    check("<s‸ x", op_template, NULL, FALSE, "<s‸ x");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/org_doc/heading_parse", test_heading_parse);
    g_test_add_func("/org_doc/heading_build_tags_column", test_heading_build_tags_column);
    g_test_add_func("/org_doc/subtree_bounds", test_subtree_bounds);
    g_test_add_func("/org_doc/promote_demote", test_promote_demote);
    g_test_add_func("/org_doc/move_subtree", test_move_subtree);
    g_test_add_func("/org_doc/insert_heading", test_insert_heading);
    g_test_add_func("/org_doc/toggle_heading", test_toggle_heading);
    g_test_add_func("/org_doc/todo_cycle", test_todo_cycle);
    g_test_add_func("/org_doc/todo_logdone_and_repeat", test_todo_logdone_and_repeat);
    g_test_add_func("/org_doc/priority", test_priority);
    g_test_add_func("/org_doc/tags", test_tags);
    g_test_add_func("/org_doc/startup_visibility", test_startup_visibility);
    g_test_add_func("/org_doc/collect_tags", test_collect_tags);
    g_test_add_func("/org_doc/planning", test_planning);
    g_test_add_func("/org_doc/clock", test_clock);
    g_test_add_func("/org_doc/item_parse", test_item_parse);
    g_test_add_func("/org_doc/insert_item", test_insert_item);
    g_test_add_func("/org_doc/indent_item", test_indent_item);
    g_test_add_func("/org_doc/move_item", test_move_item);
    g_test_add_func("/org_doc/cycle_bullet", test_cycle_bullet);
    g_test_add_func("/org_doc/checkbox", test_checkbox);
    g_test_add_func("/org_doc/todo_statistics", test_todo_statistics);
    g_test_add_func("/org_doc/links", test_links);
    g_test_add_func("/org_doc/templates", test_templates);
    return g_test_run();
}
