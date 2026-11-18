/*
 * Platform seam for the vendored NextUI overlay renderer.
 *
 * The vendored files are NextUI's production overlay code (ui_overlay.c,
 * apostrophe_overlay_host.c and the GFX/PAD/CFG implementation in api.c).
 * api.c keeps every function in its own section, so linking with
 * -ffunction-sections/--gc-sections only requires the platform symbols the
 * overlay path actually reaches. This file supplies them.
 *
 * Status data (battery, Wi-Fi, Bluetooth, volume) is the NextUI system seam.
 * It is intentionally local and replaceable: M3 (HUD) and M5 (battery) in the
 * integration plan replace the msettings/PLAT_* getters with SDK data. Until
 * then the values are conservative defaults so the renderer builds and can be
 * exercised on the host.
 *
 * License: MIT for this file. The vendored renderer is PolyForm Noncommercial
 * 1.0.0; see third_party/nextui-overlay/VENDORED.md.
 */

#include "defines.h"
#include "api.h"
#include "msettings.h"
#include "nextui_bridge.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* platform.h declares these; platform.c normally defines them from the
 * device-tree model. FIXED_SCALE/FIXED_WIDTH/FIXED_HEIGHT depend on them. */
int is_brick = 0;
int is_brickpro = 0;

/* Status fed by the cooperation layer (M3/M5). Defaults are the same
 * conservative values the renderer shipped with before the SDK existed. */
static int status_volume = SETTINGS_DEFAULT_VOLUME;
static int status_brightness = SETTINGS_DEFAULT_BRIGHTNESS;
static int status_colortemp = SETTINGS_DEFAULT_COLORTEMP;
static int status_mute = 0;
static int status_muted_volume = SETTINGS_DEFAULT_MUTE_NO_CHANGE;
static int status_muted_brightness = SETTINGS_DEFAULT_MUTE_NO_CHANGE;
static int status_muted_colortemp = SETTINGS_DEFAULT_MUTE_NO_CHANGE;
static int status_wifi_enabled = 1;
static int status_bt_enabled = 1;
static int status_bt_connected = 1;
static ConnectionStrength status_wifi_strength = SIGNAL_STRENGTH_HIGH;

/* api.c has a weak empty fallback; this strong definition runs the model
 * detection the overlay's scale macros depend on. */
void PLAT_initPlatform(void)
{
    const char* device = getenv("DEVICE");
    is_brick = device && strcmp(device, "brick") == 0;
    is_brickpro = device && strcmp(device, "brickpro") == 0;
}

/* ── msettings seam (M3/M5 replace with SDK data) ─────────────────────── */

