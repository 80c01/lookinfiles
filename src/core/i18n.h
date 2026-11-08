#ifndef I18N_H
#define I18N_H

#include <glib.h>

/* Translations of the user-visible strings (gettext, catalogs in po/).
 *
 * Mark a string shown to the user with _("...") where it is used, N_("...")
 * in a static table (translate it with _() when shown), C_("context", "...")
 * when the same English word needs two translations, and ngettext() for
 * counts. Identifiers, config keys, file names and key names stay untranslated. */

#define GETTEXT_PACKAGE "lookinfiles"
#include <glib/gi18n.h>

/* Sets the locale from the environment and binds the catalog directory
 * (i18n_locale_dir()). Call first thing in main(); argv0 helps find the
 * executable where /proc/self/exe is not available. */
void i18n_init(const char* argv0);

/* Directory holding <lang>/LC_MESSAGES/lookinfiles.mo, in this order:
 * $LOOKINFILES_LOCALEDIR, <exe_dir>/build/locale (source tree),
 * <exe_dir>/share/locale (Windows portable folder),
 * <exe_dir>/../share/locale (installed or relocated, e.g. AppImage), then
 * fallback. The first that exists wins; fallback if none does. exe_dir may
 * be NULL. Caller frees. */
gchar* i18n_locale_dir(const char* exe_dir, const char* fallback);

#endif
