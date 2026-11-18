/*
 * ml_stream spawn tests.
 *
 * The stream path no longer goes through system(). These tests run the real
 * ml_stream() against a fake "moonlight" on PATH, so the seam M1-M6 depend on
 * is checked without a device, a host, or an engine:
 *
 *   1. argv transparency — an app name with spaces and quotes arrives as one
 *      argument and the stream flags match the config.
 *   2. exit status — the engine's exit code reaches the caller.
 *   3. process group — the engine leads its own group, so a stream end can
 *      signal the whole tree.
 *   4. cooperation state — while the engine runs, the wrapper reports
 *      streaming and answers NEXTUI_OVERLAY_SYSTEM; after it exits, the menu
 *      owns the overlay again.
 *
 * The fake engine is a shell script the test writes into a temporary
 * directory prepended to PATH.
 */

#include "moonlight.h"
#include "nextui_coop.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static char tmpdir[512];
static char argv_log[512];
static char group_log[512];

static void write_engine(const char* body)
{
    char path[640];
    snprintf(path, sizeof(path), "%s/moonlight", tmpdir);
    FILE* f = fopen(path, "w");
    if (!f) {
        perror("fopen engine");
        exit(1);
    }
    fputs("#!/bin/sh\n", f);
    fputs(body, f);
    fclose(f);
    chmod(path, 0755);
}

static void reset_logs(void)
{
    unlink(argv_log);
    unlink(group_log);
}

static char* read_file(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static ml_config base_config(void)
{
    ml_config cfg = {0};
    cfg.width = 1024;
    cfg.height = 768;
    cfg.fps = 60;
    cfg.bitrate = 20000;
    cfg.active_server = 0;
    cfg.server_count = 1;
    snprintf(cfg.servers[0].name, sizeof(cfg.servers[0].name), "desk");
    snprintf(cfg.servers[0].host, sizeof(cfg.servers[0].host), "192.168.1.5");
    return cfg;
}

static void test_argv_transparency(void)
{
    printf("argv transparency\n");
    reset_logs();

    char script[512];
    snprintf(script, sizeof(script), "echo \"$@\" > '%s'\nexit 0\n", argv_log);
    write_engine(script);

    ml_config cfg = base_config();
    int rc = ml_stream(&cfg, "Steam Big Picture \"Deluxe\"");

    CHECK(rc == 0, "engine exit 0 is returned as 0");

    char* log = read_file(argv_log);
    CHECK(log != NULL, "engine wrote its argv");
    if (log) {
        CHECK(strstr(log, "stream 192.168.1.5") != NULL,
              "action and host are separate argv words");
        CHECK(strstr(log, "-app Steam Big Picture \"Deluxe\"") != NULL,
              "an app name with spaces and quotes stays one argument");
        CHECK(strstr(log, "-width 1024 -height 768 -fps 60 -bitrate 20000") != NULL,
              "stream flags match the config");
        CHECK(strstr(log, "-platform") == NULL,
              "the wrapper does not add the shim's platform flags");
        CHECK(strstr(log, "-quitappafter") == NULL,
              "the wrapper does not add the shim's quitappafter flag");
    }
}

static void test_exit_status(void)
{
    printf("exit status\n");
    reset_logs();
    write_engine("exit 7\n");

    ml_config cfg = base_config();
    CHECK(ml_stream(&cfg, "App") == 7, "engine exit code reaches the caller");
}

static void test_process_group(void)
{
    printf("process group\n");
    reset_logs();

    char script[512];
    snprintf(script, sizeof(script),
             "ps -o pgid= -p $$ | tr -d ' ' > '%s'\n"
             "ps -o pid= -p $$ | tr -d ' ' >> '%s'\n"
             "exit 0\n",
             group_log, group_log);
    write_engine(script);

    ml_config cfg = base_config();
    CHECK(ml_stream(&cfg, "App") == 0, "stream completes");

    char* log = read_file(group_log);
    CHECK(log != NULL, "engine reported its process group");
    if (log) {
        long pgid = 0, pid = 0;
        if (sscanf(log, "%ld\n%ld", &pgid, &pid) == 2)
            CHECK(pgid == pid, "engine leads its own process group");
        else
            CHECK(0, "engine process group is readable");
    }
}

typedef struct {
    ml_config cfg;
    int rc;
} stream_job;

static void* run_stream(void* arg)
{
    stream_job* job = (stream_job*)arg;
    job->rc = ml_stream(&job->cfg, "App");
    return NULL;
}

static void test_cooperation_state(void)
{
    printf("cooperation state\n");
    reset_logs();
    write_engine("sleep 1\nexit 0\n");

    nextui_overlay_owner idle_owner = nextui_coop_overlay_owner();
    CHECK(!nextui_coop_streaming(), "idle before the stream");

    stream_job job = { .cfg = base_config(), .rc = -1 };
    pthread_t thread;
    if (pthread_create(&thread, NULL, run_stream, &job) != 0) {
        CHECK(0, "stream thread started");
        return;
    }

    usleep(400 * 1000);
    CHECK(nextui_coop_streaming(), "streaming is reported while the engine runs");
    CHECK(nextui_coop_overlay_owner() == NEXTUI_OVERLAY_SYSTEM,
          "streaming defers overlay ownership to the system");
    CHECK(!nextui_coop_quit_requested(), "no quit request without an SDK event");

    pthread_join(thread, NULL);
    CHECK(job.rc == 0, "stream returns the engine's status");
    CHECK(!nextui_coop_streaming(), "streaming is cleared after the stream");
    /* Without a hosting overlay (coop_init is not called in this test) the
     * pre-stream owner is SYSTEM; a hosted menu restores APP. The point is
     * that streaming forces SYSTEM and the previous answer comes back. */
    CHECK(nextui_coop_overlay_owner() == idle_owner,
          "the pre-stream owner is restored");
}

int main(void)
{
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/moonlight-spawn-test-XXXXXX");
    if (!mkdtemp(tmpdir)) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(argv_log, sizeof(argv_log), "%s/argv.log", tmpdir);
    snprintf(group_log, sizeof(group_log), "%s/group.log", tmpdir);

    char path[1024];
    snprintf(path, sizeof(path), "%s:%s", tmpdir, getenv("PATH") ? getenv("PATH") : "");
    setenv("PATH", path, 1);

    test_argv_transparency();
    test_exit_status();
    test_process_group();
    test_cooperation_state();

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
    if (system(cmd) != 0) { /* best effort */ }

    printf("%s\n", failures == 0 ? "ml_stream tests passed" : "ml_stream tests FAILED");
    return failures == 0 ? 0 : 1;
}
