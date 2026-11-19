/*
 * Cooperation transition test.
 *
 * Runs the real nextui_coop_init() against the SDK's fake devserv, then walks
 * the contract 7.9.3 transition and asserts the request order it produced:
 *
 *   overlay.host true (start)
 *   overlay.state false + overlay.host false (menu -> streaming)
 *   sleep.inhibit reason=streaming (stream begin)
 *   sleep.release (stream end)
 *   overlay.host true (streaming -> menu)
 *
 * A missing or reordered request is a dynamic-ownership bug: HOME could end up
 * routed to a process that cannot draw, or a stream could be suspended.
 */

#include "apostrophe.h"
#include "nextui_coop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(cond, what)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok: %s\n", what);                                        \
        } else {                                                               \
            printf("  FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);           \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static char* read_all(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* Finds `needle` after `from` and advances the cursor. */
static int expect_after(const char** cursor, const char* needle, const char* what)
{
    const char* at = *cursor ? strstr(*cursor, needle) : NULL;
    if (!at) {
        printf("  FAIL: %s (missing: %s)\n", what, needle);
        failures++;
        return 0;
    }
    *cursor = at + strlen(needle);
    printf("  ok: %s\n", what);
    return 1;
}

/* monotonic milliseconds for the load test */
static unsigned long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}

/* One control round trip to the fake devserv (test.* operations). */
static int control(const char* request)
{
    const char* path = getenv("NEXTUI_DEVSERV_SOCKET");
    struct sockaddr_un addr;
    char response[256];
    struct timeval timeout = { 3, 0 };
    int fd;
    ssize_t n;

    if (!path) return 0;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return 0;
    }
    if (write(fd, request, strlen(request)) < 0 || write(fd, "\n", 1) < 0) {
        close(fd);
        return 0;
    }
    n = read(fd, response, sizeof(response) - 1);
    close(fd);
    return n > 0;
}

/*
 * HOME under load: a burst of subscription events arrives just before HOME.
 * The panel must open promptly and stay open, and the burst must not ask the
 * app to quit. This is the app-level shape of the device freeze: the open path
 * is driven from the widget frame, not from a request in flight.
 *
 * The lock-level deadlock (a full subscribe socket while a request waits) is
 * covered by the SDK's own concurrency tests with a deadlock watchdog; this
 * test guards the wrapper's use of that client.
 */
