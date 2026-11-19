#!/bin/sh
# Moonlight.pak launcher for NextUI (tg5040).
#
# Runs the Apostrophe wrapper inside a C3 device session so app-action and
# system-feedback requests have a session to route to, keeps the bundled
# engine's PATH shim first, and returns the app's exit status without
# exec-ing over this script (the session helper's EXIT cleanup must run).

# Deliberately no `set -e`/`set -u`:
#   - the app's own exit status must be captured and returned, so a non-zero
#     stream result cannot abort the script before the log line and cleanup;
#   - nextui-session.sh reads optional NextUI variables (NEXTUI_APP_PAK_DIR,
#     HOOK_EMU_PATH, ...) that are legitimately unset when the pak is launched
#     outside the desktop's hook path.

APP_BIN="moonlight-pak"
PAK_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)

# NextUI's launcher exports these; the fallbacks keep a manual shell run (and
# the desktop preview) working without pretending to be a device session.
PLATFORM=${PLATFORM:-tg5040}
SDCARD_PATH=${SDCARD_PATH:-/mnt/SDCARD}
SYSTEM_PATH=${SYSTEM_PATH:-$SDCARD_PATH/.system/$PLATFORM}
USERDATA_PATH=${USERDATA_PATH:-$SDCARD_PATH/.userdata/$PLATFORM}
SHARED_USERDATA_PATH=${SHARED_USERDATA_PATH:-$SDCARD_PATH/.userdata/shared}
LOGS_PATH=${LOGS_PATH:-$USERDATA_PATH/logs}
export PLATFORM SDCARD_PATH SYSTEM_PATH USERDATA_PATH SHARED_USERDATA_PATH LOGS_PATH

mkdir -p "$SHARED_USERDATA_PATH/moonlight" "$LOGS_PATH" 2>/dev/null || true

LOG_FILE="$LOGS_PATH/Moonlight.txt"
: >"$LOG_FILE" 2>/dev/null || LOG_FILE=/dev/null
exec >>"$LOG_FILE" 2>&1

echo "=== Moonlight.pak $(date) platform=$PLATFORM device=${DEVICE:-unknown} ==="

cd "$PAK_DIR" || exit 1

# ── Runtime environment ─────────────────────────────────────────────────────

# The shim must win over any stock moonlight binary.
PATH="$PAK_DIR/shim:$PAK_DIR/bin:$PATH"
LD_LIBRARY_PATH="$PAK_DIR/bin/libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# The vendored full-fidelity overlay links libsamplerate (as NextUI does); the
# system keeps it under .system/<platform>/lib. Keep it after the pak's libs so
# the bundled engine closure still wins.
for LIB_DIR in "$SYSTEM_PATH/lib" "$SYSTEM_PATH/../lib"; do
    if [ -d "$LIB_DIR" ]; then
        LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$LIB_DIR"
    fi
done
SDL_AUDIODRIVER=alsa
MOONLIGHT_KEY_DIR=${MOONLIGHT_KEY_DIR:-$SHARED_USERDATA_PATH/moonlight/keys}
export PATH LD_LIBRARY_PATH SDL_AUDIODRIVER MOONLIGHT_KEY_DIR

mkdir -p "$MOONLIGHT_KEY_DIR" 2>/dev/null || true

# HTTPS for pairing/app lists. Prefer the system bundle; the pak fallback is
# only used when the system one is absent (e.g. a preview run).
CA_BUNDLE="$SYSTEM_PATH/../res/ca-bundle.crt"
if [ ! -r "$CA_BUNDLE" ]; then
    CA_BUNDLE="$PAK_DIR/res/ca-bundle.crt"
fi
if [ -r "$CA_BUNDLE" ]; then
    CURL_CA_BUNDLE="$CA_BUNDLE"
    SSL_CERT_FILE="$CA_BUNDLE"
    export CURL_CA_BUNDLE SSL_CERT_FILE
fi

echo "path=$PATH"
echo "keydir=$MOONLIGHT_KEY_DIR ca=$CA_BUNDLE"

# ── C3 session ──────────────────────────────────────────────────────────────

SESSION_HELPER="$SYSTEM_PATH/../res/nextui-session.sh"
if [ -r "$SESSION_HELPER" ]; then
    # shellcheck source=/dev/null
    . "$SESSION_HELPER"
    nextui_devserv_start
    nextui_session_start
    nextui_session_run "\"$PAK_DIR/$APP_BIN\""
    APP_RC=$?
    nextui_session_stop
else
    # Older NextUI / desktop preview: no session backend, run in foreground.
    "$PAK_DIR/$APP_BIN"
    APP_RC=$?
fi

echo "=== Moonlight.pak exit rc=$APP_RC $(date) ==="
exit "$APP_RC"
