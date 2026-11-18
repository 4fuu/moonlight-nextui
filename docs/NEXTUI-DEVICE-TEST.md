# Moonlight.pak device test (tg5040 / TrimUI Brick)

Run this on a real Brick with a Sunshine host on the same network. The host
side has no video path, so a pass here is the baseline acceptance for the
S1-S5 infrastructure. M1-M6 items are marked **later**; do not fail the build
for them yet.

Record evidence for every failure: `Moonlight.txt`, a screenshot/recording,
and the exact key sequence.

## 1. Install and launch

1. Install `dist/Moonlight.pakz` with NextUI's app installer (or unzip to
   `/Tools/tg5040/Moonlight.pak/`).
2. Confirm `/Tools/tg5040/Moonlight.pak/launch.sh` is executable.
3. Launch **Moonlight** from Tools.

Expected:

- The main menu appears at 1024x768 with the installed NextUI theme.
- `$LOGS_PATH/Moonlight.txt` is created and starts with
  `=== Moonlight.pak ... platform=tg5040`.
- The log prints `path=` containing `Moonlight.pak/shim` and `bin/libs`.
- `$SHARED_USERDATA_PATH/moonlight/` exists.

Commands:

```sh
adb shell 'tail -n 40 /mnt/SDCARD/.userdata/tg5040/logs/Moonlight.txt'
adb shell 'ls -l /mnt/SDCARD/.userdata/shared/moonlight /mnt/SDCARD/.userdata/shared/moonlight/keys'
```

## 2. C3 session

1. While Moonlight is open, run from a second shell:

```sh
adb shell '/mnt/SDCARD/.system/tg5040/bin/nextdev.elf session-status'
```

Expected: `"session":"active"` and a `"pak"` path ending in
`Moonlight.pak`. If the helper is missing (older NextUI), the app still runs
but app-action/system-feedback have no session to route to.

2. Ask the system for overlay ownership:

```sh
adb shell '/mnt/SDCARD/.system/tg5040/bin/nextdev.elf app-action overlay'
```

Expected today: `"handled_by_system":true` (the pak does **not** declare the
overlay capability). On HOME, NextUI's fallback panel must open and offer
Resume/Exit; the app must not draw its own panel yet.

## 3. Server setup and pairing

1. Servers → X → enter name and the PC's **IP address**.
2. Pair → enter the PIN in Sunshine.

Expected:

- The PIN is visible and pairing succeeds.
- Keys appear in `$SHARED_USERDATA_PATH/moonlight/keys` (not `/.cache`).
- No TLS/CA error in the log. If pairing fails, confirm
  `/mnt/SDCARD/.system/res/ca-bundle.crt` exists and retry.

```sh
adb shell 'ls -l /mnt/SDCARD/.userdata/shared/moonlight/keys'
adb shell 'grep -i -E "curl|ssl|cert|pair" /mnt/SDCARD/.userdata/tg5040/logs/Moonlight.txt'
```

## 4. App list and stream

1. Stream → wait for the app list.
2. Launch a game/app.

Expected:

- The stream starts at 1024x768 by default.
- During the stream, confirm the engine argv contains the shim flags:

```sh
adb shell 'for p in $(pidof moonlight); do tr "\0" " " < /proc/$p/cmdline; echo; done'
```

  Expect `-platform sdl`, `-keydir /mnt/SDCARD/.userdata/shared/moonlight/keys`,
  and `-quitappafter`, then `stream <host> -app ...`.
- Audio plays through ALSA (no PulseAudio/HDMI sink error in the log).
- Input: d-pad, A/B/X/Y, L/R, Start/Select and the sticks all reach the host.
  **This is the highest-risk baseline item.** If buttons are missing or
  swapped, capture `adb shell dumpsys`-equivalent input info and note whether
  SDL recognized the pad. A `gamecontrollerdb.txt` entry is the likely fix.
- The engine's normal exit combo returns to the Moonlight menu, not the
  desktop.
- After exit, no `moonlight` process remains:

```sh
adb shell 'pidof moonlight || echo no-orphan'
```

- The log contains `=== Moonlight.pak exit rc=0`.

## 5. Error and cancellation paths

1. Start pairing and press B; confirm the app stays responsive and no child
   process is left (`ps | grep moonlight`).
2. Start a stream, then exit from the engine; confirm the wrapper returns to
   the menu and can stream again immediately.
3. Kill the engine from a shell while the wrapper waits
   (`kill $(pidof moonlight)`); the wrapper must return to the menu with a
   non-zero stream result and no hanging process.

## 6. Sleep behavior (baseline)

1. Start a stream.
2. Press the power key once.

Expected **today**: NextUI's normal power path runs. There is no SDK
`sleep.inhibit` yet (M4), so the device may suspend mid-stream. Record whether
the stream survives resume; this is the baseline for M4, not a failure.

## 7. Full-fidelity overlay (build-only today)

The vendored NextUI renderer is not attached to the menu yet (M2). On the host
it is covered by `overlay-smoke` (1024x768 PNG). On device, only confirm the
theme/font files it will read exist:

```sh
adb shell 'ls -l /mnt/SDCARD/.system/res/assets@3x.png /mnt/SDCARD/.system/res/font1.ttf /mnt/SDCARD/.system/res/font2.ttf'
adb shell 'grep -E "color[1-7]=|font=" /mnt/SDCARD/.userdata/shared/minuisettings.txt'
```

## 8. Later: M1-M6 acceptance (do not run yet)

When phase A/B land and the cooperation layer is enabled, rerun this section:

- Menu HOME opens an in-process panel that is pixel-identical to NextUI's
  system panel (header, shared status row, Resume/Wi-Fi/Bluetooth/Exit rows,
  footer hints), and input is owned by the panel while open.
- Volume/brightness keys show the HUD once, with the same owner as the system
  contract says (app in menu, system in stream).
- During a stream the system fallback owns HOME/HUD (`handled_by_system`), or
  HOME ends the stream cleanly if phase A does not support dynamic ownership.
- Sleep during a stream is inhibited with a visible/deferred `sleep.prepare`;
  resume sends `wake.done` and the stream continues (or is paused and
  restored, per the final contract).
- Battery row matches `/sys/class/power_supply/battery/capacity` and updates
  on change.

## 9. Evidence to attach

- `Moonlight.txt` from the failing run.
- Output of the `session-status` and `app-action overlay` commands.
- A short recording of: launch → HOME fallback → stream → exit → menu.
- For input failures, the exact pad model and which buttons are wrong.
