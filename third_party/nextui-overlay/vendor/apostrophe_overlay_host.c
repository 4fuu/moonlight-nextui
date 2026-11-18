#include "defines.h"
#include "api.h"

#include "apostrophe.h"
#include "apostrophe_host.h"

#include "apostrophe_overlay_host.h"

#include <stdlib.h>
#include <string.h>

struct ApostropheOverlayHost
{
	ApostropheOverlayConfig config;
	UIOverlay* overlay;

	SDL_Surface* target;
	SDL_Texture* texture;
	int width;
	int height;

	int owns_gfx;
};

static void host_release_target(ApostropheOverlayHost* host)
{
	if (host->texture)
	{
		SDL_DestroyTexture(host->texture);
		host->texture = NULL;
	}
	if (host->target)
	{
		SDL_FreeSurface(host->target);
		host->target = NULL;
	}
	host->width = 0;
	host->height = 0;
}

static void host_focus_changed(void* user, int overlay_has_focus)
{
	ApostropheOverlayHost* host = (ApostropheOverlayHost*)user;

	/* B5 calls this exactly once per real open/close transition. */
	ap_host_overlay_changed(overlay_has_focus != 0);

	if (host->config.focus_changed)
		host->config.focus_changed(host->config.user, overlay_has_focus);
}

static void host_input_reset(void* user)
{
	ApostropheOverlayHost* host = (ApostropheOverlayHost*)user;

	/* Both input owners: NextUI's pad latches and Apostrophe's queue/held
	 * state. ap_host_overlay_changed() also resets Apostrophe idempotently. */
	PAD_reset();
	ap_reset_input();

	if (host->config.input_reset)
		host->config.input_reset(host->config.user);
}

static int host_can_save(void* user)
{
	ApostropheOverlayHost* host = (ApostropheOverlayHost*)user;
	if (host->config.can_save) return host->config.can_save(host->config.user);
	return host->config.save != NULL;
}

static int host_save(void* user)
{
	ApostropheOverlayHost* host = (ApostropheOverlayHost*)user;
	return host->config.save ? host->config.save(host->config.user) : 0;
}

static int host_can_resume(void* user)
{
	ApostropheOverlayHost* host = (ApostropheOverlayHost*)user;
	return host->config.can_resume ? host->config.can_resume(host->config.user) : 0;
}

static int host_resume(void* user)
{
	ApostropheOverlayHost* host = (ApostropheOverlayHost*)user;
	return host->config.resume ? host->config.resume(host->config.user) : 0;
}

static int host_exit_to_desktop(void* user)
{
	ApostropheOverlayHost* host = (ApostropheOverlayHost*)user;

	if (host->config.exit_to_desktop &&
		!host->config.exit_to_desktop(host->config.user))
		return 0;

	return ap_host_request_quit();
}

ApostropheOverlayHost* ApostropheOverlayHost_create(const ApostropheOverlayConfig* config)
{
	ApostropheOverlayHost* host = (ApostropheOverlayHost*)calloc(1, sizeof(ApostropheOverlayHost));
	if (!host) return NULL;

	if (config) host->config = *config;

	int width = ap_get_screen_width();
	int height = ap_get_screen_height();
	if (width <= 0 || height <= 0)
	{
		LOG_error("ApostropheOverlayHost: invalid Apostrophe screen %dx%d\n", width, height);
		free(host);
		return NULL;
	}

	if (!GFX_initAttached(width, height))
	{
		LOG_error("ApostropheOverlayHost: GFX_initAttached(%d, %d) failed\n", width, height);
		free(host);
		return NULL;
	}
	host->owns_gfx = 1;

	OverlayAppHooks hooks;
	memset(&hooks, 0, sizeof(hooks));
	hooks.user = host;
	hooks.capabilities = host->config.capabilities;
	hooks.app_name = host->config.app_name;
	hooks.can_save = host_can_save;
	hooks.save = host_save;
	hooks.can_resume = host_can_resume;
	hooks.resume = host_resume;
	hooks.exit_to_desktop = host_exit_to_desktop;
	hooks.focus_changed = host_focus_changed;
	hooks.input_reset = host_input_reset;
	hooks.device = host->config.device;

	UIOverlayStrings strings = *UIOverlay_defaultStrings();
	if (host->config.title) strings.title = host->config.title;

	host->overlay = UIOverlay_create(&hooks, &strings);
	if (!host->overlay)
	{
		LOG_error("ApostropheOverlayHost: UIOverlay_create failed\n");
		GFX_quitAttached();
		free(host);
		return NULL;
	}

	return host;
}

