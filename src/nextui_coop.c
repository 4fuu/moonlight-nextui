/*
 * NextUI cooperation layer for Moonlight (M1-M6).
 *
 * Menu: the wrapper hosts NextUI's own overlay (dynamic overlay.host true).
 * Streaming: the engine owns fb0, so hosting is yielded and the system
 * fallback draws HOME/HUD; the SDK stays subscribed for sleep inhibit,
 * status and session end.
 *
 * The SDK's polling API must be owned by one thread; this module runs it in a
 * dedicated pthread and publishes UI-relevant events to the main thread as
 * flags. Request calls (overlay/hud/sleep/status) are thread-safe inside the
 * client.
 */

/* defines.h/api.h first: on the device Apostrophe pulls in <linux/input.h>,
 * whose BTN_A macro makes NextUI's defines.h skip its BTN_ID_* enum. Include
 * the enum before any header that can define that macro. */
#include "defines.h"
#include "api.h"

#include "apostrophe.h"
#include "apostrophe_host.h"
#include "apostrophe_overlay_host.h"

#include "nextui_app.h"
#include "nextui_bridge.h"

#include "moonlight.h"
#include "nextui_coop.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Long enough for an idle night, refreshed by nextui_coop_tick(). */
#define STREAM_INHIBIT_MS 300000
#define STREAM_INHIBIT_REFRESH_MS 60000
#define HUD_VISIBLE_MS 1600

static NextuiApp* s_app;
static pthread_t s_thread;
static int s_thread_started;
static atomic_int s_stop;
static atomic_int s_streaming;
static atomic_int s_quit;
static atomic_int s_home_pending;
static atomic_int s_connected;

static ApostropheOverlayHost* s_overlay;
static int s_hosting; /* effective overlay.host we last requested */

static pthread_mutex_t s_status_lock = PTHREAD_MUTEX_INITIALIZER;
static NextuiStatus s_status;
static int s_have_status;

static pthread_mutex_t s_feedback_lock = PTHREAD_MUTEX_INITIALIZER;
static NextuiFeedback s_feedback;
static int s_feedback_active;
static unsigned long long s_feedback_until;
static unsigned long long s_last_inhibit;

static SDL_Surface* s_hud_surface;
static SDL_Texture* s_hud_texture;
static int s_hud_width;
static int s_hud_height;

static unsigned long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}

static void log_line(const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ap_log("nextui: %s", buf);
}

/* ── Status ─────────────────────────────────────────────────────────────── */

static ConnectionStrength strength_from(const NextuiWifi* wifi)
{
    if (!wifi->available || !wifi->enabled)
        return SIGNAL_STRENGTH_OFF;
    if (!wifi->connected)
        return SIGNAL_STRENGTH_DISCONNECTED;
    if (strcmp(wifi->strength, "high") == 0)
        return SIGNAL_STRENGTH_HIGH;
    if (strcmp(wifi->strength, "med") == 0)
        return SIGNAL_STRENGTH_MED;
    if (strcmp(wifi->strength, "low") == 0)
        return SIGNAL_STRENGTH_LOW;
    return SIGNAL_STRENGTH_DISCONNECTED;
}

static void status_apply(const NextuiStatus* patch)
{
    NextuiStatus copy;
    int have;

    pthread_mutex_lock(&s_status_lock);
    if (patch->sections & NEXTUI_APP_SECTION_BATTERY) s_status.battery = patch->battery;
    if (patch->sections & NEXTUI_APP_SECTION_WIFI) s_status.wifi = patch->wifi;
    if (patch->sections & NEXTUI_APP_SECTION_BLUETOOTH) s_status.bluetooth = patch->bluetooth;
    if (patch->sections & NEXTUI_APP_SECTION_VOLUME) s_status.volume = patch->volume;
    if (patch->sections & NEXTUI_APP_SECTION_DISPLAY) s_status.display = patch->display;
    if (patch->sections & NEXTUI_APP_SECTION_THEME) s_status.theme = patch->theme;
    if (patch->sections & NEXTUI_APP_SECTION_CLOCK) s_status.clock = patch->clock;
    s_status.sections |= patch->sections;
    copy = s_status;
    have = s_have_status;
    s_have_status = 1;
    pthread_mutex_unlock(&s_status_lock);

    (void)have;
    if (copy.battery.available)
        nextui_bridge_set_battery(copy.battery.percent, copy.battery.charging);

    nextui_bridge_set_network(strength_from(&copy.wifi),
                              copy.wifi.available && copy.wifi.enabled,
                              copy.bluetooth.available && copy.bluetooth.connected,
                              copy.bluetooth.available && copy.bluetooth.enabled);

    if (copy.volume.available)
        nextui_bridge_set_audio(copy.volume.level, copy.volume.mute, copy.volume.locked);

    if (copy.display.available)
        nextui_bridge_set_display(copy.display.brightness, copy.display.colortemp,
                                  copy.display.brightness_locked,
                                  copy.display.colortemp_locked);
}

