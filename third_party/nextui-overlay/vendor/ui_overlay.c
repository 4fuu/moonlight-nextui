#include "defines.h"
#include "api.h"
#include "ui_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_OVERLAY_MAX_ITEMS 8
#define UI_OVERLAY_MESSAGE_MS 1400

typedef struct
{
	UIOverlayAction action;
	int enabled;
	int asset; /* resolved at refresh time; -1 means no icon */
	const char* label;
	const char* status;
} OverlayItem;

struct UIOverlay
{
	OverlayAppHooks hooks;
	UIOverlayDeviceHooks device; /* owned copy when hooks.device is set */
	UIOverlayStrings strings;

	int open;
	int selected;
	int item_count;
	OverlayItem items[UI_OVERLAY_MAX_ITEMS];

	char message[128];
	uint32_t message_until;

	SDL_Surface* dim;
	int dim_width;
	int dim_height;
	int dim_visible;
	int footer_visible;
};

static const UIOverlayStrings default_strings = {
	.title = "System",
	.resume = "Resume",
	.save = "Save state",
	.wifi = "Wi-Fi",
	.bluetooth = "Bluetooth",
	.exit_to_desktop = "Exit to desktop",
	.close = "Close",
	.select = "Select",
	.ready = "Ready",
	.no_state = "No saved state",
	.no_changes = "No changes",
	.unavailable = "Unavailable",
	.not_connected = "Not connected",
	.on = "On",
	.off = "Off",
	.saved = "Saved",
	.resumed = "Resumed",
	.failed = "Failed",
	.footer_left_button = "B",
	.footer_left_hint = "Close",
	.footer_right_button = "A",
	.footer_right_hint = "Select",
};

const UIOverlayStrings* UIOverlay_defaultStrings(void)
{
	return &default_strings;
}

static const char* pick_string(const char* value, const char* fallback)
{
	return value ? value : fallback;
}

static void copy_strings(UIOverlayStrings* dst, const UIOverlayStrings* src)
{
	const UIOverlayStrings* fallback = &default_strings;
	if (!src) src = fallback;

	dst->title = pick_string(src->title, fallback->title);
	dst->resume = pick_string(src->resume, fallback->resume);
	dst->save = pick_string(src->save, fallback->save);
	dst->wifi = pick_string(src->wifi, fallback->wifi);
	dst->bluetooth = pick_string(src->bluetooth, fallback->bluetooth);
	dst->exit_to_desktop = pick_string(src->exit_to_desktop, fallback->exit_to_desktop);
	dst->close = pick_string(src->close, fallback->close);
	dst->select = pick_string(src->select, fallback->select);
	dst->ready = pick_string(src->ready, fallback->ready);
	dst->no_state = pick_string(src->no_state, fallback->no_state);
	dst->no_changes = pick_string(src->no_changes, fallback->no_changes);
	dst->unavailable = pick_string(src->unavailable, fallback->unavailable);
	dst->not_connected = pick_string(src->not_connected, fallback->not_connected);
	dst->on = pick_string(src->on, fallback->on);
	dst->off = pick_string(src->off, fallback->off);
	dst->saved = pick_string(src->saved, fallback->saved);
	dst->resumed = pick_string(src->resumed, fallback->resumed);
	dst->failed = pick_string(src->failed, fallback->failed);
	dst->footer_left_button = pick_string(src->footer_left_button, fallback->footer_left_button);
	dst->footer_left_hint = pick_string(src->footer_left_hint, dst->close);
	dst->footer_right_button = pick_string(src->footer_right_button, fallback->footer_right_button);
	dst->footer_right_hint = pick_string(src->footer_right_hint, dst->select);
}

static int device_reachable(const UIOverlay* overlay)
{
	const UIOverlayDeviceHooks* device = overlay->hooks.device;
	if (!device) return 0;
	if (device->available && !device->available(device->user)) return 0;
	return 1;
}