void ApostropheOverlayHost_destroy(ApostropheOverlayHost* host)
{
	if (!host) return;

	if (host->overlay)
	{
		UIOverlay_destroy(host->overlay); /* closes first, a real transition */
		host->overlay = NULL;
	}

	host_release_target(host);

	if (host->owns_gfx)
	{
		GFX_quitAttached();
		host->owns_gfx = 0;
	}

	free(host);
}

void ApostropheOverlayHost_open(ApostropheOverlayHost* host)
{
	if (host && host->overlay) UIOverlay_open(host->overlay);
}

void ApostropheOverlayHost_close(ApostropheOverlayHost* host)
{
	if (host && host->overlay) UIOverlay_close(host->overlay);
}

void ApostropheOverlayHost_toggle(ApostropheOverlayHost* host)
{
	if (!host || !host->overlay) return;
	if (UIOverlay_isOpen(host->overlay))
		UIOverlay_close(host->overlay);
	else
		UIOverlay_open(host->overlay);
}

int ApostropheOverlayHost_isOpen(const ApostropheOverlayHost* host)
{
	return host && host->overlay && UIOverlay_isOpen(host->overlay);
}

int ApostropheOverlayHost_poll(ApostropheOverlayHost* host)
{
	if (!host || !host->overlay)
		return ap_host_quit_requested() ? 1 : 0;

	if (UIOverlay_isOpen(host->overlay))
	{
		/* While the overlay owns focus NextUI PAD is the input source. The app
		 * does not poll Apostrophe input this frame. */
		PAD_poll();
		UIOverlay_pollInput(host->overlay);
	}

	return UIOverlay_isOpen(host->overlay) || ap_host_quit_requested();
}

static int host_ensure_target(ApostropheOverlayHost* host, SDL_Renderer* renderer,
	int width, int height)
{
	if (host->target && host->texture && host->width == width && host->height == height)
		return 1;

	host_release_target(host);

	host->target = GFX_createScreenFormatSurface(width, height);
	if (!host->target)
	{
		LOG_error("ApostropheOverlayHost: target surface %dx%d failed: %s\n", width, height, SDL_GetError());
		return 0;
	}

	host->texture = SDL_CreateTexture(renderer, host->target->format->format,
		SDL_TEXTUREACCESS_STREAMING, width, height);
	if (!host->texture)
	{
		LOG_error("ApostropheOverlayHost: texture %dx%d failed: %s\n", width, height, SDL_GetError());
		host_release_target(host);
		return 0;
	}

	SDL_SetTextureBlendMode(host->texture, SDL_BLENDMODE_BLEND);
	host->width = width;
	host->height = height;
	return 1;
}

int ApostropheOverlayHost_draw(ApostropheOverlayHost* host, SDL_Renderer* renderer)
{
	if (!host || !host->overlay || !renderer) return 0;
	if (!UIOverlay_isOpen(host->overlay)) return 0;

	int width = ap_get_screen_width();
	int height = ap_get_screen_height();
	if (width <= 0 || height <= 0) return 0;

	if (!host->target || !host->texture || host->width != width || host->height != height)
	{
		if (!host_ensure_target(host, renderer, width, height)) return 0;
	}

	/* The overlay draws last over the application frame, into a transparent
	 * surface that is uploaded and composited before Apostrophe presents. */
	SDL_FillRect(host->target, NULL, 0);
	UIOverlay_draw(host->overlay, host->target);

	if (SDL_UpdateTexture(host->texture, NULL, host->target->pixels, host->target->pitch) != 0)
	{
		LOG_error("ApostropheOverlayHost: SDL_UpdateTexture failed: %s\n", SDL_GetError());
		return 0;
	}

	if (SDL_RenderCopy(renderer, host->texture, NULL, NULL) != 0)
	{
		LOG_error("ApostropheOverlayHost: SDL_RenderCopy failed: %s\n", SDL_GetError());
		return 0;
	}

	return 1;
}

int ApostropheOverlayHost_requestQuit(ApostropheOverlayHost* host)
{
	(void)host;
	return ap_host_request_quit();
}

UIOverlay* ApostropheOverlayHost_overlay(ApostropheOverlayHost* host)
{
	return host ? host->overlay : NULL;
}
