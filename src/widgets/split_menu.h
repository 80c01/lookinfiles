#ifndef SPLIT_MENU_H
#define SPLIT_MENU_H

/* The menu of a split handle: as soon as the pointer is on the handle of a
 * GtkPaned of the layout, a small bar offers to collapse either side (or to
 * expand the collapsed one), to put the handle at one of three shares of the
 * room (split_ratio.h) and, where the start side can tell its natural size,
 * to fit that side to it. The bar is drawn over the window in an
 * overlay, in the middle of the handle and beside its line (which stays free
 * to drag); it hides on a press on the handle (a drag), after a click on the
 * bar, or a second after the pointer has left both. */
#include <gtk/gtk.h>

#include "widgets/split_ratio.h"

typedef struct SplitMenu SplitMenu;

typedef struct {
    /* Sides of paned the bar may collapse (SplitSide flags, split_ratio.h);
     * -1: no bar on that handle */
    gint (*sides)(GtkWidget* paned, gpointer data);
    /* Move the handle of paned to pos (and record it in the layout) */
    void (*apply)(GtkWidget* paned, gint pos, gpointer data);
    /* The shares offered on the handle of paned (NULL: SPLIT_SHARES_EVEN) */
    SplitShares (*shares)(GtkWidget* paned, gpointer data);
    /* Position fitting the start side of paned to its content, -1: none
     * (NULL: no fit button on any handle) */
    gint (*fit)(GtkWidget* paned, gpointer data);
    gpointer data;
} SplitMenuHooks;

SplitMenu* split_menu_new(const SplitMenuHooks* hooks);
/* The overlay the bar is shown in (over every paned attached); no bar before */
void split_menu_set_overlay(SplitMenu* menu, GtkOverlay* overlay);
/* Show the bar when the pointer rests on the handle of paned */
void split_menu_attach(SplitMenu* menu, GtkWidget* paned);
/* Hide the bar (and forget a pending show) */
void split_menu_hide(SplitMenu* menu);
/* The double click on the handle of paned: fit its start side to its content
 * where the fit hook gives a position, put the handle at a half otherwise */
void split_menu_reset(SplitMenu* menu, GtkWidget* paned);
void split_menu_free(SplitMenu* menu);

#endif /* SPLIT_MENU_H */