static void add_item(UIOverlay* overlay, UIOverlayAction action, int enabled, int asset,
					 const char* label, const char* status)
{
	if (overlay->item_count >= UI_OVERLAY_MAX_ITEMS) return;
	OverlayItem* item = &overlay->items[overlay->item_count++];
	item->action = action;
	item->enabled = enabled;
	item->asset = asset;
	item->label = label;
	item->status = status;
}

static int find_item(const UIOverlay* overlay, UIOverlayAction action);

static void refresh_items(UIOverlay* overlay)
{
	const OverlayAppHooks* hooks = &overlay->hooks;
	const UIOverlayStrings* strings = &overlay->strings;

	overlay->item_count = 0;
	int needs_device = hooks->capabilities & (UI_OVERLAY_CAP_WIFI | UI_OVERLAY_CAP_BLUETOOTH);
	int device_ok = needs_device ? device_reachable(overlay) : 0;

	if (hooks->capabilities & UI_OVERLAY_CAP_RESUME)
	{
		int can = hooks->can_resume ? hooks->can_resume(hooks->user) : 0;
		int enabled = hooks->resume && can;
		const char* status = !hooks->resume ? strings->unavailable : (can ? strings->ready : strings->no_state);
		add_item(overlay, UI_OVERLAY_ACTION_RESUME, enabled, ASSET_GAMEPAD, strings->resume, status);
	}

	if (hooks->capabilities & UI_OVERLAY_CAP_SAVE)
	{
		int can = hooks->can_save ? hooks->can_save(hooks->user) : (hooks->save != NULL);
		int enabled = hooks->save && can;
		const char* status = !hooks->save ? strings->unavailable : (can ? strings->ready : strings->no_changes);
		add_item(overlay, UI_OVERLAY_ACTION_SAVE, enabled, ASSET_CHECKCIRCLE, strings->save, status);
	}

	if (hooks->capabilities & UI_OVERLAY_CAP_WIFI)
	{
		const UIOverlayDeviceHooks* device = hooks->device;
		int enabled = 0;
		int on = 0;
		const char* status = strings->unavailable;

		if (device && device->wifi_get)
		{
			if (!device_ok)
			{
				status = strings->not_connected;
			}
			else
			{
				int value = device->wifi_get(device->user);
				if (value < 0)
					status = strings->not_connected;
				else
				{
					on = value;
					status = value ? strings->on : strings->off;
					enabled = device->wifi_set != NULL;
				}
			}
		}
		add_item(overlay, UI_OVERLAY_ACTION_WIFI, enabled, on ? ASSET_WIFI : ASSET_WIFI_OFF,
				 strings->wifi, status);
	}

	if (hooks->capabilities & UI_OVERLAY_CAP_BLUETOOTH)
	{
		const UIOverlayDeviceHooks* device = hooks->device;
		int enabled = 0;
		int on = 0;
		const char* status = strings->unavailable;

		if (device && device->bluetooth_get)
		{
			if (!device_ok)
			{
				status = strings->not_connected;
			}
			else
			{
				int value = device->bluetooth_get(device->user);
				if (value < 0)
					status = strings->not_connected;
				else
				{
					on = value;
					status = value ? strings->on : strings->off;
					enabled = device->bluetooth_set != NULL;
				}
			}
		}
		add_item(overlay, UI_OVERLAY_ACTION_BLUETOOTH, enabled, on ? ASSET_BLUETOOTH : ASSET_BLUETOOTH_OFF,
				 strings->bluetooth, status);
	}

	if (hooks->capabilities & UI_OVERLAY_CAP_EXIT)
	{
		int enabled = hooks->exit_to_desktop != NULL;
		add_item(overlay, UI_OVERLAY_ACTION_EXIT, enabled, ASSET_POWEROFF, strings->exit_to_desktop,
				 enabled ? strings->ready : strings->unavailable);
	}

	add_item(overlay, UI_OVERLAY_ACTION_CLOSE, 1,
			 overlay->hooks.close_first ? ASSET_GAMEPAD : -1, strings->close, NULL);
	if (overlay->hooks.close_first)
	{
		int close_index = find_item(overlay, UI_OVERLAY_ACTION_CLOSE);
		if (close_index > 0)
		{
			OverlayItem item = overlay->items[close_index];
			memmove(&overlay->items[1], &overlay->items[0],
					sizeof(OverlayItem) * (size_t)close_index);
			overlay->items[0] = item;
		}
		int exit_index = find_item(overlay, UI_OVERLAY_ACTION_EXIT);
		if (exit_index > 1)
		{
			OverlayItem item = overlay->items[exit_index];
			memmove(&overlay->items[2], &overlay->items[1],
					sizeof(OverlayItem) * (size_t)(exit_index - 1));
			overlay->items[1] = item;
		}
	}

	if (overlay->selected >= overlay->item_count) overlay->selected = overlay->item_count - 1;
	if (overlay->selected < 0) overlay->selected = 0;
}

