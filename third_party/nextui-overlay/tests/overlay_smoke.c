/*
 * Host render smoke test for the vendored NextUI overlay.
 *
 * It initialises the production GFX resource loader against a fake SD root
 * (/mnt/SDCARD/.system/res, created by scripts/nextui/test-overlay.sh), opens
 * the real UIOverlay with the shared system rows, draws one frame, and writes
 * a PNG. This proves the vendored files, assets, theme and blit path work
 * outside NextUI; it does not replace device acceptance.
 */

#include "defines.h"
#include "api.h"
#include "ui_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int host_wifi(void* user) { (void)user; return 1; }
static int host_bt(void* user) { (void)user; return 0; }
static int host_set(void* user, int on) { (void)user; (void)on; return 1; }
static int host_available(void* user) { (void)user; return 1; }
static int host_can_save(void* user) { (void)user; return 1; }
static int host_save(void* user) { (void)user; return 1; }
static int host_can_resume(void* user) { (void)user; return 1; }
static int host_resume(void* user) { (void)user; return 1; }
static int host_exit(void* user) { (void)user; return 1; }

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "overlay-smoke.png";

    /* FIXED_WIDTH/FIXED_HEIGHT depend on is_brick/is_brickpro, which the
     * platform seam derives from DEVICE (brick, brickpro, smartpro). */
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

    /* A plain "game frame" behind the panel so the dim wash is visible. */
    SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0x20, 0x38, 0x50));

    UIOverlayDeviceHooks device = {
        .user = NULL,
        .available = host_available,
        .wifi_get = host_wifi,
        .wifi_set = host_set,
        .bluetooth_get = host_bt,
        .bluetooth_set = host_set,
    };

    OverlayAppHooks hooks;
    memset(&hooks, 0, sizeof(hooks));
    hooks.capabilities = UI_OVERLAY_CAP_SAVE | UI_OVERLAY_CAP_RESUME | UI_OVERLAY_CAP_EXIT |
                         UI_OVERLAY_CAP_WIFI | UI_OVERLAY_CAP_BLUETOOTH;
    hooks.app_name = "Moonlight";
    hooks.can_save = host_can_save;
    hooks.save = host_save;
    hooks.can_resume = host_can_resume;
    hooks.resume = host_resume;
    hooks.exit_to_desktop = host_exit;
    hooks.device = &device;

    UIOverlayStrings strings = *UIOverlay_defaultStrings();
    UIOverlay* overlay = UIOverlay_create(&hooks, &strings);
    if (!overlay) {
        fprintf(stderr, "UIOverlay_create failed\n");
        return 1;
    }

    UIOverlay_open(overlay);
    UIOverlay_handleEvent(overlay, UI_OVERLAY_EVENT_DOWN);
    UIOverlay_draw(overlay, screen);

    if (IMG_SavePNG(screen, out) != 0) {
        fprintf(stderr, "IMG_SavePNG(%s): %s\n", out, IMG_GetError());
        return 1;
    }

    printf("overlay smoke: wrote %s (%dx%d)\n", out, screen->w, screen->h);

    UIOverlay_destroy(overlay);
    GFX_quitAttached();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
