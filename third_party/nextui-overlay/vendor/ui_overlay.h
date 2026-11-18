#ifndef __UI_OVERLAY_H__
#define __UI_OVERLAY_H__

#include "sdl.h"
#include <stdint.h>

/*
 * B5 shared application overlay.
 *
 * This is an in-process overlay for adapted NextUI applications. The app owns
 * the window and the GFX surface; it calls this module to draw the panel, hand
 * over input focus while it is open, and get focus back when it closes. The
 * module cannot draw into another process and does not claim to be a
 * system-wide compositor.
 *
 * Integration contract:
 *   - The app decides what opens the overlay (for example BTN_FN3/HOME on a
 *     device). B5 does not wire FN keys and does not parse fn[123]action.
 *   - While UIOverlay_isOpen() is true the app must stop handling its own
 *     input and call UIOverlay_pollInput() (or UIOverlay_handleEvent()).
 *   - The app draws its frame first, then calls UIOverlay_draw() last.
 *   - Opening and closing invoke focus_changed and input_reset so the app can
 *     pause/resume gameplay and clear latched input. The demo's input_reset
 *     calls PAD_reset(); keeping it a callback means this module does not own
 *     the app's input state.
 *
 * State actions are caller-owned. The overlay only enables Save/Resume when
 * the app declares the capability and its callback reports a valid state; it
 * never reads a resume file or implements B3 restore logic. Device actions are
 * likewise explicit hooks. A missing hook is shown as unavailable and is not
 * reported as a successful toggle.
 */

#define UI_OVERLAY_CAP_SAVE (1u << 0)
#define UI_OVERLAY_CAP_RESUME (1u << 1)
#define UI_OVERLAY_CAP_EXIT (1u << 2)
#define UI_OVERLAY_CAP_WIFI (1u << 3)
#define UI_OVERLAY_CAP_BLUETOOTH (1u << 4)

/*
 * Black wash drawn over the app frame while the HOME panel is open. The value
 * is the alpha written into the destination pixel (not an opacity percentage).
 * 140/255 leaves about 45% of the app visible. The resident system overlay used
 * to inherit 232/255 from the launcher's modal, which leaves only 9% of a game
 * frame and reads as an opaque black layer on the Brick Pro.
 *
 * The resident system overlay disables this software wash and gives the same
 * alpha to a disp2 colour layer, so the value is shared by both paths.
 */
#define UI_OVERLAY_DIM_ALPHA 140

/*
 * The B4/manifest adapter maps PAK_CAPABILITY_SAVES to SAVE|RESUME. The
 * overlay capability itself is the app's agreement to host this module, so
 * this header does not re-read pak.json or duplicate B1.
 */

typedef struct
{
	void* user;

	/* Return 1 when a device service (for example devserv) is reachable.
	 * NULL or 0 means the device actions are not connected. */
	int (*available)(void* user);

	/* Return -1 when unavailable, otherwise 0 (off) or 1 (on).
	 * NULL means the corresponding device action is unavailable. */
	int (*wifi_get)(void* user);
	int (*wifi_set)(void* user, int on);
	int (*bluetooth_get)(void* user);
	int (*bluetooth_set)(void* user, int on);
} UIOverlayDeviceHooks;

typedef struct
{
	void* user;
	unsigned capabilities; /* UI_OVERLAY_CAP_* mask; 0 means no app actions */
	const char* app_name;  /* optional; shown in the panel header */

	/* Resident system overlay: put Continue/Close first, then Exit, as in the
	 * system fallback layout. Adapted apps leave this 0 and keep the shared
	 * overlay's action order. */
	int close_first;

	/* B3-aligned state callbacks. The overlay only asks whether an action is
	 * currently valid and then invokes the action. All callbacks may be NULL.
	 * Return 1 for success/valid, 0 for unavailable/invalid.
	 *
	 * can_resume is the validity gate for restore: an adapter can return
	 * app_state_is_resumable(...) here. If it is NULL, Resume is disabled.
	 * can_save is optional; when NULL, Save is enabled whenever save is set. */
	int (*can_save)(void* user);
	int (*save)(void* user);
	int (*can_resume)(void* user);
	int (*resume)(void* user);

	/* App-owned action. Return 1 when the app accepted the request; the
	 * overlay then closes. The module never calls exit(), power-off or sleep. */
	int (*exit_to_desktop)(void* user);

	/* Focus hand-off. Called once on open with 1 and once on close with 0.
	 * NULL is allowed. */
	void (*focus_changed)(void* user, int overlay_has_focus);

	/* Optional app input reset, called after focus_changed on open and close.
	 * Adapted apps can call PAD_reset() here so a held key does not leak into
	 * the other focus owner. NULL is allowed. */
	void (*input_reset)(void* user);

	/* Optional device service hooks. NULL means no device service is
	 * connected and the overlay must show the actions as unavailable. */
	const UIOverlayDeviceHooks* device;
} OverlayAppHooks;

/*
 * UI copy is supplied by the caller. The built-in defaults are English and
 * exist so a host demo can run; they are not part of A3's translation key
 * table. The i18n owner can pass translated strings without changing this
 * module. UIOverlay_create() copies the struct and its pointers, not the
 * pointed-to string storage: the string buffers must outlive the overlay.
 */