static int find_item(const UIOverlay* overlay, UIOverlayAction action)
{
	for (int i = 0; i < overlay->item_count; i++)
	{
		if (overlay->items[i].action == action) return i;
	}
	return -1;
}

static void set_message(UIOverlay* overlay, const char* text)
{
	if (!text) return;
	strncpy(overlay->message, text, sizeof(overlay->message) - 1);
	overlay->message[sizeof(overlay->message) - 1] = '\0';
	overlay->message_until = SDL_GetTicks() + UI_OVERLAY_MESSAGE_MS;
}

static void activate_item(UIOverlay* overlay)
{
	if (overlay->selected < 0 || overlay->selected >= overlay->item_count) return;

	OverlayItem* item = &overlay->items[overlay->selected];
	if (!item->enabled) return;

	const OverlayAppHooks* hooks = &overlay->hooks;

	switch (item->action)
	{
	case UI_OVERLAY_ACTION_RESUME:
	{
		int can = hooks->can_resume ? hooks->can_resume(hooks->user) : 0;
		if (!hooks->resume || !can) break;
		int ok = hooks->resume(hooks->user);
		set_message(overlay, ok ? overlay->strings.resumed : overlay->strings.failed);
		refresh_items(overlay);
		break;
	}
	case UI_OVERLAY_ACTION_SAVE:
	{
		int can = hooks->can_save ? hooks->can_save(hooks->user) : (hooks->save != NULL);
		if (!hooks->save || !can) break;
		int ok = hooks->save(hooks->user);
		set_message(overlay, ok ? overlay->strings.saved : overlay->strings.failed);
		refresh_items(overlay);
		break;
	}
	case UI_OVERLAY_ACTION_WIFI:
	{
		const UIOverlayDeviceHooks* device = hooks->device;
		if (!device || !device->wifi_get || !device->wifi_set || !device_reachable(overlay)) break;
		int before = device->wifi_get(device->user);
		if (before < 0) break;
		int rc = device->wifi_set(device->user, !before);
		int after = device->wifi_get(device->user);
		if (rc != 0 || after < 0) set_message(overlay, overlay->strings.failed);
		refresh_items(overlay);
		break;
	}
	case UI_OVERLAY_ACTION_BLUETOOTH:
	{
		const UIOverlayDeviceHooks* device = hooks->device;
		if (!device || !device->bluetooth_get || !device->bluetooth_set || !device_reachable(overlay)) break;
		int before = device->bluetooth_get(device->user);
		if (before < 0) break;
		int rc = device->bluetooth_set(device->user, !before);
		int after = device->bluetooth_get(device->user);
		if (rc != 0 || after < 0) set_message(overlay, overlay->strings.failed);
		refresh_items(overlay);
		break;
	}
	case UI_OVERLAY_ACTION_EXIT:
		if (!hooks->exit_to_desktop) break;
		if (hooks->exit_to_desktop(hooks->user))
			UIOverlay_close(overlay);
		else
			set_message(overlay, overlay->strings.failed);
		break;
	case UI_OVERLAY_ACTION_CLOSE:
		UIOverlay_close(overlay);
		break;
	default:
		break;
	}
}

