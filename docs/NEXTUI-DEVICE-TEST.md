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
- `bin/libs` has 35 libraries and `./bin/moonlight --version` (run from the
  pak directory with `LD_LIBRARY_PATH=$PWD/bin/libs`) does not report a
  missing shared library. v0.3.2 failed here on `libpulsecommon-13.99.so`.

Commands:

```sh
adb shell 'tail -n 40 /mnt/SDCARD/.userdata/tg5040/logs/Moonlight.txt'
adb shell 'ls -l /mnt/SDCARD/.userdata/shared/moonlight /mnt/SDCARD/.userdata/shared/moonlight/keys'
adb shell 'ls /mnt/SDCARD/Tools/tg5040/Moonlight.pak/bin/libs | wc -l'
adb shell 'cd /mnt/SDCARD/Tools/tg5040/Moonlight.pak && LD_LIBRARY_PATH=$PWD/bin/libs ./bin/moonlight --version'
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

Expected in v0.3.4: `"handled_by_system":true` and `"overlay_host":false`.
Pressing HOME opens NextUI's system panel; the app log shows
`nextui: init overlay_mode=system-fallback` and `nextui: overlay.host(0) -> ok`.
The app process must not draw a panel.

With the experimental opt-in (`MOONLIGHT_INPROCESS_OVERLAY=1` in `launch.sh`),
the same query is `"handled_by_app":true` and HOME opens the app panel.

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
- With a stream running, HOME → **Exit to desktop** ends the stream and lands
  on the NextUI desktop within about a second. The log ends with
  `signal 15; stopping engine`, and the engine is not left running behind the
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

## 7. HOME panel (default: system fallback)

Home from any menu screen (main, servers, settings, pairing):

- NextUI's system panel appears over the current screen: Resume, Wi-Fi,
  Bluetooth, Exit to desktop, footer hints, top-right status pill.
- D-pad up/down moves, A activates, B or HOME closes. The app behind it must
  not move.
- Exit to desktop returns to NextUI; the log ends with
  `=== Moonlight.pak exit rc=0`.
- The app log must **not** contain `overlay opened` or `panel draw` (the app
  is not drawing).
- This is the regression test for the v0.3.0 freeze: HOME must never leave the
  device on a dead screen.

```sh
adb shell 'grep -E "overlay_mode|overlay.host|overlay.home" /mnt/SDCARD/.userdata/tg5040/logs/Moonlight.txt'
```

### 7b. Optional in-process panel (only if asked)

Add `MOONLIGHT_INPROCESS_OVERLAY=1` to the `launch.sh` environment, reinstall,
and repeat. Expected: the app-drawn panel (Resume, Wi-Fi, Bluetooth, Exit,
Close) over the menu, same visual as the system panel. The log must show the
sequence below; the first missing line is where it stalls:

```text
nextui: init overlay_mode=in-process
nextui: overlay renderer created
nextui: requesting overlay.host(1) (in-process)
nextui: overlay.host(1) -> ok
nextui: connected: session=... hosting=1 declared_host=1
nextui: overlay.home event
nextui: HOME consumed by main loop
nextui: panel open begin
nextui: overlay opened (state queued)
nextui: panel open returned open=1
nextui: panel draw #1 begin
nextui: panel draw #1 end
nextui: reporting overlay.state(1)
nextui: overlay.state(1) ok
```

Then press HOME again: the panel must close, with

```text
nextui: overlay.home event
nextui: HOME consumed by main loop
nextui: panel close begin
nextui: panel close returned open=0
nextui: reporting overlay.state(0)
nextui: overlay.state(0) ok
```

If it stalls, capture the full `Moonlight.txt`; a 2 s open watchdog will log
`panel did not open ... yielding hosting` and return HOME to the system. The
v0.3.4 UI thread no longer sends `overlay.state`, so the old
`UIOverlay_open -> focus_changed -> nextui_app_overlay_set_open` blocking path
is gone from this build; the SDK request lock fix is still required for the
opt-in path to be considered reliable.

## 8. HUD cooperation

1. In the menu, press volume up/down and brightness keys.
2. Default: keymon draws the system HUD (the app yields hosting, so
   `hud.policy` is effective-system). Confirm exactly one HUD.
3. With the in-process opt-in, the app draws NextUI's indicator for
   volume/brightness/colortemp; mute stays with keymon.
4. During a stream, the same keys must show the **system** HUD in both modes.
5. If the HUD is missing or doubled, capture `hud.policy` lines from the log
   and `nextdev session-status`.

## 9. Sleep / wake handshake

0. **Power ownership:** the log must contain
   `power: Apostrophe handler disabled; keymon owns KEY_POWER`. A short press
   must **not** print `Power: short press → suspend`, and a long press must not
   exit the app or create `/tmp/poweroff`; keymon drives the handshake. If the
   old Apostrophe lines appear, the pak is running an older build.
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

## 11. Dynamic ownership

Default (system fallback): `overlay_host:false` in the menu **and** during a
stream. `app-action overlay` is always `handled_by_system`.
In-process opt-in: menu is `handled_by_app`, streaming must flip to
`handled_by_system`.

1. Start a stream (in-process mode).
2. From a second shell: `nextdev app-action overlay`. Expected:
   `"handled_by_system":true` and `"overlay_host":false`.
3. Press HOME: NextUI's system panel appears over the stream.
4. Its Exit action must end the stream and return to NextUI; Resume closes the
   panel and leaves the stream running.
5. After returning to the Moonlight menu (in-process mode), `app-action
   overlay` must be `handled_by_app` again.

```sh
adb shell '/mnt/SDCARD/.system/tg5040/bin/nextdev.elf session-status | grep overlay_host'
```

## 12. Evidence to attach

- `Moonlight.txt` from the failing run.
- Output of the `session-status` and `app-action overlay` commands.
- A short recording of: launch → HOME panel → HUD → stream → dynamic HOME
  fallback → exit → menu.
- For input failures, the exact pad model and which buttons are wrong.
