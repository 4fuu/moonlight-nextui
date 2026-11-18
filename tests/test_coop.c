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

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <fake-devserv-log>\n", argv[0]);
        return 2;
    }

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

    CHECK(nextui_coop_overlay_owner() == NEXTUI_OVERLAY_APP,
          "menu hosts the overlay in-process");

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
    CHECK(nextui_coop_overlay_owner() == NEXTUI_OVERLAY_APP,
          "menu restores overlay ownership");

    nextui_coop_shutdown();
    ap_quit();

    char* log = read_all(argv[1]);
    if (!log) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    printf("request order\n");
    const char* cursor = log;
    expect_after(&cursor, "\"op\":\"overlay.host\"", "overlay.host requested at start");
    expect_after(&cursor, "\"host\":true", "start hosts");
    expect_after(&cursor, "\"op\":\"overlay.state\"", "overlay.state reported before yield");
    expect_after(&cursor, "\"open\":false", "panel reported closed");
    expect_after(&cursor, "\"op\":\"overlay.host\"", "overlay.host requested at suspend");
    expect_after(&cursor, "\"host\":false", "streaming yields hosting");
    expect_after(&cursor, "\"op\":\"sleep.inhibit\"", "stream requests sleep.inhibit");
    expect_after(&cursor, "\"reason\":\"streaming\"", "inhibit reason is streaming");
    expect_after(&cursor, "\"op\":\"sleep.release\"", "stream end releases the inhibit");
    expect_after(&cursor, "\"op\":\"overlay.host\"", "overlay.host requested at resume");
    expect_after(&cursor, "\"host\":true", "menu restores hosting");

    printf("%s\n", failures == 0 ? "coop transition tests passed" : "coop transition tests FAILED");
    return failures == 0 ? 0 : 1;
}
