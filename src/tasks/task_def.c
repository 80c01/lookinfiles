/* A build or test task (see task_def.h) */
#include "tasks/task_def.h"

TaskDef* task_def_new(const gchar* label, const gchar* const* argv, const gchar* dir) {
    TaskDef* def = g_new0(TaskDef, 1);
    def->label = g_strdup(label);
    def->argv = g_strdupv((gchar**)argv);
    def->dir = g_strdup(dir);
    return def;
}

TaskDef* task_def_copy(const TaskDef* def) {
    if (def == NULL) return NULL;
    TaskDef* copy = task_def_new(def->label, (const gchar* const*)def->argv, def->dir);
    copy->detail = g_strdup(def->detail);
    copy->env = g_strdupv(def->env);
    copy->matcher = g_strdup(def->matcher);
    copy->shell = def->shell;
    copy->tex_root = g_strdup(def->tex_root);
    return copy;
}

void task_def_free(TaskDef* def) {
    if (def == NULL) return;
    g_free(def->label);
    g_free(def->detail);
    g_strfreev(def->argv);
    g_free(def->dir);
    g_strfreev(def->env);
    g_free(def->matcher);
    g_free(def->tex_root);
    g_free(def);
}

static gboolean strv_equal(gchar** a, gchar** b) {
    if (a == NULL || b == NULL) return (a == NULL || a[0] == NULL) && (b == NULL || b[0] == NULL);
    return g_strv_equal((const gchar* const*)a, (const gchar* const*)b);
}

gboolean task_def_equal(const TaskDef* a, const TaskDef* b) {
    if (a == NULL || b == NULL) return a == b;
    return g_strcmp0(a->dir, b->dir) == 0 && strv_equal(a->argv, b->argv) && strv_equal(a->env, b->env);
}
