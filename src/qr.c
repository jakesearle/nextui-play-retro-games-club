/* Light modules around the code. The spec asks for 4; 2 is what phone
   scanners need in practice, and the code sits on a dark screen, so the
   white border is the quiet zone. */
#define QR_QUIET_MODULES 2
#define QR_MAX_LINKS 4

#include <string.h>

#include "qrcodegen.h"

#include "qr.h"

typedef struct {
    uint8_t code[qrcodegen_BUFFER_LEN_MAX];
    int modules;           /* per side, without the quiet zone */
    SDL_Surface *caption;  /* may be NULL */
    int column_w;
} Column;

static void FillRect(SDL_Surface *s, int x, int y, int w, int h, Uint32 color) {
    SDL_Rect r = { x, y, w, h };
    SDL_FillRect(s, &r, color);
}

bool Qr_RenderLinks(const Qr_Link *links, int count, TTF_Font *font, SDL_Color color,
                    int qr_size, int gap, const char *out_path, int *out_w, int *out_h) {
    if (count > QR_MAX_LINKS) count = QR_MAX_LINKS;
    static Column columns[QR_MAX_LINKS];
    int used = 0;

    /* One module size for every code, so they come out the same size even
       when one URL needs a denser code: the densest code sets it. */
    int widest = 0;
    for (int i = 0; i < count; i++) {
        Column *c = &columns[used];
        uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
        if (!links[i].url || !links[i].url[0] ||
            !qrcodegen_encodeText(links[i].url, temp, c->code, qrcodegen_Ecc_MEDIUM,
                                  qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                  qrcodegen_Mask_AUTO, true)) {
            continue;
        }
        c->modules = qrcodegen_getSize(c->code);
        if (c->modules > widest) widest = c->modules;
        c->caption = font && links[i].caption ? TTF_RenderUTF8_Blended(font, links[i].caption, color) : NULL;
        used++;
    }
    if (used == 0) return false;

    int module_px = qr_size / (widest + 2 * QR_QUIET_MODULES);
    if (module_px < 1) module_px = 1;

    int caption_h = 0;
    for (int i = 0; i < used; i++) {
        if (columns[i].caption && columns[i].caption->h > caption_h) caption_h = columns[i].caption->h;
    }
    int caption_gap = caption_h ? caption_h / 3 : 0;
    int code_px = module_px * (widest + 2 * QR_QUIET_MODULES);

    int width = 0;
    for (int i = 0; i < used; i++) {
        int w = code_px;
        if (columns[i].caption && columns[i].caption->w > w) w = columns[i].caption->w;
        columns[i].column_w = w;
        width += w + (i > 0 ? gap : 0);
    }
    int height = caption_h + caption_gap + code_px;

    SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    bool ok = out != NULL;
    if (ok) {
        SDL_FillRect(out, NULL, SDL_MapRGBA(out->format, 0, 0, 0, 0));
        Uint32 white = SDL_MapRGBA(out->format, 255, 255, 255, 255);
        Uint32 black = SDL_MapRGBA(out->format, 0, 0, 0, 255);

        int x = 0;
        for (int i = 0; i < used; i++) {
            Column *c = &columns[i];
            if (c->caption) {
                /* Copied, not blended: the canvas is clear here, and
                   blending onto clear black would darken the text's
                   anti-aliased edges. */
                SDL_SetSurfaceBlendMode(c->caption, SDL_BLENDMODE_NONE);
                SDL_Rect at = { x + (c->column_w - c->caption->w) / 2, 0, c->caption->w, c->caption->h };
                SDL_BlitSurface(c->caption, NULL, out, &at);
            }

            /* Centred in its slot, each code at the shared module size. */
            int own_px = module_px * (c->modules + 2 * QR_QUIET_MODULES);
            int cx = x + (c->column_w - code_px) / 2 + (code_px - own_px) / 2;
            int cy = caption_h + caption_gap + (code_px - own_px) / 2;
            FillRect(out, cx, cy, own_px, own_px, white);
            for (int my = 0; my < c->modules; my++) {
                for (int mx = 0; mx < c->modules; mx++) {
                    if (!qrcodegen_getModule(c->code, mx, my)) continue;
                    FillRect(out, cx + (mx + QR_QUIET_MODULES) * module_px,
                             cy + (my + QR_QUIET_MODULES) * module_px, module_px, module_px, black);
                }
            }
            x += c->column_w + gap;
        }

        /* A 32-bit BMP keeps the alpha, and needs no PNG writer. */
        ok = SDL_SaveBMP(out, out_path) == 0;
        if (ok) {
            *out_w = width;
            *out_h = height;
        }
        SDL_FreeSurface(out);
    }

    for (int i = 0; i < used; i++) {
        if (columns[i].caption) SDL_FreeSurface(columns[i].caption);
    }
    return ok;
}
