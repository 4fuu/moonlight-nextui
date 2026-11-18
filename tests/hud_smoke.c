/*
 * Host render smoke test for the cooperation HUD.
 *
 * Draws NextUI's own GFX_blitHardwareIndicator (the exact function the system
 * HUD uses) with values fed through the SDK bridge, then writes a PNG. This
 * proves the HUD asset path and the bridge values, not the event plumbing
 * (tests/test_coop.c covers the protocol side).
 */

#include "defines.h"
#include "api.h"
#include "nextui_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "hud-smoke.png";

    PLAT_initPlatform();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!IMG_Init(IMG_INIT_PNG)) {
        fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());
        return 1;
    }
    if (!TTF_WasInit() && TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        return 1;
    }

    SDL_Surface* screen = GFX_initAttached(FIXED_WIDTH, FIXED_HEIGHT);
    if (!screen) {
        fprintf(stderr, "GFX_initAttached(%d, %d) failed\n", FIXED_WIDTH, FIXED_HEIGHT);
        return 1;
    }

    /* A dark app frame behind the HUD, like a menu screen. */
    SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0x1e, 0x23, 0x29));

    nextui_bridge_set_audio(12, 0, 0);
    nextui_bridge_set_display(7, 20, 0, 0);

    int ow = SCALE1(PILL_SIZE + SETTINGS_WIDTH + 10 + 4);
    int ox = screen->w - SCALE1(PADDING) - ow;
    int oy = SCALE1(PADDING);
    GFX_blitHardwareIndicator(screen, ox, oy, INDICATOR_VOLUME);

    if (IMG_SavePNG(screen, out) != 0) {
        fprintf(stderr, "IMG_SavePNG(%s): %s\n", out, IMG_GetError());
        return 1;
    }
    printf("hud smoke: wrote %s (%dx%d)\n", out, screen->w, screen->h);

    GFX_quitAttached();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