int GetVolume(void) { return status_volume; }
int GetBrightness(void) { return status_brightness; }
int GetColortemp(void) { return status_colortemp; }
int GetContrast(void) { return SETTINGS_DEFAULT_CONTRAST; }
int GetSaturation(void) { return SETTINGS_DEFAULT_SATURATION; }
int GetExposure(void) { return SETTINGS_DEFAULT_EXPOSURE; }
int GetDisplayCalEnabled(void) { return 0; }
int GetDisplayCalRedGain(void) { return 100; }
int GetDisplayCalGreenGain(void) { return 100; }
int GetDisplayCalBlueGain(void) { return 100; }
int GetJack(void) { return 0; }
int GetAudioSink(void) { return AUDIO_SINK_DEFAULT; }
int GetHDMI(void) { return 0; }
int GetMute(void) { return status_mute; }
int GetMutedVolume(void) { return status_muted_volume; }
int GetMutedBrightness(void) { return status_muted_brightness; }
int GetMutedColortemp(void) { return status_muted_colortemp; }
int GetMutedContrast(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMutedSaturation(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMutedExposure(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMuteDisablesDpad(void) { return 0; }
int GetMuteEmulatesJoystick(void) { return 0; }
int GetMuteTurboA(void) { return 0; }
int GetMuteTurboB(void) { return 0; }
int GetMuteTurboX(void) { return 0; }
int GetMuteTurboY(void) { return 0; }
int GetMuteTurboL1(void) { return 0; }
int GetMuteTurboL2(void) { return 0; }
int GetMuteTurboR1(void) { return 0; }
int GetMuteTurboR2(void) { return 0; }

void SetVolume(int value) { (void)value; }
void SetBrightness(int value) { (void)value; }
void SetColortemp(int value) { (void)value; }
void SetContrast(int value) { (void)value; }
void SetSaturation(int value) { (void)value; }
void SetExposure(int value) { (void)value; }
void SetDisplayCalEnabled(int value) { (void)value; }
void SetDisplayCalRedGain(int value) { (void)value; }
void SetDisplayCalGreenGain(int value) { (void)value; }
void SetDisplayCalBlueGain(int value) { (void)value; }
void SetJack(int value) { (void)value; }
void SetAudioSink(int value) { (void)value; }
void SetHDMI(int value) { (void)value; }
void SetMute(int value) { (void)value; }
void SetMutedVolume(int value) { (void)value; }
void SetMutedBrightness(int value) { (void)value; }
void SetMutedColortemp(int value) { (void)value; }
void SetMutedContrast(int value) { (void)value; }
void SetMutedSaturation(int value) { (void)value; }
void SetMutedExposure(int value) { (void)value; }
void SetMuteDisablesDpad(int value) { (void)value; }
void SetMuteEmulatesJoystick(int value) { (void)value; }
void SetMuteTurboA(int value) { (void)value; }
void SetMuteTurboB(int value) { (void)value; }
void SetMuteTurboX(int value) { (void)value; }
void SetMuteTurboY(int value) { (void)value; }
void SetMuteTurboL1(int value) { (void)value; }
void SetMuteTurboL2(int value) { (void)value; }
void SetMuteTurboR1(int value) { (void)value; }
void SetMuteTurboR2(int value) { (void)value; }

/* ── network / status seam (M5 replaces with SDK battery/status) ──────── */

bool PLAT_hasWifi(void) { return true; }
bool PLAT_wifiEnabled(void) { return status_wifi_enabled != 0; }
void PLAT_wifiEnable(bool on) { (void)on; }
bool PLAT_hasBluetooth(void) { return true; }
bool PLAT_bluetoothEnabled(void) { return status_bt_enabled != 0; }
void PLAT_bluetoothEnable(bool on) { (void)on; }
ConnectionStrength PLAT_connectionStrength(void) { return status_wifi_strength; }
bool PLAT_btIsConnected(void) { return status_bt_connected != 0; }

void nextui_bridge_set_network(int wifi_strength, int wifi_enabled,
                               int bt_connected, int bt_enabled)
{
    status_wifi_strength = (ConnectionStrength)wifi_strength;
    status_wifi_enabled = wifi_enabled;
    status_bt_connected = bt_connected;
    status_bt_enabled = bt_enabled;
}

void nextui_bridge_set_audio(int volume, int mute, int locked)
{
    status_volume = volume;
    status_mute = mute;
    status_muted_volume = locked ? volume : SETTINGS_DEFAULT_MUTE_NO_CHANGE;
}

void nextui_bridge_set_display(int brightness, int colortemp,
                               int brightness_locked, int colortemp_locked)
{
    status_brightness = brightness;
    status_colortemp = colortemp;
    status_muted_brightness =
        brightness_locked ? brightness : SETTINGS_DEFAULT_MUTE_NO_CHANGE;
    status_muted_colortemp =
        colortemp_locked ? colortemp : SETTINGS_DEFAULT_MUTE_NO_CHANGE;
}

void nextui_bridge_set_battery(int percent, int charging)
{
    PWR_setBatteryStatus(charging, percent);
}

/* ── video/power seam ─────────────────────────────────────────────────────
 *
 * The overlay never owns the display: Apostrophe presents, and the NextUI
 * power path belongs to the system. These exist only because api.c's retained
 * sections reference them; they are not called on the overlay path. */

void PLAT_flip(SDL_Surface* screen, int sync) { (void)screen; (void)sync; }
SDL_Surface* PLAT_resizeVideo(int w, int h, int pitch)
{
    (void)w; (void)h; (void)pitch;
    return NULL;
}
void PLAT_clearVideo(SDL_Surface* screen) { (void)screen; }
void PLAT_clearLayers(int layer) { (void)layer; }
void PLAT_clearAll(void) {}
void PLAT_powerOff(int reboot) { (void)reboot; }

/* Battery source for the shared row. The cooperation layer overrides it with
 * SDK data via nextui_bridge_set_battery(); this fallback reads the same sysfs
 * nodes NextUI uses so a missed status patch does not freeze the icon. */
void PLAT_getBatteryStatusFine(int* is_charging, int* charge)
{
    if (is_charging) {
        int time_to_full = 0;
        int charger_present = 0;
        FILE* f = fopen("/sys/class/power_supply/axp2202-battery/time_to_full_now", "r");
        if (f) { if (fscanf(f, "%d", &time_to_full) != 1) time_to_full = 0; fclose(f); }
        f = fopen("/sys/class/power_supply/axp2202-usb/online", "r");
        if (f) { if (fscanf(f, "%d", &charger_present) != 1) charger_present = 0; fclose(f); }
        *is_charging = (charger_present == 1) && (time_to_full > 0);
    }
    if (charge) {
        int percent = 0;
        FILE* f = fopen("/sys/class/power_supply/axp2202-battery/capacity", "r");
        if (f) { if (fscanf(f, "%d", &percent) != 1) percent = 0; fclose(f); }
        *charge = percent;
    }
}

/* ── input seam ───────────────────────────────────────────────────────────
 *
 * apostrophe_overlay_host.c calls PAD_poll() = PLAT_pollInput() while the
 * overlay owns focus. PLAT_pollInput itself lives in api.c and maps SDL
 * joystick events to BTN_*; this file only opens/closes the devices, using the
 * same approach as NextUI's tg5040 platform.c. While the overlay is closed the
 * host does not poll PAD, so Apostrophe keeps its own input ownership. */

static SDL_Joystick** joysticks = NULL;
static int num_joysticks = 0;

void PLAT_initInput(void)
{
    PLAT_initPlatform();
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
        LOG_error("Failed initializing joysticks: %s\n", SDL_GetError());

    num_joysticks = SDL_NumJoysticks();
    if (num_joysticks > 0) {
        joysticks = (SDL_Joystick**)malloc(sizeof(SDL_Joystick*) * (size_t)num_joysticks);
        for (int i = 0; i < num_joysticks; i++) {
            joysticks[i] = SDL_JoystickOpen(i);
            LOG_info("Opening joystick %d: %s\n", i, SDL_JoystickName(joysticks[i]));
        }
    }
}

void PLAT_quitInput(void)
{
    if (joysticks) {
        for (int i = 0; i < num_joysticks; i++) {
            if (SDL_JoystickGetAttached(joysticks[i]))
                SDL_JoystickClose(joysticks[i]);
        }
        free(joysticks);
        joysticks = NULL;
        num_joysticks = 0;
    }
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

/*
 * Brick Pro reports KEY_HOMEPAGE on the combined TRIMUI Player1 evdev node.
 * SDL's evdev keyboard backend skips joystick-class devices, so HOME never
 * becomes an SDL event. Query the kernel key bitmap without consuming the
 * event queue, exactly as NextUI's tg5040 platform.c does. Used only when the
 * SDK has no session (otherwise devserv sends overlay.home).
 */
#define HOME_KEY_EVENT_TYPE 0x01
#define HOME_KEY_CODE 172
#define HOME_KEY_MAX 0x2ff
#define HOME_KEY_BITS (HOME_KEY_MAX / 8 + 1)
#define HOME_KEY_EVIOCGBIT(len) _IOC(_IOC_READ, 'E', 0x20 + HOME_KEY_EVENT_TYPE, len)
#define HOME_KEY_EVIOCGKEY(len) _IOC(_IOC_READ, 'E', 0x18, len)

static int home_key_fd = -1;

static int open_home_key_device(void)
{
    char path[32];
    unsigned char keybits[HOME_KEY_BITS];

    for (int i = 0; i < 32; i++) {
        int fd;
        snprintf(path, sizeof(path), "/dev/input/event%i", i);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        memset(keybits, 0, sizeof(keybits));
        if (ioctl(fd, HOME_KEY_EVIOCGBIT(sizeof(keybits)), keybits) >= 0 &&
            (keybits[HOME_KEY_CODE / 8] & (1 << (HOME_KEY_CODE % 8)))) {
            return fd;
        }
        close(fd);
    }
    return -1;
}

int PLAT_getHomeKeyPressed(void)
{
    unsigned char keys[HOME_KEY_BITS];

    if (home_key_fd < 0 && !(home_key_fd = open_home_key_device()))
        home_key_fd = -1;
    if (home_key_fd < 0) return 0;
    memset(keys, 0, sizeof(keys));
    if (ioctl(home_key_fd, HOME_KEY_EVIOCGKEY(sizeof(keys)), keys) < 0) return 0;
    return (keys[HOME_KEY_CODE / 8] & (1 << (HOME_KEY_CODE % 8))) ? 1 : 0;
}

void PLAT_updateInput(const SDL_Event* event)
{
    if (event->type == SDL_JOYDEVICEADDED) {
        SDL_Joystick* joy = SDL_JoystickOpen(event->jdevice.which);
        if (joy) {
            joysticks = (SDL_Joystick**)realloc(
                joysticks, sizeof(SDL_Joystick*) * (size_t)(num_joysticks + 1));
            joysticks[num_joysticks++] = joy;
        }
    } else if (event->type == SDL_JOYDEVICEREMOVED) {
        for (int i = 0; i < num_joysticks; i++) {
            if (SDL_JoystickInstanceID(joysticks[i]) == event->jdevice.which) {
                SDL_JoystickClose(joysticks[i]);
                for (int j = i; j < num_joysticks - 1; j++)
                    joysticks[j] = joysticks[j + 1];
                num_joysticks--;
                break;
            }
        }
    }
}
