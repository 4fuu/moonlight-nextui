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
# Hardware decode (Cedar VPU) for the stream engine. The engine falls back to
# its software decoder on any firmware without the vendor libraries, so this
# is safe by default; `export MOONLIGHT_HWDEC=0` in env.sh turns it off.
MOONLIGHT_HWDEC=${MOONLIGHT_HWDEC:-1}
export PATH LD_LIBRARY_PATH SDL_AUDIODRIVER MOONLIGHT_KEY_DIR MOONLIGHT_HWDEC

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

# Engine development output (decode statistics, latency lines) is off unless
# explicitly requested with `export MOONLIGHT_DEBUG=1` in env.sh.
MOONLIGHT_DEBUG=${MOONLIGHT_DEBUG:-0}
export MOONLIGHT_DEBUG

# Optional user overrides, sourced last so they win over the defaults above
# (e.g. `export SDL_RENDER_VSYNC=0` for on-device A/B tests). The shim execs
# the engine from this environment, so the variables reach it.
USER_ENV="$SHARED_USERDATA_PATH/moonlight/env.sh"
if [ -f "$USER_ENV" ] && [ -r "$USER_ENV" ]; then
    # shellcheck source=/dev/null
    . "$USER_ENV"
    echo "env=$USER_ENV sourced"
fi

echo "path=$PATH"
echo "keydir=$MOONLIGHT_KEY_DIR ca=$CA_BUNDLE"

# ── WiFi power save ─────────────────────────────────────────────────────────
#
# With power save on, wlan0 latency averages ~23ms; off it drops to ~5-9ms,
# which is the difference between a smooth stream and a stuttering one. Turn it
# off for the run and put the previous state back afterwards. Every step is
# best effort: a missing iw/wlan0 or a failed call just leaves the radio alone.

WLAN_IFACE=wlan0
POWER_SAVE_CHANGED=0

wifi_power_save_restore() {
    [ "$POWER_SAVE_CHANGED" = "1" ] || return 0
    POWER_SAVE_CHANGED=0
    if iw dev "$WLAN_IFACE" set power_save "$POWER_SAVE_PREVIOUS" 2>/dev/null; then
        echo "wifi power_save restored to $POWER_SAVE_PREVIOUS on $WLAN_IFACE"
    else
        echo "wifi power_save restore to $POWER_SAVE_PREVIOUS failed on $WLAN_IFACE"
    fi
    return 0
}

# Signals must not end the script before the session helper's own EXIT
# cleanup has run, so record the signal and let the script finish normally:
# the restore below runs inline at the end of the script body, before the
# helper's EXIT trap. (A signal that arrives while the app runs is handled once
# the app returns, so the launcher never exits out from under the app.)
LAUNCH_SIGNAL=""
for LAUNCH_SIGNAL_NAME in HUP INT TERM; do
    trap 'LAUNCH_SIGNAL='"$LAUNCH_SIGNAL_NAME" "$LAUNCH_SIGNAL_NAME"
done

if command -v iw >/dev/null 2>&1 && iw dev "$WLAN_IFACE" info >/dev/null 2>&1; then
    POWER_SAVE_CURRENT=$(iw dev "$WLAN_IFACE" get power_save 2>/dev/null)
    POWER_SAVE_PREVIOUS=${POWER_SAVE_CURRENT##* }
    case "$POWER_SAVE_PREVIOUS" in
        on)
            if iw dev "$WLAN_IFACE" set power_save off 2>/dev/null; then
                POWER_SAVE_CHANGED=1
                echo "wifi power_save off on $WLAN_IFACE (was on)"
            else
                echo "wifi power_save off failed on $WLAN_IFACE"
            fi
            ;;
        off)
            echo "wifi power_save already off on $WLAN_IFACE"
            ;;
        *)
            echo "wifi power_save state unknown on $WLAN_IFACE: ${POWER_SAVE_CURRENT:-no output}"
            ;;
    esac
fi

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

# Restore before the exit below, so the helper's EXIT trap still runs after it
# and the cleanup never depends on trap order.
wifi_power_save_restore
if [ -n "$LAUNCH_SIGNAL" ]; then
    echo "interrupted by SIG$LAUNCH_SIGNAL"
    case "$LAUNCH_SIGNAL" in
        HUP) APP_RC=129 ;;
        INT) APP_RC=130 ;;
        TERM) APP_RC=143 ;;
    esac
fi

echo "=== Moonlight.pak exit rc=$APP_RC $(date) ==="
exit "$APP_RC"
