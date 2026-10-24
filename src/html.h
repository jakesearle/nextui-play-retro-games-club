#ifndef HTML_H
#define HTML_H

#include <stdbool.h>
#include <stddef.h>

/* Just enough HTML handling to read one well-formed, server-rendered page.
   Not a general parser: it locates elements by class token and reads their
   text, which is all the club page needs. Every function works on a
   [start, end) span and never reads past end. */

/* An element's span, from its opening '<' to just past its closing tag. */
typedef struct {
    const char *start;
    const char *end;
} Html_Span;

/* Finds the next element in span whose class attribute contains cls as a
   whole token. Returns false when there is none. */
bool Html_FindClass(Html_Span span, const char *cls, Html_Span *out);

/* Copies an attribute of the element starting at elem, entity-decoded.
   Returns false when the element has no such attribute. */
bool Html_Attr(const char *elem, const char *end, const char *name,
               char *out, size_t out_size);

/* The first url('...') in the element's style attribute, which is how the
   page sets its box art. */
bool Html_StyleUrl(const char *elem, const char *end, char *out, size_t out_size);

/* The element's visible text: tags stripped, entities decoded, whitespace
   collapsed and trimmed. Children marked aria-hidden="true" are skipped,
   since on this page they hold only emoji the device font cannot draw. */
void Html_Text(Html_Span span, char *out, size_t out_size);

/* Shorthand: the text of the first element with class cls, or "" if absent. */
bool Html_ClassText(Html_Span span, const char *cls, char *out, size_t out_size);

#endif