typedef struct
{
	const char* title;
	const char* resume;
	const char* save;
	const char* wifi;
	const char* bluetooth;
	const char* exit_to_desktop;
	const char* close;
	const char* select;
	const char* ready;
	const char* no_state;
	const char* no_changes;
	const char* unavailable;
	const char* not_connected;
	const char* on;
	const char* off;
	const char* saved;
	const char* resumed;
	const char* failed;
	/* Optional footer override. The defaults keep the adapted-app B/A copy;
	 * a resident host that owns different keys can pass its own labels. */
	const char* footer_left_button;
	const char* footer_left_hint;
	const char* footer_right_button;
	const char* footer_right_hint;
} UIOverlayStrings;

typedef enum
{
	UI_OVERLAY_ACTION_NONE = 0,
	UI_OVERLAY_ACTION_RESUME,
	UI_OVERLAY_ACTION_SAVE,
	UI_OVERLAY_ACTION_WIFI,
	UI_OVERLAY_ACTION_BLUETOOTH,
	UI_OVERLAY_ACTION_EXIT,
	UI_OVERLAY_ACTION_CLOSE,
	UI_OVERLAY_ACTION_COUNT
} UIOverlayAction;

typedef enum
{
	UI_OVERLAY_EVENT_NONE = 0,
	UI_OVERLAY_EVENT_UP,
	UI_OVERLAY_EVENT_DOWN,
	UI_OVERLAY_EVENT_ACCEPT,
	UI_OVERLAY_EVENT_CANCEL
} UIOverlayEvent;

typedef enum
{
	UI_OVERLAY_RESULT_IGNORED = 0, /* overlay closed: the app owns the input */
	UI_OVERLAY_RESULT_CONSUMED = 1, /* overlay consumed the input */
	UI_OVERLAY_RESULT_CLOSED = 2    /* this event closed the overlay */
} UIOverlayResult;

typedef struct UIOverlay UIOverlay;

/* Returns a built-in English string table. Do not free it. */
const UIOverlayStrings* UIOverlay_defaultStrings(void);

/*
 * Create an overlay. hooks may be NULL (all callbacks absent); strings may be
 * NULL to use UIOverlay_defaultStrings(). The hooks and device hooks structs
 * are copied, but app_name, status strings and any data they point at must
 * outlive the overlay.
 */
UIOverlay* UIOverlay_create(const OverlayAppHooks* hooks, const UIOverlayStrings* strings);

/* Closes the overlay (restoring focus) and frees it. NULL is a no-op. */
void UIOverlay_destroy(UIOverlay* overlay);

/* Open/close are idempotent. Open refreshes app state, selects the first row,
 * calls focus_changed(1), then input_reset. Close calls focus_changed(0), then
 * input_reset. */
void UIOverlay_open(UIOverlay* overlay);
void UIOverlay_close(UIOverlay* overlay);
int UIOverlay_isOpen(const UIOverlay* overlay);

/*
 * Feed one normalized event. While open, every event is consumed. ACCEPT runs
 * the selected action; CANCEL closes. Returns IGNORED only when the overlay is
 * closed. This is the testable input entry point.
 */
UIOverlayResult UIOverlay_handleEvent(UIOverlay* overlay, UIOverlayEvent event);

/*
 * Convenience for adapted apps: maps PAD just-pressed/repeated Up/Down/A/B to
 * overlay events. Returns 1 when the overlay is/was open and the app must skip
 * its own input this frame, 0 when the app owns input. B cancels; MENU and the
 * FN/HOME keys stay outside this module so the app/system can route them.
 */
int UIOverlay_pollInput(UIOverlay* overlay);

/* Draw the overlay over an app frame. The app must call this last. */
void UIOverlay_draw(UIOverlay* overlay, SDL_Surface* screen);

/*
 * Draw only the centred panel (body, header, rows and message). The resident
 * system overlay uses this for a selection move so it does not repaint the
 * footer and hardware group on every keypress. The caller is responsible for
 * clearing the panel rectangle first; UIOverlay_getPanelRect() returns it.
 * Does nothing while the overlay is closed.
 */
void UIOverlay_drawPanel(UIOverlay* overlay, SDL_Surface* screen);

/* Screen rectangle UIOverlay_draw()/UIOverlay_drawPanel() use for the panel.
 * Returns 1 and fills rect while the overlay is open, 0 otherwise. */
int UIOverlay_getPanelRect(const UIOverlay* overlay, const SDL_Surface* screen, SDL_Rect* rect);

/*
 * The resident system overlay composites its own full-screen dim with a disp2
 * colour layer, so it hides the software wash instead of blending 786k pixels
 * on every change. The default is visible.
 */
void UIOverlay_setDimVisible(UIOverlay* overlay, int visible);

/* Update the optional app name shown at the right of the panel header. The
 * pointer must outlive the overlay; NULL clears it. */
void UIOverlay_setAppName(UIOverlay* overlay, const char* app_name);

/* The resident host may draw its own bottom hint bar (for example because it
 * owns the actual key set). Hiding the built-in footer leaves the panel and
 * dim in place. The default is visible. */
void UIOverlay_setFooterVisible(UIOverlay* overlay, int visible);

/* Read-only row state, useful to tests and to apps that mirror the panel. */
int UIOverlay_actionVisible(const UIOverlay* overlay, UIOverlayAction action);
int UIOverlay_actionEnabled(const UIOverlay* overlay, UIOverlayAction action);
const char* UIOverlay_actionStatus(const UIOverlay* overlay, UIOverlayAction action);
UIOverlayAction UIOverlay_selectedAction(const UIOverlay* overlay);

#endif
