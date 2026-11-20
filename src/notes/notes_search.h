#ifndef NOTES_SEARCH_H
#define NOTES_SEARCH_H

/* How Ctrl+Shift+F searches the notes (project_search_show_notes()): the
 * trash and notes.xml are skipped, a Canvas note is searched in its text
 * (not the XML of its page), each file of the results shows as its note
 * (title, notebook, type icon) and a result opens as from the Notes page:
 * a Canvas note in its Canvas view, the others in an editor tab at the
 * line. */
#include "core/app_types.h"

const SearchArea* notes_search_area(void);

#endif /* NOTES_SEARCH_H */
