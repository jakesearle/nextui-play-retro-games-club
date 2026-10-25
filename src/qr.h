#ifndef QR_H
#define QR_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Links as QR codes, since a handheld cannot open them: each one a white
   code with its caption above, side by side in one picture with a
   transparent background, for the detail screen's image section, which
   only shows files. */

typedef struct {
    const char *caption; /* "RetroAchievements" */
    const char *url;
} Qr_Link;

/* Renders up to 4 links side by side, each code qr_size pixels square
   (rounded down to whole pixels per module) and gap pixels apart, captions
   in font and color. Saves the picture to out_path and reports its size.
   Returns false if nothing could be drawn. Main thread only (SDL_ttf). */
bool Qr_RenderLinks(const Qr_Link *links, int count, TTF_Font *font, SDL_Color color,
                    int qr_size, int gap, const char *out_path, int *out_w, int *out_h);

#endif
