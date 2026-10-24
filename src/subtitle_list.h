#ifndef SUBTITLE_LIST_H
#define SUBTITLE_LIST_H

#include "apostrophe.h"
#include "apostrophe_widgets.h"

/* A list whose rows have a smaller second line under the label, which
   ap_list cannot do. Styled after ap_list, minus what this app does not use
   (images, multi-select, reordering, letter jumps). */

typedef struct {
    const char *label;
    const char *subtitle; /* May be NULL or empty for a one-line row */
} SubtitleListItem;

typedef struct {
    const char       *title;
    SubtitleListItem *items;
    int               item_count;
    ap_footer_item   *footer;
    int               footer_count;
    ap_button         secondary_action_button; /* AP_BTN_NONE = unused */
    int               initial_index;
    int               visible_start_index;
} SubtitleListOpts;

/* Blocks until A, B or the secondary button. Returns AP_OK with
   result->action AP_ACTION_SELECTED or AP_ACTION_SECONDARY_TRIGGERED, or
   AP_CANCELLED on B. selected_index and visible_start_index are filled in
   either way, for restoring the position on the next call. */
int SubtitleList(const SubtitleListOpts *opts, ap_list_result *result);

/* Apostrophe's device-scaled layout units, which only the translation unit
   defining AP_IMPLEMENTATION can compute; main.c provides these. */
int App_DeviceScaled(int base);
int App_DevicePadding(void);

#endif
