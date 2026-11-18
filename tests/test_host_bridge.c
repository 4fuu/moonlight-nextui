/*
 * Apostrophe widget host bridge test.
 *
 * The widget loops call ap_poll_input()/ap_present() themselves. The fork
 * routes those through the attached host backend so a widget-based app can
 * host the NextUI panel without a custom main loop. This test checks the three
 * behaviours the cooperation layer depends on:
 *
 *   - ap_present() draws the host overlay before presenting;
 *   - the backend is polled once per frame, not once per ap_poll_input() call;
 *   - a latched quit request produces one synthetic B press so the widget
 *     cancels back to its caller.
 */

#include "apostrophe.h"
#include "apostrophe_host.h"

#include <stdio.h>
#include <string.h>

static int failures;
static int draws;
static int polls;

#define CHECK(cond, what)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok: %s\n", what);                                        \
        } else {                                                               \
            printf("  FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);           \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int backend_poll(void* user)
{
    (void)user;
    polls++;
    return 0;
}

static void backend_draw(void* user, SDL_Renderer* renderer)
{
    (void)user;
    (void)renderer;
    draws++;
}

static void on_event(void* user, ap_host_event event)
{
    (void)user;
    (void)event;
}

int main(void)
{
    ap_config config;
    memset(&config, 0, sizeof(config));
    config.window_title = "host-bridge-test";
    config.font_path = "third_party/apostrophe/res/font.ttf";
    config.log_path = "/tmp/moonlight-host-bridge-test.log";
    config.is_nextui = false;
    config.cpu_speed = AP_CPU_SPEED_MENU;

    if (ap_init(&config) != AP_OK) {
        fprintf(stderr, "ap_init failed: %s\n", ap_get_error());
        return 1;
    }

    ap_host_backend backend = {
        .user = NULL,
        .poll = backend_poll,
        .draw = backend_draw,
    };
    CHECK(ap_host_attach(&backend, on_event, NULL) == AP_OK, "host backend attaches");

    ap_input_event event;
    memset(&event, 0, sizeof(event));

    ap_present();
    CHECK(draws == 1, "ap_present draws the host overlay");

    ap_poll_input(&event);
    CHECK(polls == 1, "first input poll runs the backend once");
    ap_poll_input(&event);
    CHECK(polls == 1, "the backend is not polled twice in one frame");

    ap_present();
    ap_poll_input(&event);
    CHECK(polls == 2, "a new frame polls again");

    CHECK(ap_host_request_quit() == 1, "quit request is accepted");
    int got_b = ap_poll_input(&event);
    CHECK(got_b && event.button == AP_BTN_B && event.pressed,
          "quit injects one synthetic B press");
    CHECK(!ap_poll_input(&event), "the synthetic press is not repeated");

    ap_host_detach();
    ap_quit();

    printf("%s\n", failures == 0 ? "host bridge tests passed" : "host bridge tests FAILED");
    return failures == 0 ? 0 : 1;
}
