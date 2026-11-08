#ifndef UI_PROBE_H
#define UI_PROBE_H

#include <glib.h>

/* Test hook for the headless UI tests: with LOOKIN_UI_PROBE=<file>, each
 * SIGUSR2 writes an increasing number to <file> once the main loop has run
 * everything of higher priority than G_PRIORITY_LOW (input, layout,
 * redraws). A test that sends the signal and waits for the number to change
 * knows the application has caught up. Does nothing without the variable or
 * outside Unix. */
void ui_probe_install(void);

/* Same with an explicit file, for the unit test */
void ui_probe_install_file(const gchar* path);

#endif