static void status_refresh(void)
{
    if (!s_app || !nextui_app_is_connected(s_app)) return;
    NextuiStatus status;
    if (nextui_app_status_get(s_app, NEXTUI_APP_SECTION_ALL, &status) == NEXTUI_APP_OK)
        status_apply(&status);
}

/* ── Overlay device rows ────────────────────────────────────────────────── */

static int overlay_available(void* user)
{
    (void)user;
    return s_have_status;
}

static int overlay_wifi_get(void* user)
{
    (void)user;
    pthread_mutex_lock(&s_status_lock);
    int value = s_status.wifi.available ? (s_status.wifi.enabled ? 1 : 0) : -1;
    pthread_mutex_unlock(&s_status_lock);
    return value;
}

static int overlay_bt_get(void* user)
{
    (void)user;
    pthread_mutex_lock(&s_status_lock);
    int value = s_status.bluetooth.available ? (s_status.bluetooth.enabled ? 1 : 0) : -1;
    pthread_mutex_unlock(&s_status_lock);
    return value;
}

static int overlay_set_toggle(const char* capability, int on)
{
    if (!s_app) return 0;
    char args[32];
    char value[128];
    snprintf(args, sizeof(args), "{\"enabled\":%s}", on ? "true" : "false");
    return nextui_app_call(s_app, capability, "set", args, value, sizeof(value)) ==
           NEXTUI_APP_OK;
}

static int overlay_wifi_set(void* user, int on)
{
    (void)user;
    return overlay_set_toggle("net.wifi", on);
}

static int overlay_bt_set(void* user, int on)
{
    (void)user;
    return overlay_set_toggle("net.bluetooth", on);
}

static int overlay_can_resume(void* user)
{
    (void)user;
    return 1;
}

static int overlay_resume(void* user)
{
    (void)user;
    return 1; /* UIOverlay closes the panel on success */
}

static int overlay_exit_to_desktop(void* user)
{
    (void)user;
    atomic_store(&s_quit, 1);
    return ap_host_request_quit();
}

static void overlay_focus_changed(void* user, int open)
{
    (void)user;
    if (s_app && nextui_app_is_connected(s_app))
        nextui_app_overlay_set_open(s_app, open ? 1 : 0);
    if (open) {
        pthread_mutex_lock(&s_feedback_lock);
        s_feedback_active = 0;
        pthread_mutex_unlock(&s_feedback_lock);
    }
}

static const UIOverlayDeviceHooks s_device_hooks = {
    .user = NULL,
    .available = overlay_available,
    .wifi_get = overlay_wifi_get,
    .wifi_set = overlay_wifi_set,
    .bluetooth_get = overlay_bt_get,
    .bluetooth_set = overlay_bt_set,
};

/* ── HUD ────────────────────────────────────────────────────────────────── */

static void hud_release(void)
{
    if (s_hud_texture) {
        SDL_DestroyTexture(s_hud_texture);
        s_hud_texture = NULL;
    }
    if (s_hud_surface) {
        SDL_FreeSurface(s_hud_surface);
        s_hud_surface = NULL;
    }
    s_hud_width = 0;
    s_hud_height = 0;
}

