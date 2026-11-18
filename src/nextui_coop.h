#ifndef NEXTUI_COOP_H
#define NEXTUI_COOP_H

#include <stdbool.h>

/*
 * NextUI cooperation seam (integration plan M1-M6).
 *
 * S1-S5 put the call sites and the ownership policy in place; the SDK work
 * itself is still gated on phase A (contract + C client) and phase B (system
 * side). Everything in this module is a no-op until NEXTUI_SDK is defined.
 *
 * Ownership policy, fixed here so M2/M6 cannot drift:
 *   - menu / pair / settings: the wrapper owns the display and hosts the
 *     full-fidelity overlay in-process (NEXTUI_OVERLAY_APP);
 *   - streaming: the engine is a separate process owning the display, so the
 *     wrapper answers NEXTUI_OVERLAY_SYSTEM and the system fallback draws.
 *
 * M1  src/nextui_coop.c: connect devserv, attach ap_host_backend, event pump.
 * M2  main.c: ap_host_attach + poll/present around the menu loop.
 * M3  system-feedback ownership + HUD drawing (app only in menu mode).
 * M4  sleep.prepare / sleep.inhibit / wake.done; stream_begin holds inhibit.
 * M5  battery/status for the overlay row.
 * M6  streaming HOME: handled_by_system with phase A; until then the wrapper
 *     can end the stream (quit_requested) and reopen the menu overlay.
 */

typedef enum {
    NEXTUI_OVERLAY_SYSTEM = 0, /* system fallback draws HOME/HUD */
    NEXTUI_OVERLAY_APP,        /* wrapper draws the overlay in-process */
} nextui_overlay_owner;

/* Process-wide lifecycle, called from main() around the Apostrophe loop. */
void nextui_coop_init(void);
void nextui_coop_shutdown(void);

/* Stream lifecycle, called by ml_stream() around the controlled spawn. */
void nextui_coop_stream_begin(void);
void nextui_coop_stream_end(void);

/* Pump SDK events. Called by ml_stream() while the engine runs; M1 makes this
 * drain the event channel (sleep.prepare, wake.done, battery changes). */
void nextui_coop_tick(void);

bool nextui_coop_streaming(void);

/* Dynamic ownership seam: the answer the app gives app-action/system-feedback
 * for the current mode. */
nextui_overlay_owner nextui_coop_overlay_owner(void);

/* True when the SDK asked the app to leave the current stream (phase-A
 * dynamic ownership + HOME, or the M6 fallback). ml_stream() terminates the
 * engine process group and returns a clean status. */
bool nextui_coop_quit_requested(void);

#endif /* NEXTUI_COOP_H */