static unsigned long long run_home_under_load(void)
{
    unsigned long long start;
    unsigned long long elapsed = 0;
    int frames;

    (void)control("{\"op\":\"test.flood\",\"events\":4000}");
    usleep(80 * 1000);

    start = now_ms();
    (void)control("{\"op\":\"test.overlay_home\"}");

    /* Drive the same frame loop a widget does: poll, then present. */
    while (now_ms() - start < 2500) {
        ap_input_event event;
        while (ap_poll_input(&event)) { }
        ap_request_frame();
        ap_present();
        if (nextui_coop_overlay_open()) {
            elapsed = now_ms() - start;
            break;
        }
        usleep(5 * 1000);
    }

    /* Events keep arriving while the panel is open; it must not close. */
    for (frames = 0; frames < 40; frames++) {
        ap_input_event event;
        while (ap_poll_input(&event)) { }
        ap_request_frame();
        ap_present();
        usleep(5 * 1000);
    }
    if (elapsed && !nextui_coop_overlay_open()) {
        printf("  FAIL: panel closed while events arrived\n");
        failures++;
    }
    if (nextui_coop_quit_requested()) {
        printf("  FAIL: event burst asked the app to quit\n");
        failures++;
    }

    /* The open panel owns the frame, so the bridge stops calling coop_poll().
     * A second HOME must still reach the panel and close it; a panel that
     * ignores input is the device "freeze" with the panel up. */
    if (elapsed) {
        unsigned long long closed_at = 0;
        (void)control("{\"op\":\"test.overlay_home\"}");
        start = now_ms();
        while (now_ms() - start < 1500) {
            ap_input_event event;
            while (ap_poll_input(&event)) { }
            ap_request_frame();
            ap_present();
            if (!nextui_coop_overlay_open()) {
                closed_at = now_ms() - start;
                break;
            }
            usleep(5 * 1000);
        }
        printf("  panel closed in %llu ms\n", closed_at);
        CHECK(closed_at > 0 && closed_at < 800,
              "HOME closes the open in-process panel");
    }

    return elapsed;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <fake-devserv-log> [inprocess|system]\n", argv[0]);
        return 2;
    }
    int inprocess = argc < 3 || strcmp(argv[2], "inprocess") == 0;

    ap_config config;
    memset(&config, 0, sizeof(config));
    config.window_title = "coop-test";
    config.font_path = "third_party/apostrophe/res/font.ttf";
    config.log_path = "/tmp/moonlight-coop-test.log";
    config.is_nextui = false;
    config.cpu_speed = AP_CPU_SPEED_MENU;

    if (ap_init(&config) != AP_OK) {
        fprintf(stderr, "ap_init failed: %s\n", ap_get_error());
        return 1;
    }

    nextui_coop_init();
    usleep(700 * 1000);

    CHECK(nextui_coop_overlay_owner() == (inprocess ? NEXTUI_OVERLAY_APP
                                                    : NEXTUI_OVERLAY_SYSTEM),
          inprocess ? "menu hosts the overlay in-process"
                    : "menu leaves HOME to the system");

    if (inprocess) {
        unsigned long long home_ms = run_home_under_load();
        printf("  home under load opened in %llu ms\n", home_ms);
        CHECK(home_ms > 0 && home_ms < 1200,
              "HOME opens the in-process panel during an event burst");
    } else {
        /* No renderer exists, so a HOME event must be ignored, not drawn. */
        (void)control("{\"op\":\"test.overlay_home\"}");
        for (int i = 0; i < 40; i++) {
            ap_input_event event;
            while (ap_poll_input(&event)) { }
            ap_request_frame();
            ap_present();
            usleep(5 * 1000);
        }
        CHECK(!nextui_coop_overlay_open(),
              "system fallback never opens an app panel");
    }

    nextui_coop_overlay_suspend();
    usleep(200 * 1000);
    CHECK(nextui_coop_overlay_owner() == NEXTUI_OVERLAY_SYSTEM,
          "streaming transition yields overlay ownership");

    nextui_coop_stream_begin();
    usleep(300 * 1000);
    CHECK(nextui_coop_streaming(), "stream is marked active");

    nextui_coop_stream_end();
    usleep(200 * 1000);

    nextui_coop_overlay_resume();
    usleep(300 * 1000);
    CHECK(nextui_coop_overlay_owner() == (inprocess ? NEXTUI_OVERLAY_APP
                                                    : NEXTUI_OVERLAY_SYSTEM),
          inprocess ? "menu restores overlay ownership"
                    : "menu stays on the system fallback");

    nextui_coop_shutdown();
    ap_quit();

    char* log = read_all(argv[1]);
    if (!log) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    printf("request order (%s)\n", inprocess ? "in-process" : "system");
    const char* cursor = log;
    if (inprocess) {
        expect_after(&cursor, "\"op\":\"overlay.host\"", "overlay.host requested at start");
        expect_after(&cursor, "\"host\":true", "start hosts");
        expect_after(&cursor, "\"op\":\"overlay.state\"", "overlay.state reported before yield");
        expect_after(&cursor, "\"open\":false", "panel reported closed");
        expect_after(&cursor, "\"op\":\"overlay.host\"", "overlay.host requested at suspend");
        expect_after(&cursor, "\"host\":false", "streaming yields hosting");
    } else {
        expect_after(&cursor, "\"op\":\"overlay.host\"", "overlay.host requested at start");
        expect_after(&cursor, "\"host\":false", "session starts on the system fallback");
    }
    expect_after(&cursor, "\"op\":\"sleep.inhibit\"", "stream requests sleep.inhibit");
    expect_after(&cursor, "\"reason\":\"streaming\"", "inhibit reason is streaming");
    expect_after(&cursor, "\"op\":\"sleep.release\"", "stream end releases the inhibit");
    if (inprocess) {
        expect_after(&cursor, "\"op\":\"overlay.host\"", "overlay.host requested at resume");
        expect_after(&cursor, "\"host\":true", "menu restores hosting");
    }

    printf("%s\n", failures == 0 ? "coop transition tests passed" : "coop transition tests FAILED");
    return failures == 0 ? 0 : 1;
}