UIOverlay* UIOverlay_create(const OverlayAppHooks* hooks, const UIOverlayStrings* strings)
{
	UIOverlay* overlay = (UIOverlay*)calloc(1, sizeof(UIOverlay));
	if (!overlay) return NULL;

	if (hooks) overlay->hooks = *hooks;
	if (overlay->hooks.device)
	{
		overlay->device = *overlay->hooks.device;
		overlay->hooks.device = &overlay->device;
	}

	copy_strings(&overlay->strings, strings);
	overlay->footer_visible = 1;
	overlay->dim_visible = 1;
	overlay->selected = 0;
	refresh_items(overlay);

	return overlay;
}

void UIOverlay_destroy(UIOverlay* overlay)
{
	if (!overlay) return;
	if (overlay->open) UIOverlay_close(overlay);
	if (overlay->dim) SDL_FreeSurface(overlay->dim);
	free(overlay);
}

void UIOverlay_open(UIOverlay* overlay)
{
	if (!overlay || overlay->open) return;

	refresh_items(overlay);
	overlay->open = 1;
	overlay->selected = 0;
	overlay->message[0] = '\0';
	overlay->message_until = 0;

	if (overlay->hooks.focus_changed) overlay->hooks.focus_changed(overlay->hooks.user, 1);
	if (overlay->hooks.input_reset) overlay->hooks.input_reset(overlay->hooks.user);
}

void UIOverlay_close(UIOverlay* overlay)
{
	if (!overlay || !overlay->open) return;

	overlay->open = 0;
	overlay->message[0] = '\0';
	overlay->message_until = 0;

	if (overlay->hooks.focus_changed) overlay->hooks.focus_changed(overlay->hooks.user, 0);
	if (overlay->hooks.input_reset) overlay->hooks.input_reset(overlay->hooks.user);
}

int UIOverlay_isOpen(const UIOverlay* overlay)
{
	return overlay && overlay->open;
}

UIOverlayResult UIOverlay_handleEvent(UIOverlay* overlay, UIOverlayEvent event)
{
	if (!overlay || !overlay->open) return UI_OVERLAY_RESULT_IGNORED;

	switch (event)
	{
	case UI_OVERLAY_EVENT_UP:
		if (overlay->item_count > 0)
			overlay->selected = (overlay->selected + overlay->item_count - 1) % overlay->item_count;
		return UI_OVERLAY_RESULT_CONSUMED;
	case UI_OVERLAY_EVENT_DOWN:
		if (overlay->item_count > 0)
			overlay->selected = (overlay->selected + 1) % overlay->item_count;
		return UI_OVERLAY_RESULT_CONSUMED;
	case UI_OVERLAY_EVENT_ACCEPT:
		activate_item(overlay);
		return overlay->open ? UI_OVERLAY_RESULT_CONSUMED : UI_OVERLAY_RESULT_CLOSED;
	case UI_OVERLAY_EVENT_CANCEL:
		UIOverlay_close(overlay);
		return UI_OVERLAY_RESULT_CLOSED;
	case UI_OVERLAY_EVENT_NONE:
	default:
		return UI_OVERLAY_RESULT_CONSUMED;
	}
}

int UIOverlay_pollInput(UIOverlay* overlay)
{
	if (!overlay || !overlay->open) return 0;

	UIOverlayEvent event = UI_OVERLAY_EVENT_NONE;
	if (PAD_justPressed(BTN_B))
		event = UI_OVERLAY_EVENT_CANCEL;
	else if (PAD_justPressed(BTN_A))
		event = UI_OVERLAY_EVENT_ACCEPT;
	else if (PAD_justRepeated(BTN_UP))
		event = UI_OVERLAY_EVENT_UP;
	else if (PAD_justRepeated(BTN_DOWN))
		event = UI_OVERLAY_EVENT_DOWN;

	UIOverlay_handleEvent(overlay, event);
	return 1;
}

