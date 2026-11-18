#ifndef NEXTUI_COOP_H
#define NEXTUI_COOP_H

#include <stdbool.h>

/*
 * NextUI cooperation layer (integration plan M1-M6).
 *
 * The SDK client (third_party/nextui-app-sdk) owns the devserv protocol; this
 * module owns the app-side policy:
 *
 *   - menu: the wrapper hosts the full-fidelity NextUI overlay in-process
 *     (overlay.host true, hud.policy app);
 *   - streaming: the engine owns fb0, so the wrapper yields hosting
 *     (overlay.host false) and the system fallback draws HOME/HUD, while the
 *     SDK keeps the event channel open for sleep inhibit and status.
 *
 * Ownership policy is therefore dynamic, not a static manifest claim:
 * nextui_coop_overlay_owner() answers per frame, and overlay_suspend/resume
 * perform the ordered transition from contract section 7.9.3.
 *
 * All SDK calls are safe to make from the main thread while the SDK thread
 * owns polling; request functions are serialized inside the client.
 */

typedef enum {
    NEXTUI_OVERLAY_SYSTEM = 0, /* system fallback draws HOME/HUD */
    NEXTUI_OVERLAY_APP,        /* wrapper draws the overlay in-process */
} nextui_overlay_owner;

/* After ap_init()/PAD_init: connect, attach the Apostrophe host backend, and
 * start the event thread. A missing devserv is not fatal: the app runs
 * unadapted and HOME falls back to the system panel. */
void nextui_coop_init(void);
void nextui_coop_shutdown(void);

/* Before ap_quit(): close the panel, report overlay.state closed, yield
 * hosting, and release the renderer-bound overlay resources.
 * After ap_init(): rebuild the overlay against the new renderer and restore
 * hosting. */
void nextui_coop_overlay_suspend(void);
void nextui_coop_overlay_resume(void);

/* Stream lifecycle (ml_stream). Holds the automatic sleep pre-inhibit and
 * tracks the ownership answer. */
void nextui_coop_stream_begin(void);
void nextui_coop_stream_end(void);

/* Called from the stream wait loop: refreshes the sleep inhibit lease and
 * observes session end. */
void nextui_coop_tick(void);

bool nextui_coop_streaming(void);
nextui_overlay_owner nextui_coop_overlay_owner(void);

/* True when the SDK asked the app to leave (session.ended, or the overlay's
 * Exit action). ml_stream() terminates the engine; main() leaves the loop. */
bool nextui_coop_quit_requested(void);

/* A short human-readable state line for logs/tests. */
const char *nextui_coop_state_string(void);

#endif /* NEXTUI_COOP_H */
