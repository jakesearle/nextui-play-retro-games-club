#ifndef CHOICE_H
#define CHOICE_H

#include "apostrophe.h"

/* A message, an optional picture above it and a footer of buttons, waiting
   for one of them. ap_confirmation draws the same thing but only answers
   A and B; resuming needs a third answer (Start Over). */

typedef struct {
    const char     *message;
    const char     *image_path;  /* NULL for none; shown scaled to fit */
    ap_footer_item *footer;      /* Every button in it is an answer */
    int             footer_count;
} ChoiceOpts;

/* Returns the button pressed, always one of the footer's. */
ap_button Choice(const ChoiceOpts *opts);

#endif
