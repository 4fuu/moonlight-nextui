#include "apostrophe.h"
#include "nextui_coop.h"

#include <stdbool.h>

/*
 * No-op implementation of the cooperation seam. The real binding is M1-M6 and
 * waits for the phase-A contract (nextui-app-sdk) and phase B (NextUI system
 * side). Keep this file as the only place that knows both the SDK and the
 * wrapper's stream state; ui.c and moonlight_cli.c only call this API.
 */

static bool s_streaming;
static bool s_quit_requested;

void nextui_coop_init(void)
{
#ifdef NEXTUI_SDK
    /* M1: open the devserv connection, subscribe to the event channel, then
     * attach the Apostrophe host backend:
     *   ap_host_attach(&backend, on_host_event, &state);
     * Overlay data (app name, shared rows) arrives with phase A. */
#endif
}

void nextui_coop_shutdown(void)
{
#ifdef NEXTUI_SDK
    /* M1: detach the host backend, stop the event pump, close devserv. */
#endif
}

void nextui_coop_stream_begin(void)
{
    s_streaming = true;
    s_quit_requested = false;

#ifdef NEXTUI_SDK
    /* M4: request sleep.inhibit("streaming", timeout) before the engine owns
     * the display, so the system does not suspend mid-session. */
#endif
}

void nextui_coop_stream_end(void)
{
#ifdef NEXTUI_SDK
    /* M4: release the sleep.inhibit lease and ack any pending sleep.prepare. */
#endif
    s_streaming = false;
}

void nextui_coop_tick(void)
{
#ifdef NEXTUI_SDK
    /* M1/M4: drain events. sleep.prepare while streaming keeps the inhibit
     * alive; wake.done re-arms it. Never touches SDL: the engine owns fb0. */
#endif
}

bool nextui_coop_streaming(void)
{
    return s_streaming;
}

nextui_overlay_owner nextui_coop_overlay_owner(void)
{
    /* Dynamic ownership direction from the integration plan: the menu can
     * host in-process, streaming cannot (engine process owns the display). */
    return s_streaming ? NEXTUI_OVERLAY_SYSTEM : NEXTUI_OVERLAY_APP;
}

bool nextui_coop_quit_requested(void)
{
    return s_quit_requested;
}