static void ensure_dim(UIOverlay* overlay, SDL_Surface* screen)
{
	if (overlay->dim && overlay->dim_width == screen->w && overlay->dim_height == screen->h) return;

	if (overlay->dim)
	{
		SDL_FreeSurface(overlay->dim);
		overlay->dim = NULL;
	}

	overlay->dim = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h,
												  screen->format->BitsPerPixel, screen->format->format);
	overlay->dim_width = screen->w;
	overlay->dim_height = screen->h;
	if (!overlay->dim) return;

	SDL_FillRect(overlay->dim, NULL, SDL_MapRGBA(screen->format, 0, 0, 0, UI_OVERLAY_DIM_ALPHA));
	SDL_SetSurfaceBlendMode(overlay->dim, SDL_BLENDMODE_BLEND);
}

/* SDL_ttf renders a whole line box: the surface is TTF_FontHeight tall, which is
 * larger than the size the font was opened with (and larger still for the CJK
 * font). Center on that measured height rather than the nominal FONT_* value,
 * so text shares the same vertical center as the equally centered icon. */
static int text_height(TTF_Font* font, const char* text)
{
	int height = 0;
	if (font && text && text[0]) TTF_SizeUTF8(font, text, NULL, &height);
	return height;
}

static void blit_text(TTF_Font* font, const char* text, SDL_Color color, SDL_Surface* dst,
					  int x, int y, SDL_Rect* clip)
{
	if (!font || !text || !text[0]) return;

	SDL_Rect old_clip;
	SDL_GetClipRect(dst, &old_clip);
	if (clip) SDL_SetClipRect(dst, clip);

	SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, color);
	if (surface)
	{
		SDL_Rect dst_rect = {x, y, surface->w, surface->h};
		SDL_BlitSurface(surface, NULL, dst, &dst_rect);
		SDL_FreeSurface(surface);
	}

	SDL_SetClipRect(dst, &old_clip);
}

static int panel_geometry(const UIOverlay* overlay, const SDL_Surface* screen, SDL_Rect* rect)
{
	if (!overlay || !overlay->open || !screen || !rect) return 0;

	const int padding = SCALE1(8);
	const int header_height = SCALE1(24);
	const int row_height = SCALE1(22);
	const int message_height = SCALE1(10);
	int panel_width = SCALE1(260);
	if (panel_width > screen->w - SCALE1(20)) panel_width = screen->w - SCALE1(20);
	int panel_height = padding * 2 + header_height + overlay->item_count * row_height + message_height;
	int panel_x = (screen->w - panel_width) / 2;
	int panel_y = (screen->h - panel_height) / 2;
	if (panel_y < SCALE1(PADDING + PILL_SIZE + PADDING)) panel_y = SCALE1(PADDING + PILL_SIZE + PADDING);

	rect->x = panel_x;
	rect->y = panel_y;
	rect->w = panel_width;
	rect->h = panel_height;
	return 1;
}

/* Panel body, header, rows and message. footer draws the built-in bottom bar
 * between the panel background and its contents, preserving the original
 * UIOverlay_draw() order. */
