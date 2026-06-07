# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Clawd Mochi is an Arduino firmware sketch for an **ESP32-C3 Super Mini** driving an **ST7789 1.54" 240×240 SPI TFT**, optionally with a passive buzzer and TTP223 capacitive touch module.

Two modes:
- **Standalone controller**: device runs a WiFi AP, phone connects, manual control over animations + canvas + terminal. This is how a fresh device behaves until configured.
- **Claude Code mirror**: device connects to home WiFi (STA), a Mac-side Python bridge (in [bridge/](bridge/)) translates Claude Code hooks into HTTP POSTs to the device, which renders **session state** (thinking / working / permission / done / error) and beeps. Touch input lets the user acknowledge permission requests by patting the case.

All firmware lives in one `.ino` file; bridge is a separate tiny Python script.

## Build / upload

There is no CLI build system — this is an Arduino IDE 2.x project.

- **Board:** `ESP32C3 Dev Module`
- **USB CDC On Boot:** **Enabled** (required for serial output; without it `Serial` is a no-op and the board may look unresponsive)
- **CPU Frequency:** 160 MHz
- **Upload Speed:** 921600
- **Required libraries (Library Manager):** `Adafruit GFX Library`, `Adafruit ST7735 and ST7789 Library`. ESP32 core (`esp32` by Espressif) must be installed via Boards Manager using the URL in [README.md](README.md).
- **Sketch entry:** [clawd_mochi.ino](clawd_mochi.ino) — open this file in Arduino IDE, select the port, click Upload.

There are no tests, no linter, no formatter. Compile-checking happens via Arduino IDE's "Verify" or `arduino-cli compile -b esp32:esp32:esp32c3` if you have arduino-cli installed.

## Hardware constraints (do not break these)

