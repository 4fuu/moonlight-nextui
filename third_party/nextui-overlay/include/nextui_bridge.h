/*
 * Bridge between the cooperation layer and the vendored NextUI renderer.
 *
 * The renderer draws the shared status row and HUD from the same platform
 * getters NextUI uses. On device those getters live in platform.c; in this pak
 * they are fed from the SDK's status/feedback data instead, so the drawn row
 * matches the system panel without reading msettings twice.
 *
 * MIT, like the rest of this repository's glue.
 */

#ifndef NEXTUI_BRIDGE_H
#define NEXTUI_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* wifi_strength is a SIGNAL_STRENGTH_* value from api.h. */
void nextui_bridge_set_network(int wifi_strength, int wifi_enabled,
                               int bt_connected, int bt_enabled);
void nextui_bridge_set_audio(int volume, int mute, int locked);
void nextui_bridge_set_display(int brightness, int colortemp,
                               int brightness_locked, int colortemp_locked);
void nextui_bridge_set_battery(int percent, int charging);

#ifdef __cplusplus
}
#endif

#endif /* NEXTUI_BRIDGE_H */