static void draw_panel(UIOverlay* overlay, SDL_Surface* screen, int footer)
{
	const int padding = SCALE1(8);
	const int header_height = SCALE1(24);
	const int row_height = SCALE1(22);
	SDL_Rect panel;

	if (!panel_geometry(overlay, screen, &panel)) return;
	int panel_x = panel.x;
	int panel_y = panel.y;
	int panel_width = panel.w;
	int panel_height = panel.h;

	GFX_blitRectColor(ASSET_STATE_BG, screen, &(SDL_Rect){panel_x, panel_y, panel_width, panel_height},
					  THEME_COLOR3);

	if (footer)
	{
		int footer_y = screen->h - SCALE1(PADDING + PILL_SIZE);
		SDL_FillRect(screen, &(SDL_Rect){0, footer_y, screen->w, screen->h - footer_y},
					 THEME_COLOR7);
	}

	int header_x = panel_x + padding;
	int header_y = panel_y + padding;

	SDL_Rect settings_rect;
	GFX_assetRect(ASSET_SETTINGS, &settings_rect);
	int settings_x = header_x;
	int settings_y = header_y + (header_height - settings_rect.h) / 2;
	GFX_blitAssetColor(ASSET_SETTINGS, NULL, screen, &(SDL_Rect){settings_x, settings_y, 0, 0}, THEME_COLOR6);

	int title_x = header_x + settings_rect.w + SCALE1(6);
	int title_y = header_y + (header_height - text_height(font.large, overlay->strings.title)) / 2;
	SDL_Rect title_clip = {title_x, header_y, panel_width - (title_x - panel_x) - padding, header_height};
	blit_text(font.large, overlay->strings.title, uintToColour(THEME_COLOR4_255), screen, title_x, title_y,
			  &title_clip);

	if (overlay->hooks.app_name && overlay->hooks.app_name[0])
	{
		int name_width = 0;
		TTF_SizeUTF8(font.small, overlay->hooks.app_name, &name_width, NULL);
		int name_x = panel_x + panel_width - padding - name_width;
		if (name_x < title_x + SCALE1(8)) name_x = title_x + SCALE1(8);
		SDL_Rect name_clip = {name_x, header_y, panel_x + panel_width - padding - name_x, header_height};
		int name_y = header_y + (header_height - text_height(font.small, overlay->hooks.app_name)) / 2;
		blit_text(font.small, overlay->hooks.app_name, uintToColour(THEME_COLOR6_255), screen, name_x, name_y,
				  &name_clip);
	}

	int row_x = panel_x + padding;
	int row_width = panel_width - padding * 2;
	int row_y = panel_y + padding + header_height;

	for (int i = 0; i < overlay->item_count; i++)
	{
		OverlayItem* item = &overlay->items[i];
		int selected = (i == overlay->selected);
		SDL_Rect row = {row_x, row_y, row_width, row_height - SCALE1(2)};
		if (selected)
			GFX_blitRectColor(ASSET_STATE_BG, screen, &row, THEME_COLOR1);

		SDL_Color text_color = uintToColour(selected ? THEME_COLOR5_255
													  : (item->enabled ? THEME_COLOR4_255 : THEME_COLOR6_255));
		uint32_t icon_color = selected ? THEME_COLOR5 : (item->enabled ? THEME_COLOR4 : THEME_COLOR6);

		int label_x = row.x + SCALE1(8);
		SDL_Rect icon_slot;
		GFX_assetRect(ASSET_GAMEPAD, &icon_slot);
		label_x += icon_slot.w + SCALE1(7);
		int icon = item->asset;
		if (icon >= 0)
		{
			SDL_Rect icon_rect;
			GFX_assetRect(icon, &icon_rect);
			int icon_x = row.x + SCALE1(7);
			int icon_y = row.y + (row.h - icon_rect.h) / 2;
			GFX_blitAssetColor(icon, NULL, screen, &(SDL_Rect){icon_x, icon_y, 0, 0}, icon_color);
		}

		int label_y = row.y + (row.h - text_height(font.medium, item->label)) / 2;

		int status_width = 0;
		if (item->status && item->status[0])
			TTF_SizeUTF8(font.small, item->status, &status_width, NULL);
		int status_x = row.x + row.w - SCALE1(8) - status_width;

		SDL_Rect label_clip = {label_x, row.y, status_x - label_x - SCALE1(6), row.h};
		blit_text(font.medium, item->label, text_color, screen, label_x, label_y, &label_clip);

		if (item->status && item->status[0])
		{
			int status_y = row.y + (row.h - text_height(font.small, item->status)) / 2;
			SDL_Rect status_clip = {status_x, row.y, row.x + row.w - SCALE1(8) - status_x, row.h};
			blit_text(font.small, item->status, text_color, screen, status_x, status_y, &status_clip);
		}

		row_y += row_height;
	}

	if (overlay->message[0] && SDL_GetTicks() < overlay->message_until)
	{
		int message_width = 0;
		TTF_SizeUTF8(font.small, overlay->message, &message_width, NULL);
		int message_x = panel_x + (panel_width - message_width) / 2;
		int message_y = panel_y + panel_height - padding - SCALE1(FONT_SMALL);
		blit_text(font.small, overlay->message, uintToColour(THEME_COLOR6_255), screen, message_x, message_y, NULL);
	}
}

