#include <string.h>

#include "subtitle_list.h"

/* Matches ap_list's AP__PILL_SIZE and AP__BUTTON_PADDING. */
#define LIST_PILL_SIZE 30
#define LIST_BUTTON_PADDING 12
/* Space between the label and the line under it, and above and below both. */
#define LIST_LINE_GAP 1
#define LIST_ROW_PADDING 2

static bool HasSubtitle(const SubtitleListItem *item) {
    return item->subtitle && item->subtitle[0];
}

int SubtitleList(const SubtitleListOpts *opts, ap_list_result *result) {
    if (!opts || !result || !opts->items || opts->item_count <= 0) return AP_ERROR;

    memset(result, 0, sizeof(*result));
    result->selected_index = -1;

    ap_theme *theme = ap_get_theme();
    TTF_Font *label_font = ap_get_font(AP_FONT_LARGE);
    TTF_Font *sub_font = ap_get_font(AP_FONT_TINY);
    if (!label_font || !sub_font) return AP_ERROR;

    int margin = App_DeviceScaled(App_DevicePadding());
    int pad = App_DeviceScaled(LIST_BUTTON_PADDING);
    int label_h = TTF_FontHeight(label_font);
    int sub_h = TTF_FontHeight(sub_font);
    int gap = App_DeviceScaled(LIST_LINE_GAP);
    int row_h = label_h + gap + sub_h + App_DeviceScaled(LIST_ROW_PADDING) * 2;
    int pill_min_h = App_DeviceScaled(LIST_PILL_SIZE);
    if (row_h < pill_min_h) row_h = pill_min_h;
    int radius = pill_min_h / 2;

    SDL_Rect content = ap_get_content_rect(opts->title != NULL, opts->footer_count > 0, false);
    int max_visible = content.h / row_h;
    if (max_visible < 1) max_visible = 1;

    int count = opts->item_count;
    int cursor = opts->initial_index;
    if (cursor < 0 || cursor >= count) cursor = 0;
    int scroll_top = opts->visible_start_index;
    if (scroll_top > count - max_visible) scroll_top = count - max_visible;
    if (scroll_top < 0) scroll_top = 0;

    int available_w = ap_get_screen_width() - margin * 2;
    if (count > max_visible) available_w -= ap_scale(12); /* scrollbar */
    int text_max_w = available_w - pad * 2;

    ap_text_scroll scroll;
    ap_text_scroll_init(&scroll);
    int last_cursor = cursor;
    uint32_t last_frame = SDL_GetTicks();

    bool running = true;
    while (running) {
        uint32_t now = SDL_GetTicks();
        uint32_t dt = now - last_frame;
        last_frame = now;

        ap_input_event ev;
        while (ap_poll_input(&ev)) {
            if (!ev.pressed) continue;

            if (ev.button == AP_BTN_UP) {
                /* Wrap only on a fresh press, as ap_list does, so holding
                   the button stops at the top instead of spinning. */
                if (cursor > 0) cursor--;
                else if (!ev.repeated) cursor = count - 1;
            } else if (ev.button == AP_BTN_DOWN) {
                if (cursor < count - 1) cursor++;
                else if (!ev.repeated) cursor = 0;
            } else if (ev.button == AP_BTN_A) {
                result->action = AP_ACTION_SELECTED;
                running = false;
            } else if (ev.button == AP_BTN_B) {
                result->action = AP_ACTION_BACK;
                running = false;
            } else if (opts->secondary_action_button != AP_BTN_NONE &&
                       ev.button == opts->secondary_action_button) {
                result->action = AP_ACTION_SECONDARY_TRIGGERED;
                running = false;
            } else if (opts->tertiary_action_button != AP_BTN_NONE &&
                       ev.button == opts->tertiary_action_button) {
                result->action = AP_ACTION_TERTIARY_TRIGGERED;
                running = false;
            }
        }
        if (!running) break;

        if (cursor < scroll_top) scroll_top = cursor;
        if (cursor >= scroll_top + max_visible) scroll_top = cursor - max_visible + 1;
        if (cursor != last_cursor) {
            ap_text_scroll_reset(&scroll);
            last_cursor = cursor;
        }

        ap_draw_background();
        if (opts->title) ap_draw_screen_title(opts->title, NULL);

        for (int i = 0; i < max_visible && scroll_top + i < count; i++) {
            int idx = scroll_top + i;
            const SubtitleListItem *item = &opts->items[idx];
            const char *label = item->label ? item->label : "";
            bool selected = idx == cursor;
            bool two_lines = HasSubtitle(item);

            int row_y = content.y + i * row_h;
            int text_x = margin + pad;
            int block_h = two_lines ? label_h + gap + sub_h : label_h;
            int label_y = row_y + (row_h - block_h) / 2;
            int sub_y = label_y + label_h + gap;

            int label_w = ap_measure_text(label_font, label);

            if (selected) {
                /* Hug the text like ap_list's pill, wide enough for both lines. */
                int text_w = label_w;
                if (two_lines) {
                    int sub_w = ap_measure_text_ellipsized(sub_font, item->subtitle, text_max_w);
                    if (sub_w > text_w) text_w = sub_w;
                }
                int pill_w = text_w + pad * 2;
                if (pill_w > available_w) pill_w = available_w;
                ap_draw_rounded_rect(margin, row_y, pill_w, row_h, radius, theme->highlight);
            }

            ap_color label_color = selected ? theme->highlighted_text : theme->text;

            /* The selected label scrolls when it is too long to fit; the
               others are cut off with an ellipsis. */
            if (selected && label_w > text_max_w) {
                ap_text_scroll_update(&scroll, label_w, text_max_w, dt);
                if (scroll.active) ap_request_frame();
                SDL_Rect clip = { text_x, row_y, text_max_w, row_h };
                SDL_RenderSetClipRect(ap_get_renderer(), &clip);
                ap_draw_text(label_font, label, text_x - scroll.offset, label_y, label_color);
                SDL_RenderSetClipRect(ap_get_renderer(), NULL);
            } else {
                ap_draw_text_ellipsized(label_font, label, text_x, label_y, label_color, text_max_w);
            }

            if (two_lines) {
                ap_color sub_color = selected ? theme->highlighted_text : theme->hint;
                if (selected) sub_color.a = 190;
                ap_draw_text_ellipsized(sub_font, item->subtitle, text_x, sub_y, sub_color, text_max_w);
            }
        }

        if (count > max_visible) {
            int bar_x = ap_get_screen_width() - margin - ap_scale(6);
            ap_draw_scrollbar(bar_x, content.y, content.h, max_visible, count, scroll_top);
        }

        if (opts->footer && opts->footer_count > 0) ap_draw_footer(opts->footer, opts->footer_count);

        ap_present();
    }

    result->selected_index = cursor;
    result->visible_start_index = scroll_top;
    return result->action == AP_ACTION_BACK ? AP_CANCELLED : AP_OK;
}
