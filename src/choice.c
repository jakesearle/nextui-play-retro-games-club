#include "apostrophe.h"
#include "apostrophe_widgets.h"

#include "choice.h"

ap_button Choice(const ChoiceOpts *opts) {
    ap_theme *theme = ap_get_theme();
    TTF_Font *font = ap_get_font(AP_FONT_MEDIUM);
    int screen_w = ap_get_screen_width();
    int screen_h = ap_get_screen_height();

    /* The picture gets at most half the screen, kept to its own shape. */
    SDL_Texture *image = opts->image_path ? ap_load_image(opts->image_path) : NULL;
    int image_w = 0, image_h = 0;
    if (image) {
        int tex_w = 0, tex_h = 0;
        SDL_QueryTexture(image, NULL, NULL, &tex_w, &tex_h);
        int max_w = screen_w * 2 / 3, max_h = screen_h / 2;
        if (tex_w > 0 && tex_h > 0) {
            image_w = max_w;
            image_h = tex_h * max_w / tex_w;
            if (image_h > max_h) {
                image_h = max_h;
                image_w = tex_w * max_h / tex_h;
            }
        }
    }

    int text_w = screen_w - ap_scale(80);
    int text_h = font ? ap_measure_wrapped_text_height(font, opts->message, text_w) : 0;
    int gap = image ? ap_scale(20) : 0;

    ap_button answer = AP_BTN_NONE;
    while (answer == AP_BTN_NONE) {
        ap_input_event ev;
        while (ap_poll_input(&ev)) {
            if (!ev.pressed || ev.repeated) continue;
            for (int i = 0; i < opts->footer_count; i++) {
                if (ev.button == opts->footer[i].button) answer = ev.button;
            }
        }

        ap_draw_background();
        SDL_Rect content = ap_get_content_rect(false, opts->footer_count > 0, false);
        int y = content.y + (content.h - (image_h + gap + text_h)) / 2;
        if (image) {
            ap_draw_image(image, (screen_w - image_w) / 2, y, image_w, image_h);
            y += image_h + gap;
        }
        if (font) {
            ap_draw_text_wrapped(font, opts->message, (screen_w - text_w) / 2, y, text_w,
                                 theme->text, AP_ALIGN_CENTER);
        }
        if (opts->footer_count > 0) ap_draw_footer(opts->footer, opts->footer_count);
        ap_present();
    }

    if (image) SDL_DestroyTexture(image);
    return answer;
}
