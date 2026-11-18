#ifndef __APOSTROPHE_OVERLAY_HOST_H__
#define __APOSTROPHE_OVERLAY_HOST_H__

#include <SDL2/SDL.h>

#include "ui_overlay.h"

/*
 * Hosts the shared B5 overlay inside an Apostrophe application.
 *
 * This is a NextUI-side adapter: it does not copy B5's panel or drawing code,
 * it owns a real UIOverlay and composites that overlay's SDL_Surface into the
 * SDL_Renderer Apostrophe already created. No second window, renderer or
 * GFX_init is involved.
 *
 * The application still owns its main loop:
 *
 *   host = ApostropheOverlayHost_create(&config);       // after ap_init()
 *   ap_host_attach(&backend, on_host_event, app);       // backend calls
 *                                                       // Host_poll/draw
 *   ...
 *   if (!ap_host_poll()) { app input }
 *   draw_app();
 *   ap_host_present();                                  // app frame, then
 *                                                       // B5 composite, then
 *                                                       // ap_present()
 *
 * The host callback table's poll function calls ApostropheOverlayHost_poll().
 * Its draw function calls ApostropheOverlayHost_draw(). The adapter's B5 focus
 * callback is the only place that calls ap_host_overlay_changed(), and its
 * input-reset callback calls PAD_reset() and ap_reset_input() together.
 *
 * The application must have called PAD_init() before the first poll. Config
 * strings (app_name/title) and device hooks must outlive the host.
 */

typedef struct
{
	void* user;

	const char* app_name; /* optional; shown in the overlay header */
	const char* title;    /* optional; defaults to B5's "System" */

	unsigned capabilities; /* UI_OVERLAY_CAP_* accepted by the app */

	int (*can_save)(void* user);
	int (*save)(void* user);
	int (*can_resume)(void* user);
	int (*resume)(void* user);

	/* Called for the overlay's Exit action. Returning 1 lets the adapter send
	 * the one-shot ap_host_request_quit(); the application still owns the quit
	 * flag and the shutdown path. NULL accepts the exit request directly. */
	int (*exit_to_desktop)(void* user);

	const UIOverlayDeviceHooks* device; /* NULL means device rows Unavailable */

	/* Optional observers. They run after the adapter's own host transition
	 * work, never instead of it. */
	void (*focus_changed)(void* user, int overlay_has_focus);
	void (*input_reset)(void* user);
} ApostropheOverlayConfig;

typedef struct ApostropheOverlayHost ApostropheOverlayHost;

/* Requires a completed ap_init() and an unclaimed NextUI GFX context.
 * Initializes GFX's drawing resources against Apostrophe's existing screen
 * size without creating a display, creates the B5 overlay and returns NULL
 * when either step fails. */
ApostropheOverlayHost* ApostropheOverlayHost_create(const ApostropheOverlayConfig* config);

/* Closes an open overlay (a real transition), destroys it, releases the
 * composite target and the attached GFX resources. Does not detach ap_host;
 * call ap_host_detach() first in the normal shutdown path. */
void ApostropheOverlayHost_destroy(ApostropheOverlayHost* host);

void ApostropheOverlayHost_open(ApostropheOverlayHost* host);
void ApostropheOverlayHost_close(ApostropheOverlayHost* host);
void ApostropheOverlayHost_toggle(ApostropheOverlayHost* host);
int ApostropheOverlayHost_isOpen(const ApostropheOverlayHost* host);

/* Frame input from the host backend's poll callback. While the overlay owns
 * focus this pumps NextUI PAD and feeds B5; while it is closed it leaves input
 * to Apostrophe. Returns 1 when the application must skip its own input. */
int ApostropheOverlayHost_poll(ApostropheOverlayHost* host);

/* Host backend draw callback. Draws the B5 panel into an offscreen surface and
 * composites it into Apostrophe's renderer. Returns 1 when a frame was
 * composited, 0 when the overlay is closed. Never presents. */
int ApostropheOverlayHost_draw(ApostropheOverlayHost* host, SDL_Renderer* renderer);

/* One-shot normal quit request, forwarded to ap_host_request_quit(). */
int ApostropheOverlayHost_requestQuit(ApostropheOverlayHost* host);

/* Read-only access for host tests and diagnostics. */
UIOverlay* ApostropheOverlayHost_overlay(ApostropheOverlayHost* host);

#endif