static void hud_draw(SDL_Renderer* renderer)
{
    if (!renderer || !s_overlay) return;

    NextuiFeedback feedback;
    int active;

    pthread_mutex_lock(&s_feedback_lock);
    active = s_feedback_active && now_ms() < s_feedback_until;
    if (!active) s_feedback_active = 0;
    feedback = s_feedback;
    pthread_mutex_unlock(&s_feedback_lock);

    if (!active) return;

    int width = ap_get_screen_width();
    int height = ap_get_screen_height();
    if (width <= 0 || height <= 0) return;

    if (!s_hud_surface || s_hud_width != width || s_hud_height != height) {
        hud_release();
        s_hud_surface = GFX_createScreenFormatSurface(width, height);
        if (!s_hud_surface) return;
        s_hud_texture = SDL_CreateTexture(renderer, s_hud_surface->format->format,
                                          SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!s_hud_texture) {
            hud_release();
            return;
        }
        SDL_SetTextureBlendMode(s_hud_texture, SDL_BLENDMODE_BLEND);
        s_hud_width = width;
        s_hud_height = height;
    }

    SDL_FillRect(s_hud_surface, NULL, 0);

    IndicatorType type = INDICATOR_VOLUME;
    if (feedback.kind == NEXTUI_APP_HUD_BRIGHTNESS)
        type = INDICATOR_BRIGHTNESS;
    else if (feedback.kind == NEXTUI_APP_HUD_COLORTEMP)
        type = INDICATOR_COLORTEMP;

    int ow = SCALE1(PILL_SIZE + SETTINGS_WIDTH + 10 + 4);
    int ox = width - SCALE1(PADDING) - ow;
    int oy = SCALE1(PADDING);
    GFX_blitHardwareIndicator(s_hud_surface, ox, oy, type);

    SDL_UpdateTexture(s_hud_texture, NULL, s_hud_surface->pixels, s_hud_surface->pitch);
    SDL_RenderCopy(renderer, s_hud_texture, NULL, NULL);
}

/* ── Apostrophe host backend ────────────────────────────────────────────── */

static int home_key_was_down;

static int overlay_create(void)
{
    if (s_overlay) return 1;

    ApostropheOverlayConfig config;
    memset(&config, 0, sizeof(config));
    config.app_name = "Moonlight";
    config.capabilities = UI_OVERLAY_CAP_RESUME | UI_OVERLAY_CAP_EXIT |
                          UI_OVERLAY_CAP_WIFI | UI_OVERLAY_CAP_BLUETOOTH;
    config.device = &s_device_hooks;
    config.can_resume = overlay_can_resume;
    config.resume = overlay_resume;
    config.exit_to_desktop = overlay_exit_to_desktop;
    config.focus_changed = overlay_focus_changed;

    s_overlay = ApostropheOverlayHost_create(&config);
    if (!s_overlay) {
        log_line("overlay renderer unavailable; yielding HOME to the system");
        return 0;
    }
    return 1;
}

static void overlay_destroy(void)
{
    if (!s_overlay) return;
    ApostropheOverlayHost_destroy(s_overlay);
    s_overlay = NULL;
}

static void overlay_close_if_open(void)
{
    if (s_overlay && ApostropheOverlayHost_isOpen(s_overlay))
        ApostropheOverlayHost_close(s_overlay);
}

static void overlay_toggle(void)
{
    if (!overlay_create()) return;
    if (ApostropheOverlayHost_isOpen(s_overlay))
        ApostropheOverlayHost_close(s_overlay);
    else
        ApostropheOverlayHost_open(s_overlay);
}

static int coop_poll(void* user)
{
    (void)user;

    /* HOME arrives as an overlay.home event. Without a devserv connection the
     * manifest still makes us the adapted app, so read KEY_HOMEPAGE ourselves
     * rather than leaving HOME dead. */
    if (atomic_exchange(&s_home_pending, 0)) {
        overlay_toggle();
    } else if (!atomic_load(&s_connected)) {
        int down = PLAT_getHomeKeyPressed();
        if (down && !home_key_was_down)
            overlay_toggle();
        home_key_was_down = down;
    }

    if (s_overlay && ApostropheOverlayHost_isOpen(s_overlay)) {
        ApostropheOverlayHost_poll(s_overlay);
        return 1;
    }

    return atomic_load(&s_quit) ? 1 : 0;
}

static void coop_draw(void* user, SDL_Renderer* renderer)
{
    (void)user;
    if (s_overlay) ApostropheOverlayHost_draw(s_overlay, renderer);
    if (!s_overlay || !ApostropheOverlayHost_isOpen(s_overlay))
        hud_draw(renderer);
}

static void on_host_event(void* user, ap_host_event event)
{
    (void)user;
    if (event == AP_HOST_EVENT_QUIT_REQUESTED)
        atomic_store(&s_quit, 1);
}

static ap_host_backend s_backend = {
    .user = NULL,
    .poll = coop_poll,
    .draw = coop_draw,
};

/* ── SDK events ─────────────────────────────────────────────────────────── */

