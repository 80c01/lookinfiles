#ifndef SPLIT_MENU_H
#define SPLIT_MENU_H

/* The menu of a split handle: when the pointer rests on the handle of a
 * GtkPaned of the layout, a small bar along the handle offers to collapse
 * either side (or to expand the collapsed one) and to put the handle at a
 * third, a half or two thirds of the room (split_ratio.h). The bar is drawn
 * over the window in an overlay, next to the pointer (which stays on the
 * handle, so a drag still works), and hides when the pointer leaves both, on
 * a press on the handle or after a click on the bar. */
#include <gtk/gtk.h>

typedef struct SplitMenu SplitMenu;

typedef struct {
    /* Sides of paned the bar may collapse (SplitSide flags, split_ratio.h);
     * -1: no bar on that handle */
    gint (*sides)(GtkWidget* paned, gpointer data);
    /* Move the handle of paned to pos (and record it in the layout) */
    void (*apply)(GtkWidget* paned, gint pos, gpointer data);
    gpointer data;
} SplitMenuHooks;

SplitMenu* split_menu_new(const SplitMenuHooks* hooks);
/* The overlay the bar is shown in (over every paned attached); no bar before */
void split_menu_set_overlay(SplitMenu* menu, GtkOverlay* overlay);
/* Show the bar when the pointer rests on the handle of paned */
void split_menu_attach(SplitMenu* menu, GtkWidget* paned);
/* Hide the bar (and forget a pending show) */
void split_menu_hide(SplitMenu* menu);
/* Put the handle of paned at a half (the double click on the handle) */
void split_menu_halve(SplitMenu* menu, GtkWidget* paned);
void split_menu_free(SplitMenu* menu);

#endif /* SPLIT_MENU_H */
