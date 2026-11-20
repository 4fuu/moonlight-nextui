/*
 * Test double for the bundled moonlight engine.
 *
 * The wrapper stops a stream with SIGTERM to the engine's process group. A
 * shell script cannot stand in for that check: /bin/sh clears an inherited
 * blocked mask, so "the engine never saw SIGTERM because the fork-time mask
 * leaked into it" would stay invisible. This binary keeps the mask it was
 * exec'd with, reports it, and exits on SIGTERM like the engine's signal loop.
 *
 * Installed on PATH as "moonlight" by test_stream_spawn.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t got_term;

static void on_term(int sig)
{
    (void)sig;
    got_term = 1;
}

int main(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_term;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);

    sigset_t mask;
    sigemptyset(&mask);
    sigprocmask(SIG_BLOCK, NULL, &mask);

    /* Report state before the pid, so the test can read both without racing a
     * signal that arrives right after the pid exists. */
    const char *mask_path = getenv("MOONLIGHT_TEST_ENGINE_MASK");
    if (mask_path) {
        FILE *f = fopen(mask_path, "w");
        if (f) {
            fprintf(f, "term_blocked=%d\n", sigismember(&mask, SIGTERM) ? 1 : 0);
            fclose(f);
        }
    }

    const char *pid_path = getenv("MOONLIGHT_TEST_ENGINE_PID");
    if (pid_path) {
        FILE *f = fopen(pid_path, "w");
        if (f) {
            fprintf(f, "%ld\n", (long)getpid());
            fclose(f);
        }
    }

    for (int i = 0; i < 200 && !got_term; i++)
        usleep(50 * 1000);

    return got_term ? 0 : 3;
}
