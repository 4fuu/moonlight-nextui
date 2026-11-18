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

#include <stdlib.h>
#include <string.h>

/* platform.h declares these; platform.c normally defines them from the
 * device-tree model. FIXED_SCALE/FIXED_WIDTH/FIXED_HEIGHT depend on them. */
int is_brick = 0;
int is_brickpro = 0;

/* api.c has a weak empty fallback; this strong definition runs the model
 * detection the overlay's scale macros depend on. */
void PLAT_initPlatform(void)
{
    const char* device = getenv("DEVICE");
    is_brick = device && strcmp(device, "brick") == 0;
    is_brickpro = device && strcmp(device, "brickpro") == 0;
}

/* ── msettings seam (M3/M5 replace with SDK data) ─────────────────────── */

int GetVolume(void) { return SETTINGS_DEFAULT_VOLUME; }
int GetBrightness(void) { return SETTINGS_DEFAULT_BRIGHTNESS; }
int GetColortemp(void) { return SETTINGS_DEFAULT_COLORTEMP; }
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
int GetMute(void) { return 0; }
int GetMutedVolume(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMutedBrightness(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMutedColortemp(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
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
bool PLAT_wifiEnabled(void) { return true; }
void PLAT_wifiEnable(bool on) { (void)on; }
bool PLAT_hasBluetooth(void) { return true; }
bool PLAT_bluetoothEnabled(void) { return true; }
void PLAT_bluetoothEnable(bool on) { (void)on; }
ConnectionStrength PLAT_connectionStrength(void) { return SIGNAL_STRENGTH_HIGH; }
bool PLAT_btIsConnected(void) { return true; }

/* ── input seam (M2 wires this to the app's input owner) ──────────────────
 *
 * apostrophe_overlay_host.c calls PAD_poll() = PLAT_pollInput() while the
 * overlay owns focus, and PAD_reset() = PLAT_resetInput() on focus changes.
 * The device implementation must feed the same physical events Apostrophe
 * receives, or read evdev like NextUI's tg5040 platform.c. S1-S5 only needs
 * the symbols to exist; M2 owns the policy. */

void PLAT_initInput(void) {}
void PLAT_quitInput(void) {}
void PLAT_updateInput(const SDL_Event* event) { (void)event; }
void PLAT_pollInput(void) {}
