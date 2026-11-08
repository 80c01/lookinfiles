#ifndef HTML_TAGS_H
#define HTML_TAGS_H

/* Text tags of the HTML preview (html_title, html_h1..., html_code...),
 * created once per buffer with role colours and fonts that follow the theme */
#include <gtk/gtk.h>

void ensure_html_tags(GtkTextBuffer* buffer);

#endif /* HTML_TAGS_H */
