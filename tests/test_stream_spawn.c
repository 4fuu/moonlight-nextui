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

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

static void write_sleeper_engine(const char* pid_path)
{
    char body[1024];
    snprintf(body, sizeof(body),
             "echo $$ > '%s'\n"
             "trap 'exit 0' TERM\n"
             "sleep 30\n",
             pid_path);
    write_engine(body);
}

static int read_pid(const char* path, pid_t* out)
{
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    long value = 0;
    int ok = fscanf(f, "%ld", &value) == 1;
    fclose(f);
    if (!ok || value <= 0) return 0;
    *out = (pid_t)value;
    return 1;
}

static int wait_for_engine(const char* path, pid_t* out, int timeout_ms)
{
    for (int i = 0; i < timeout_ms / 20; i++) {
        if (read_pid(path, out)) return 1;
        usleep(20 * 1000);
    }
    return 0;
}

static int pid_gone(pid_t pid, int timeout_ms)
{
    for (int i = 0; i < timeout_ms / 50; i++) {
        if (kill(pid, 0) < 0 && errno == ESRCH) return 1;
        usleep(50 * 1000);
    }
    return 0;
}

static int wait_child(pid_t pid, int* status, int timeout_ms)
{
    for (int i = 0; i < timeout_ms / 50; i++) {
        pid_t r = waitpid(pid, status, WNOHANG);
        if (r == pid) return 1;
        if (r < 0 && errno != EINTR) return 0;
        usleep(50 * 1000);
    }
    return 0;
}

static void test_timeout(int sig)
{
    (void)sig;
    _exit(2);
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

static void test_config_format(void)
{
    printf("config format\n");
    char userdata[640];
    snprintf(userdata, sizeof(userdata), "%s/userdata", tmpdir);
    mkdir(userdata, 0755);
    setenv("SHARED_USERDATA_PATH", userdata, 1);

    ml_config cfg = base_config();
    CHECK(save_config(&cfg) == 0, "config saves");

    char path[640];
    snprintf(path, sizeof(path), "%s/moonlight/config.txt", userdata);
    char* text = read_file(path);
    CHECK(text && strstr(text, "width = 1024") != NULL,
          "new config uses key = value (engine-compatible)");

    FILE* f = fopen(path, "w");
    if (f) {
        fputs("width=640\nheight=480\n", f);
        fclose(f);
    }
    ml_config legacy = load_config();
    CHECK(legacy.width == 640 && legacy.height == 480,
          "legacy key=value config still loads");

    f = fopen(path, "w");
    if (f) {
        fputs("width = 1280\nheight = 720\n", f);
        fclose(f);
    }
    ml_config spaced = load_config();
    CHECK(spaced.width == 1280 && spaced.height == 720,
          "spaced key = value config loads");
}

static void test_atexit_terminates_engine(void)
{
    printf("atexit cleanup\n");

    char pid_path[640];
    snprintf(pid_path, sizeof(pid_path), "%s/atexit-engine.pid", tmpdir);
    unlink(pid_path);
    write_sleeper_engine(pid_path);

    pid_t wrapper = fork();
    if (wrapper == 0) {
        stream_job job = { .cfg = base_config(), .rc = -1 };
        pthread_t thread;
        if (pthread_create(&thread, NULL, run_stream, &job) != 0)
            _exit(3);
        pid_t engine = 0;
        (void)wait_for_engine(pid_path, &engine, 2000);
        usleep(300 * 1000);
        exit(0); /* atexit must stop the engine */
    }

    CHECK(wrapper > 0, "wrapper child started");
    int status = 0;
    if (!wait_child(wrapper, &status, 6000)) {
        kill(wrapper, SIGKILL);
        (void)waitpid(wrapper, NULL, 0);
        CHECK(0, "wrapper exits");
        return;
    }
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "wrapper exits normally with status 0");

    pid_t engine = 0;
    CHECK(read_pid(pid_path, &engine), "engine pid was recorded");
    if (engine > 0)
        CHECK(pid_gone(engine, 4000), "engine is gone after wrapper exit");
}

static void test_signal_terminates_engine(void)
{
    printf("signal cleanup\n");

    char pid_path[640];
    snprintf(pid_path, sizeof(pid_path), "%s/signal-engine.pid", tmpdir);
    unlink(pid_path);
    write_sleeper_engine(pid_path);

    pid_t wrapper = fork();
    if (wrapper == 0) {
        stream_job job = { .cfg = base_config(), .rc = -1 };
        pthread_t thread;
        if (pthread_create(&thread, NULL, run_stream, &job) != 0)
            _exit(3);
        pid_t engine = 0;
        (void)wait_for_engine(pid_path, &engine, 2000);
        for (;;)
            pause();
    }

    CHECK(wrapper > 0, "wrapper child started");
    pid_t engine = 0;
    CHECK(wait_for_engine(pid_path, &engine, 3000), "engine started");

    kill(wrapper, SIGTERM);
    int status = 0;
    if (!wait_child(wrapper, &status, 6000)) {
        kill(wrapper, SIGKILL);
        (void)waitpid(wrapper, NULL, 0);
        CHECK(0, "wrapper exits after SIGTERM");
        return;
    }
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM,
          "wrapper re-raises SIGTERM after cleanup");
    if (engine > 0)
        CHECK(pid_gone(engine, 4000), "engine is gone after SIGTERM");
}

static void test_quit_flag_terminates_engine(void)
{
    printf("quit flag cleanup\n");

    char pid_path[640];
    snprintf(pid_path, sizeof(pid_path), "%s/quit-engine.pid", tmpdir);
    unlink(pid_path);
    write_sleeper_engine(pid_path);

    stream_job job = { .cfg = base_config(), .rc = -1 };
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, run_stream, &job) == 0,
          "stream thread started");

    pid_t engine = 0;
    CHECK(wait_for_engine(pid_path, &engine, 3000), "engine started");

    /* This is the overlay Exit action's flag, not a host unwind. */
    nextui_coop_request_quit();
    signal(SIGALRM, test_timeout);
    alarm(10);
    pthread_join(thread, NULL);
    alarm(0);

    CHECK(job.rc == 0, "quit flag returns success");
    CHECK(nextui_coop_quit_requested(), "quit flag stays set");
    if (engine > 0)
        CHECK(pid_gone(engine, 4000), "engine is gone after quit flag");
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
    test_config_format();
    test_cooperation_state();
    test_atexit_terminates_engine();
    test_signal_terminates_engine();
    test_quit_flag_terminates_engine();

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
    if (system(cmd) != 0) { /* best effort */ }

    printf("%s\n", failures == 0 ? "ml_stream tests passed" : "ml_stream tests FAILED");
    return failures == 0 ? 0 : 1;
}
