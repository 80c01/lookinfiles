#ifndef CODE_SCAN_C_H
#define CODE_SCAN_C_H

/* Symbol scanner for the C family (C, C++, Java, C#): a state machine over
 * comments, string literals, preprocessor lines, braces and statements,
 * without a real parser. Finds functions (definitions and prototypes),
 * methods, macros, typedefs, structs / unions / enums / classes, enum
 * constants, fields and global variables. Only the first branch of
 * #if / #else is read, so braces stay balanced. Pure GLib, unit-tested
 * through code_scan. */
#include "code/code_symbol.h"

/* Append the symbols of text to out (CodeSymbol*): all of them, or only
 * those named only_name when it is not NULL */
void code_scan_c(const gchar* text, gsize len, CodeLang lang, const gchar* only_name,
                 GPtrArray* out);

#endif /* CODE_SCAN_C_H */
