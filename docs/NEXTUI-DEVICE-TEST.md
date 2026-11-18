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

Expected: `"handled_by_app":true` and `"overlay_host":true` (the pak declares
`overlay` and hosts in the menu). Pressing HOME opens the in-process panel; the
system panel must not appear. The app log should show
`nextui: connected: session=... overlay_host=1`.

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

## 6. Sleep behavior (see section 9 for the handshake)

Start a stream and press the power key once. The SDK must defer the attempt for
five minutes and the stream must keep running; section 9 covers the full
handshake and the post-stream release.

## 7. Full-fidelity HOME panel

Home from any menu screen (main, servers, settings, pairing):

- The panel appears over the current screen with the NextUI dim wash. Compare
  it against the system panel from an unadapted app: same position, radius,
  title "System" + "Moonlight", gear icon, row spacing, footer pills and the
  top-right battery/Wi-Fi/Bluetooth pill.
- Rows: Resume (closes), Save/Resume capabilities are **not** offered by
  Moonlight; Wi-Fi and Bluetooth show `On`/`Off` from the SDK status; Exit to
  desktop closes the app; Close returns to the app.
- D-pad up/down moves, A activates, B closes. While the panel is open the app
  behind it must not move.
- Wi-Fi/Bluetooth toggles currently answer unavailable (no C1 backend); a
  failed toggle must not change the row.
- Exit to desktop returns to NextUI; the log ends with
  `=== Moonlight.pak exit rc=0`.

Theme check: change the NextUI theme or font, relaunch, and confirm the panel
follows it (the renderer reads `/mnt/SDCARD/.system/res` and
`minuisettings.txt`, not a packaged copy).

```sh
adb shell 'ls -l /mnt/SDCARD/.system/res/assets@3x.png /mnt/SDCARD/.system/res/font1.ttf /mnt/SDCARD/.system/res/font2.ttf'
adb shell 'grep -E "color[1-7]=|font=" /mnt/SDCARD/.userdata/shared/minuisettings.txt'
```

## 8. HUD cooperation

1. In the menu, press volume up/down and brightness keys.
2. Expected: exactly one HUD, drawn by the app (NextUI's volume/brightness
   pill at the top right), auto-hiding after ~1.6 s. keymon must not draw a
   second one.
3. Mute keys are left to the system HUD; confirm one system HUD and no app one.
4. During a stream, the same keys must show the **system** HUD. The app yields
   `overlay.host:false`, so no `feedback` events are sent.
5. If the app's HUD does not appear, check the log for `hud.policy` failures
   and confirm the session is adapted.

## 9. Sleep / wake handshake

1. **Menu, idle:** let the device auto-sleep. It should suspend normally (the
   app acks `sleep.prepare` with nothing to save).
2. **Streaming, idle:** wait past the idle threshold. The stream must not
   suspend: the wrapper holds and refreshes `sleep.inhibit("streaming", 300000)`.
3. **Streaming, explicit power press:** the system sends `sleep.prepare`; the
   app inhibits that attempt for five minutes and the stream continues. The
   log shows the inhibit; `nextdev session-status`/`sleep.status` shows the
   lease.
4. **After the stream ends:** the lease is released (`sleep.release`), so idle
   sleep works again.
5. **Wake:** after a suspend in the menu, `wake.done` refreshes the status
   snapshot; the panel and battery row stay correct.

```sh
adb shell 'grep -E "sleep|stream (begin|end)" /mnt/SDCARD/.userdata/tg5040/logs/Moonlight.txt'
adb shell '/mnt/SDCARD/.system/tg5040/bin/nextdev.elf sleep-status'
```

## 10. Battery and status rows

- Battery: compare the pill against `/sys/class/power_supply/axp2202-battery/capacity`
  and the charging state; plug/unplug USB and confirm the icon updates within
  a few seconds (SDK pushes battery every 5 s).
- Wi-Fi: toggle Wi-Fi from NextUI settings while the panel is open; the row and
  the status pill change without restarting Moonlight.
- Bluetooth: connect/disconnect a pad or headset; the pill's Bluetooth icon
  follows.

## 11. Dynamic ownership during a stream

1. Start a stream.
2. From a second shell: `nextdev app-action overlay`. Expected:
   `"handled_by_system":true` and `"overlay_host":false`.
3. Press HOME on the device: NextUI's system panel appears over the stream.
4. Its Exit action must end the stream and return to NextUI; Resume closes the
   panel and leaves the stream running.
5. After returning to the Moonlight menu, `app-action overlay` must be
   `handled_by_app` again.

```sh
adb shell '/mnt/SDCARD/.system/tg5040/bin/nextdev.elf session-status | grep overlay_host'
```

## 12. Evidence to attach

- `Moonlight.txt` from the failing run.
- Output of the `session-status` and `app-action overlay` commands.
- A short recording of: launch → HOME panel → HUD → stream → dynamic HOME
  fallback → exit → menu.
- For input failures, the exact pad model and which buttons are wrong.