void UIOverlay_draw(UIOverlay* overlay, SDL_Surface* screen)
{
	if (!overlay || !overlay->open || !screen) return;
	if (overlay->item_count == 0) refresh_items(overlay);

	if (overlay->dim_visible)
	{
		ensure_dim(overlay, screen);
		if (overlay->dim) SDL_BlitSurface(overlay->dim, NULL, screen, NULL);
	}

	draw_panel(overlay, screen, overlay->footer_visible);

	GFX_blitHardwareGroup(screen, 0);

	if (overlay->footer_visible)
	{
		char* close_hint[] = {(char*)overlay->strings.footer_left_button,
							  (char*)overlay->strings.footer_left_hint, NULL};
		char* select_hint[] = {(char*)overlay->strings.footer_right_button,
							   (char*)overlay->strings.footer_right_hint, NULL};
		GFX_blitButtonGroup(close_hint, 0, screen, 0);
		GFX_blitButtonGroup(select_hint, 0, screen, 1);
	}
}

void UIOverlay_drawPanel(UIOverlay* overlay, SDL_Surface* screen)
{
	if (!overlay || !overlay->open || !screen) return;
	if (overlay->item_count == 0) refresh_items(overlay);
	draw_panel(overlay, screen, 0);
}

int UIOverlay_getPanelRect(const UIOverlay* overlay, const SDL_Surface* screen, SDL_Rect* rect)
{
	return panel_geometry(overlay, screen, rect);
}

void UIOverlay_setDimVisible(UIOverlay* overlay, int visible)
{
	if (overlay) overlay->dim_visible = visible ? 1 : 0;
}

void UIOverlay_setFooterVisible(UIOverlay* overlay, int visible)
{
	if (overlay) overlay->footer_visible = visible ? 1 : 0;
}

void UIOverlay_setAppName(UIOverlay* overlay, const char* app_name)
{
	if (overlay) overlay->hooks.app_name = app_name;
}

int UIOverlay_actionVisible(const UIOverlay* overlay, UIOverlayAction action)
{
	if (!overlay) return 0;
	return find_item(overlay, action) >= 0;
}

int UIOverlay_actionEnabled(const UIOverlay* overlay, UIOverlayAction action)
{
	if (!overlay) return 0;
	int index = find_item(overlay, action);
	return index >= 0 && overlay->items[index].enabled;
}

const char* UIOverlay_actionStatus(const UIOverlay* overlay, UIOverlayAction action)
{
	if (!overlay) return NULL;
	int index = find_item(overlay, action);
	if (index < 0) return NULL;
	return overlay->items[index].status;
}

UIOverlayAction UIOverlay_selectedAction(const UIOverlay* overlay)
{
	if (!overlay || !overlay->open) return UI_OVERLAY_ACTION_NONE;
	if (overlay->selected < 0 || overlay->selected >= overlay->item_count) return UI_OVERLAY_ACTION_NONE;
	return overlay->items[overlay->selected].action;
}