static void on_event(NextuiApp* app, const NextuiAppEvent* event, void* user)
{
    (void)app;
    (void)user;

    switch (event->type) {
    case NEXTUI_APP_EVENT_OVERLAY_HOME:
        atomic_store(&s_home_pending, 1);
        break;
    case NEXTUI_APP_EVENT_STATUS_SNAPSHOT:
    case NEXTUI_APP_EVENT_STATUS_CHANGED:
        status_apply(&event->data.status);
        break;
    case NEXTUI_APP_EVENT_FEEDBACK:
        pthread_mutex_lock(&s_feedback_lock);
        s_feedback = event->data.feedback;
        s_feedback_active = 1;
        s_feedback_until = now_ms() + HUD_VISIBLE_MS;
        pthread_mutex_unlock(&s_feedback_lock);
        break;
    case NEXTUI_APP_EVENT_WAKE_DONE:
        status_refresh();
        break;
    case NEXTUI_APP_EVENT_OVERFLOW:
        log_line("event overflow; resynchronizing");
        status_refresh();
        break;
    case NEXTUI_APP_EVENT_SESSION_ENDED:
        log_line("session ended (%s)", event->data.session_ended.reason);
        atomic_store(&s_quit, 1);
        break;
    case NEXTUI_APP_EVENT_TRANSPORT_ERROR:
        log_line("transport: %s", event->data.error.detail);
        break;
    default:
        break;
    }
}

static void on_sleep(NextuiApp* app, const NextuiSleepPrepare* prepare,
                     NextuiAppSleepDecision* decision, void* user)
{
    (void)app;
    (void)prepare;
    (void)user;

    if (atomic_load(&s_streaming)) {
        /* The engine must survive an idle sleep; an explicit power press is
         * still delivered, and this prepare is deferred for five minutes. */
        decision->action = NEXTUI_APP_SLEEP_INHIBIT;
        decision->reason = "streaming";
        decision->timeout_ms = STREAM_INHIBIT_MS;
        decision->detail = "Moonlight stream active";
    } else {
        /* Menu has nothing to save; let the system proceed immediately. */
        decision->action = NEXTUI_APP_SLEEP_DEFAULT;
    }
}

static void log_sdk(void* user, int level, const char* message)
{
    (void)user;
    (void)level;
    ap_log("nextui sdk: %s", message);
}