- **VCC is 3.3V only.** Never wire to 5V — the ST7789 logic and the ESP32-C3 are both 3.3V.
- **SPI uses GPIO 8 (SCK) and GPIO 10 (MOSI).** GPIO 6/7 are routed to the C3 Super Mini's onboard SPI flash on most board variants — never use them externally.
- **Pin map** at the top of [clawd_mochi.ino](clawd_mochi.ino):
  - TFT: `TFT_CS=4, TFT_DC=1, TFT_RST=2, TFT_BLK=3` + SPI `SCK=8, MOSI=10`
  - **`BUZZER_PIN=5`** (passive piezo, driven via `tone()` / LEDC — non-blocking)
  - **`TOUCH_PIN=0`** (TTP223 OUT, active-HIGH default)
  - **`BOOT_PIN=9`** (on-board BOOT button, software fallback — case-sealed so end-users can't reach it, kept for bring-up)
  - **`I2C_SDA=20, I2C_SCL=21`** (MPU6050; **note** these are normally UART pins but USB CDC On Boot moves `Serial` onto USB, freeing them — losing them does NOT lose Serial debug)
- **The 3D-printed case is fixed**: the user prints from the upstream STL once, no drilling. That's why touch input goes through capacitive sensing (TTP223 + foil patch) and motion input goes through MPU6050 (sealed inside, no external visibility needed). Sensors that need to "see" through the case (APDS, PIR, LDR, AHT20) won't work without case modification.
- **MPU6050 is optional at runtime** — `mpuPresent` flag from `mpu.begin()` gates the gesture tick. Build still works without the chip.

## Architecture

The firmware is a single sketch, [clawd_mochi.ino](clawd_mochi.ino), laid out top-to-bottom:

1. **Pins, display object, WiFi/server, NVS `Preferences`, display geometry, colours** — globals at the top, plus the `Config`-related globals (`staSsid`, `staPass`, `apMode`, `staIpStr`).
2. **Runtime state**:
   - **Low-level view**: `currentView` (one of `VIEW_EYES_NORMAL / SQUISH / CODE / DRAW / WORKING / PERMISSION / ERROR`), `termMode`, `busy`, `animSpeed`, twin background colours `animBgColor` / `drawBgColor` (kept in sync by routes), `buzzerMuted`.
   - **High-level session state** (the Claude Code mirror layer): `sessionState` (`SS_IDLE / THINKING / WORKING / PERMISSION / DONE / ERROR`), `sessionMeta`, `autoMode`. `sessionState` drives `currentView` via `setSessionState()` — it is the **upper layer**; `currentView` is the lower-level renderer.
3. **Logo geometry** — `LOGO_TRIS[]` / `LOGO_SEGS[]` `PROGMEM` tables read with `pgm_read_word`. Drive `drawLogoFilled()` and `animLogoReveal()`.
4. **Buzzer + NVS + Touch + BOOT + MPU helpers** — `beep(BeepEvent)`, `cfgLoad/Save/Clear`, `tickTouch()` (short/long/double tap classification), `tickBoot()` (GPIO 9 debounce), `tickMpu()` (20Hz polled tap/shake gesture detection). All touch / boot / gesture events funnel into the same `handleTouchShort/Long/Double` handlers so downstream behaviour is unified — the `touchLastEvent` + `touchLastEventMs` pair is what the bridge polls to close the permission loop.
5. **Drawing primitives** — original `drawNormalEyes`, `drawSquishEyes`, `drawCodeView`; new `drawWorkingView(tool)`, `drawPermissionView(desc)`, `drawErrorView(msg)` for session states. Terminal renderer (`term*` family) is for the manual Claude Code view, ASCII only.
6. **Animations** — original `animNormalEyes / animSquishEyes / animLogoReveal` are blocking. `tickSessionAnim()` is a **non-blocking** millis-based tick that runs from `loop()` — it pulses thinking eyes, wiggles idle eyes, flashes the permission frame, and auto-transitions `SS_DONE → SS_IDLE` after 3s. **Blocking animations must keep calling `server.handleClient()` inside their loops.**
7. **`INDEX_HTML`** — main mobile controller as a `PROGMEM` raw string. Polls `/state` to sync UI.
8. **`SETUP_HTML`** — minimal first-time provisioning page (SSID + password form, POST `/provision`). Served at `/` while `apMode == true`.
9. **Web routes** — registered in `setup()`:
   - **Standalone controller (unchanged behaviour)**: `/`, `/cmd`, `/char`, `/speed`, `/redraw`, `/canvas`, `/draw/clear`, `/draw/stroke`, `/backlight`, `/buzzer`, `/state`
   - **First-time setup**: `/setup` (form), `/provision` (POST → NVS write → restart), `/factoryreset` (POST → clear NVS → restart)
   - **Claude Code mirror**: `/event?type=<prompt|tool_pre|tool_post|permission|stop|error|idle>&meta=<urlencoded>` (the bridge endpoint), `/auto?on=<0|1>` (toggle whether `/event` drives the display)
10. **`setup()`** — initialises SPI/buzzer/touch, boot splash, logo reveal, then **STA-first / AP-fallback** WiFi flow. On STA success, shows IP + plays done chime. On failure, falls back to AP and shows setup info screen.
11. **`loop()`** — `server.handleClient()` + `tickTouch()` + `tickBoot()` + `tickMpu()` + `tickSessionAnim()`. No blocking.

### Cross-cutting things to be aware of

- **Single-file constraint:** the README explicitly asks contributors to keep everything in `clawd_mochi.ino`.
- **Session state vs view state:** when adding session-driven behaviour, write through `setSessionState()` — don't poke `currentView` directly. The manual web buttons (`/cmd`) still poke `currentView`; `/event` overrides via `setSessionState()` unless `autoMode == false`.
- **`busy` flag:** set during blocking animations; the web UI disables buttons while it's true. New long-running ops should respect it.
- **`tickSessionAnim()` is the only repeating animation engine** for session states — don't add `delay()` loops outside the existing blocking animations.
- **Two background colours** (`animBgColor`, `drawBgColor`) must stay in sync — most routes mirror.
- **Colour helpers:** `hexToRgb565` / `rgb565ToHex` are the only conversion between web `#RRGGBB` and the display's 16-bit format.
- **Terminal mode is modal:** while `termMode == true`, `/cmd` only honours `q` (quit). Other view changes need to clear `termMode` first.
- **WiFi mode is decided at boot** based on saved NVS — switching modes at runtime requires `ESP.restart()`. `/factoryreset` does this for you.

## Mac-side bridge ([bridge/](bridge/))

Tiny Python script registered as Claude Code hooks. **No daemon, no dependencies beyond Python 3.** Every hook fires the script, which POSTs one event to the ESP32 over local HTTP and exits.

- [bridge/claude_hook.py](bridge/claude_hook.py) — reads hook JSON from stdin, forwards to `/event`. **For `PreToolUse` on the dangerous-tool whitelist** (`Bash` / `Edit` / `Write` / `MultiEdit` / `NotebookEdit`) it blocks while polling `/state` for a new `touchTs`, then emits a `permissionDecision` JSON to stdout — Claude Code reads that and skips the terminal y/n prompt. Other tools and non-blocking events stay fire-and-forget with a 1.5s timeout.
- [bridge/install.sh](bridge/install.sh) — asks for the device IP, writes `bridge/config.json` (preserving any tuned settings on re-install), merges hook entries into `~/.claude/settings.json` without clobbering existing hooks (idempotent).
- [bridge/uninstall.sh](bridge/uninstall.sh) — removes Mochi hooks, leaves others intact.
- `bridge/config.json` — created at install time, gitignored. Fields: `esp32_ip`, `permission_loop_enabled`, `permission_timeout_s`.
- `~/.cache/clawd-mochi/state.json` — runtime state for multi-session arbitration. Stores `primary_session` (uuid) — every `UserPromptSubmit` claims it; all other events check it and silently skip if non-primary. Atomic write via `os.replace`, no fcntl needed.

### Bridge / firmware contract

- `/event?type=...&meta=...` vocabulary is shared between [bridge/claude_hook.py](bridge/claude_hook.py)'s `post_event()` calls and [clawd_mochi.ino](clawd_mochi.ino)'s `routeEvent()`. Adding a new event type requires changes on both sides.
- `/state` returns `touchTs` (millis stamp) and `touch` (last event name). The bridge does NOT mutate these — it just polls. Mochi clears no state on read.
- The `PreToolUse` decision JSON is the only stdout the hook writes; everything else goes to stderr (which Claude Code ignores).

### Cross-cutting things to be aware of

- **Single-file constraint:** the README explicitly asks contributors to keep everything in `clawd_mochi.ino` so beginners can flash it without juggling files. Don't split into `.h`/`.cpp` unless asked.
- **`busy` flag:** set during animations; the web UI reads it via `/state` to disable buttons. Honour it in new long-running operations.
- **Two background colours:** `animBgColor` and `drawBgColor` are kept in sync by most routes. If you add a route that changes one, mirror it to the other unless you have a reason not to.
- **Colour helpers:** `hexToRgb565` / `rgb565ToHex` are the only conversion path between web `#RRGGBB` strings and the display's 16-bit format.
- **Terminal mode is modal:** while `termMode` is true, `/cmd` is mostly inert (only `q` exits). New view changes that should work from inside the terminal need to clear `termMode` first.

## Assets (not code)

- [models/](models/) — STL files for the 3D-printed case and standalone 3D Clawd figures. CC BY-NC-SA 4.0.
- [pics/](pics/) — README images and marketing shots.