static void* sdk_thread(void* arg)
{
    (void)arg;
    while (!atomic_load(&s_stop)) {
        int dispatched = nextui_app_wait(s_app, 100);
        if (dispatched < 0) {
            if (!nextui_app_is_connected(s_app)) {
                atomic_store(&s_connected, 0);
                usleep(200 * 1000);
            }
        } else if (!atomic_load(&s_connected) && nextui_app_is_connected(s_app)) {
            atomic_store(&s_connected, 1);
        }
    }
    return NULL;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void nextui_coop_init(void)
{
    atomic_store(&s_stop, 0);
    atomic_store(&s_streaming, 0);
    atomic_store(&s_quit, 0);
    atomic_store(&s_home_pending, 0);
    atomic_store(&s_connected, 0);
    s_have_status = 0;
    memset(&s_status, 0, sizeof(s_status));

    PAD_init();

    /* The overlay renderer needs the GFX resources; create it before the host
     * backend can be called from a widget frame. */
    overlay_create();

    ap_host_attach(&s_backend, on_host_event, NULL);

    NextuiAppConfig config;
    memset(&config, 0, sizeof(config));
    config.client_name = "moonlight/0.2.0";
    config.connect_timeout_ms = 1500;
    config.io_timeout_ms = 1500;
    config.log = log_sdk;

    s_app = nextui_app_create(&config);
    if (!s_app) {
        log_line("SDK unavailable; running without cooperation");
        return;
    }

    NextuiAppInfo info;
    memset(&info, 0, sizeof(info));
    NextuiAppStatus status = nextui_app_connect(s_app, &info);
    if (status != NEXTUI_APP_OK) {
        log_line("devserv connect failed (%s); HOME falls back to the system",
                 nextui_app_status_string(status));
        return;
    }

    if (!info.session_active) {
        log_line("no active session; cooperation idle");
    }
    if (info.overlay_host_declared && !info.overlay_host) {
        log_line("session started with overlay host off; restoring");
    }

    nextui_app_set_event_callback(s_app, on_event, NULL);
    nextui_app_set_sleep_handler(s_app, on_sleep, NULL);

    /* Mute keeps the system HUD (the app has no mute icon in its own HUD). */
    unsigned app_hud = NEXTUI_APP_HUD_VOLUME | NEXTUI_APP_HUD_BRIGHTNESS |
                       NEXTUI_APP_HUD_COLORTEMP;
    (void)nextui_app_hud_set_policy(s_app, NEXTUI_APP_HUD_ALL, NEXTUI_APP_HUD_SYSTEM);
    (void)nextui_app_hud_set_policy(s_app, app_hud, NEXTUI_APP_HUD_APP);

    s_hosting = 1;
    (void)nextui_app_overlay_set_host(s_app, 1);

    status_refresh();

    if (pthread_create(&s_thread, NULL, sdk_thread, NULL) == 0) {
        s_thread_started = 1;
        atomic_store(&s_connected, 1);
        log_line("connected: session=%s overlay_host=%d",
                 info.session_id, info.overlay_host);
    } else {
        log_line("failed to start the event thread");
    }
}

void nextui_coop_shutdown(void)
{
    atomic_store(&s_stop, 1);
    atomic_store(&s_connected, 0);
    if (s_thread_started) {
        pthread_join(s_thread, NULL);
        s_thread_started = 0;
    }
    if (s_app) {
        overlay_close_if_open();
        ap_host_detach();
        hud_release();
        overlay_destroy();
        nextui_app_destroy(s_app);
        s_app = NULL;
    } else {
        ap_host_detach();
        hud_release();
        overlay_destroy();
    }
}

void nextui_coop_overlay_suspend(void)
{
    overlay_close_if_open();
    hud_release();
    ap_host_detach();

    if (s_app && nextui_app_is_connected(s_app)) {
        (void)nextui_app_overlay_set_open(s_app, 0);
        NextuiAppStatus st = nextui_app_overlay_set_host(s_app, 0);
        if (st != NEXTUI_APP_OK)
            log_line("overlay.host(false) failed: %s", nextui_app_status_string(st));
    }
    s_hosting = 0;

    /* The renderer and GFX resources go away with ap_quit(). */
    overlay_destroy();
    log_line("overlay suspended for streaming");
}

void nextui_coop_overlay_resume(void)
{
    if (!s_app) return;

    if (overlay_create())
        ap_host_attach(&s_backend, on_host_event, NULL);

    if (nextui_app_is_connected(s_app)) {
        NextuiAppStatus st = nextui_app_overlay_set_host(s_app, 1);
        if (st != NEXTUI_APP_OK)
            log_line("overlay.host(true) failed: %s", nextui_app_status_string(st));
        nextui_app_overlay_set_open(s_app, 0);
        status_refresh();
    }
    s_hosting = 1;
    log_line("overlay resumed");
}

void nextui_coop_stream_begin(void)
{
    atomic_store(&s_streaming, 1);

    s_last_inhibit = now_ms();
    if (s_app && nextui_app_is_connected(s_app)) {
        /* Automatic pre-inhibit: suppresses idle sleep for five minutes and is
         * refreshed by nextui_coop_tick(); an explicit power press still sends
         * sleep.prepare and is deferred/acked by on_sleep(). */
        NextuiAppStatus st = nextui_app_sleep_inhibit(s_app, "streaming", STREAM_INHIBIT_MS);
        if (st != NEXTUI_APP_OK)
            log_line("sleep.inhibit failed: %s", nextui_app_status_string(st));
    }
    log_line("stream begin");
}

void nextui_coop_stream_end(void)
{
    atomic_store(&s_streaming, 0);
    if (s_app && nextui_app_is_connected(s_app))
        (void)nextui_app_sleep_release(s_app);
    log_line("stream end");
}

void nextui_coop_tick(void)
{
    if (!atomic_load(&s_streaming)) return;
    if (!s_app || !nextui_app_is_connected(s_app)) return;

    unsigned long long now = now_ms();
    if (now - s_last_inhibit >= STREAM_INHIBIT_REFRESH_MS) {
        (void)nextui_app_sleep_inhibit(s_app, "streaming", STREAM_INHIBIT_MS);
        s_last_inhibit = now;
    }
}

bool nextui_coop_streaming(void)
{
    return atomic_load(&s_streaming) != 0;
}

nextui_overlay_owner nextui_coop_overlay_owner(void)
{
    if (atomic_load(&s_streaming)) return NEXTUI_OVERLAY_SYSTEM;
    if (!s_overlay || !s_hosting) return NEXTUI_OVERLAY_SYSTEM;
    return NEXTUI_OVERLAY_APP;
}

bool nextui_coop_quit_requested(void)
{
    return atomic_load(&s_quit) != 0;
}

const char* nextui_coop_state_string(void)
{
    static char buf[96];
    snprintf(buf, sizeof(buf), "connected=%d streaming=%d hosting=%d overlay=%d",
             atomic_load(&s_connected), atomic_load(&s_streaming), s_hosting,
             s_overlay != NULL);
    return buf;
}
