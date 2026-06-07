<!-- LOGO -->
<p align="center">
  <img src="pics/clawd_mochi_banner.png" alt="Clawd Mochi Logo" width="700"/>
</p>

# Clawd Mochi 🦀🤖

A physical desk companion inspired by **Clawd** — the pixel-crab mascot of Claude Code by Anthropic. An ESP32-C3 drives a 1.54" color TFT display and hosts a mobile web controller — no app, no internet, no cloud required.

**Cost: ~$6–8 · Build time: ~1 hour · Skill level: Beginner**

📦 3D printable case on MakerWorld: [https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)

---

> ⚠️ This is an independent fan project. It is not affiliated with, sponsored by, or endorsed by Anthropic. "Claude" and "Clawd" are trademarks of Anthropic.

---

<p align="center">
  <img src="pics/clawd_mochi_3_4.jpeg" alt="Assembled Clawd Mochi on a desk" width="500"/>
  &nbsp;
  <img src="pics/clawd_mochi_claude_code.jpeg" alt="Claude Code view" width="500"/>
</p>

## What it does

Clawd Mochi sits on your desk and shows animated expressions on a small color display. You control it from any phone or browser by connecting to its built-in WiFi hotspot:

- **Normal eyes** — pixel-art square eyes with wiggle and blink animations
- **Squish eyes** — `> <` happy squint with open/close animation
- **Claude Code** — displays "Claude Code" with an interactive terminal
- **Canvas** — draw anything on the display from your phone in real time

---

## Parts list

| Part                | Spec                             | ~Price |
| ------------------- | -------------------------------- | ------ |
| ESP32-C3 Super Mini | microcontroller with WiFi        | ~$2.50 |
| ST7789 1.54" TFT    | 240×240 SPI color display        | ~$3.00 |
| Passive piezo buzzer| 12mm, PWM driven                 | ~$0.30 |
| TTP223 touch module | capacitive, single-pad           | ~$0.30 |
| MPU6050 accelerometer | I2C, ~20×15×3mm                | ~$0.80 |
| Aluminium foil tape | ~15×15mm patch inside case top   | ~$0.05 |
| 12 short wires      | 8–10 cm Dupont / jumper wires    | ~$0.70 |
| 2× M2×6mm screws    | to mount display bezel           | ~$0.10 |
| Double-sided tape   | to secure components inside case | ~$0.10 |
| USB-C cable         | for power                        | —      |
| 3D printed case     | PLA or PETG, ~30g                | ~$0.50 |

**Total: ~$9–10**

> The buzzer, TTP223 and MPU6050 are all optional. Without any of them you still get the standalone controller. Add them and Mochi gains: sound (buzzer), head-pat input (TTP223), and pickup/shake/desk-tap detection (MPU6050) — all of which the **Claude Code mirror** uses.

---

## Wiring

> ⚠️ Connect VCC to **3.3V only** — never 5V. Use GPIO 8 and 10 for SPI (hardware SPI, fast). Do not use GPIO 6/7 for SPI.

| Display pin | ESP32-C3 GPIO  | Wire color (suggested) |
| ----------- | -------------- | ---------------------- |
| VCC         | 3V3            | Red                    |
| GND         | GND            | Black                  |
| SDA         | GPIO 10 (MOSI) | Orange                 |
| SCL         | GPIO 8 (SCK)   | Green                  |
| RES         | GPIO 2         | Purple                 |
| DC          | GPIO 1         | Blue                   |
| CS          | GPIO 4         | White                  |
| BL          | GPIO 3         | Yellow                 |

### Optional add-ons — buzzer + touch + accelerometer

| Module        | Pin   | ESP32-C3 GPIO | Notes                                |
| ------------- | ----- | ------------- | ------------------------------------ |
| Passive buzzer| `+`   | GPIO 5        | `−` to GND                           |
| TTP223 touch  | VCC   | 3V3           |                                      |
|               | GND   | GND           |                                      |
|               | OUT   | GPIO 0        | active-HIGH (default jumpers)        |
| MPU6050       | VCC   | 3V3           |                                      |
|               | GND   | GND           |                                      |
|               | SDA   | GPIO 20       | shared I2C bus (any other I2C parts on the same 2 pins) |
|               | SCL   | GPIO 21       |                                      |

> GPIO 20/21 are normally the C3's UART pins. They're free here because the README requires **USB CDC On Boot = Enabled**, which moves `Serial` onto the USB peripheral. Serial debugging still works.

Glue the TTP223 module flat against the inside of the case top with double-sided tape, then stick a ~15×15mm aluminium foil patch on the inside surface above the module's sensing pad. Touch the outside of the case (pat Mochi on the head) to trigger.

Glue the MPU6050 flat anywhere inside the case — orientation doesn't matter, the firmware just looks for spikes and shakes. Pickup, shake, and tabletop taps all work after install.

---

## Software setup

### Step 1 — Install Arduino IDE

Download [Arduino IDE 2.x](https://www.arduino.cc/en/software) and install it.

### Step 2 — Add ESP32 board support

1. Open Arduino IDE → **File → Preferences**
2. In "Additional boards manager URLs" paste:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**, search `esp32`, install **"esp32 by Espressif Systems"**

### Step 3 — Install libraries

Go to **Tools → Library Manager** and install:

- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`
- `Adafruit MPU6050` (also pulls `Adafruit Unified Sensor` and `Adafruit BusIO` as dependencies — accept when prompted)

The MPU6050 library compiles into the firmware even if you don't install the chip — the firmware skips it gracefully when no module responds on I2C.

### Step 4 — Configure board settings

Go to **Tools** and set:

| Setting         | Value                   |
| --------------- | ----------------------- |
| Board           | ESP32C3 Dev Module      |
| USB CDC On Boot | **Enabled** ← important |
| CPU Frequency   | 160 MHz                 |
| Upload Speed    | 921600                  |

### Step 5 — Upload the sketch

1. Clone or download this repo
2. Open `clawd_mochi/clawd_mochi.ino` in Arduino IDE
3. Connect the ESP32 via USB-C
4. Select the correct port under **Tools → Port**
5. Click **Upload** (→ arrow button)
6. Wait for "Hard resetting via RTS pin..." — this means success

---

## How to use it

### First-time WiFi setup

On first power-up Mochi has no saved network and opens a setup hotspot.

1. Power the ESP32 via USB-C
2. Wait for the boot logo, then "SETUP MODE" with the hotspot details
3. On your phone, join **`ClaWD-Mochi`** (password: **`clawd1234`**)
4. Open **`http://192.168.4.1`** in a browser
5. Enter your home WiFi SSID and password → "SAVE & REBOOT"

After reboot, Mochi connects to your home WiFi and the display shows its **new IP address**. From then on, just open that IP in a browser to use the controller — no hotspot, no app.

If Mochi can't reach the saved network (you moved, changed router), it automatically falls back to setup mode so you can re-provision.

### The controller

<img src="pics/clawd_mochi_webpage.jpeg" alt="Webpage view" width="500"/>

### Controller features

| Button / control   | What it does                                    |
| ------------------ | ----------------------------------------------- |
| Normal eyes        | Plays wiggle + blink animation                  |
| Squish eyes        | Plays open/close animation                      |
| Claude Code        | Shows code display, opens terminal              |
| Canvas             | Enter drawing mode — draw on display from phone |
| Speed slider       | Controls animation speed (slow / normal / fast) |
| Background color   | Changes background color of all views           |
| Pen color          | Sets drawing color for canvas                   |
| Display on/off     | Toggles the backlight                           |
| Sound on/off       | Toggles the passive buzzer                      |
| ✓ done (in canvas) | Exits canvas mode                               |

---

## Connect to Claude Code

This is the main reason to add the buzzer + TTP223. Mochi can mirror your real Claude Code session:

- Claude is thinking → squish eyes pulse
- Claude calls a tool (`Bash`, `Edit`, etc.) → tool name on screen + buzzer tick
- Claude requests permission → flashing frame + alert tone; **pat Mochi on the head** to confirm, or hold the touch to dismiss
- Claude finishes → satisfied eyes + chime; you can walk away and come back when Mochi calls

It's a one-time install:

```bash
git clone <this repo>
cd clawd-mochi
bash bridge/install.sh        # asks for Mochi's IP, registers Claude Code hooks
```

Restart Claude Code and Mochi will react to your next session.

### Permission loop — pat Mochi to approve

When Claude wants to run a **dangerous tool** (`Bash`, `Edit`, `Write`, `MultiEdit`, `NotebookEdit`) it's gated by a `PreToolUse` hook that asks Mochi instead of the terminal:

- **Pat Mochi's head** (TTP223 short tap) **or tap the desk** (MPU6050) → **allow**
- **Hold the touch** for >800ms → **deny**
- **Double tap** or wait 15s → falls through to Claude's normal terminal y/n prompt (escape hatch)

Other tools (`Read`, `Glob`, `Grep`, etc.) pass through silently — no gating, no buzzer. Tune via `bridge/config.json` (`permission_loop_enabled`, `permission_timeout_s`).

### Multiple Claude Code sessions

If you run two Claude Code terminals at the same time, Mochi mirrors **whichever session most recently submitted a prompt**. So if you switch terminals and hit enter, that session "takes over" the display. Sessions you ignore stop showing on Mochi until you go back and submit again.

(Per-window focus tracking like the desktop variant would need macOS Accessibility APIs; the bridge keeps the dependency story zero — "last prompt wins" covers the common case.)

See [bridge/README.md](bridge/README.md) for full install / config / uninstall details.

---

## 3D case

The electronics case (body + back) is in the `clawd_mochi` model folder:

| File                                                                                 | Description                               |
| ------------------------------------------------------------------------------------ | ----------------------------------------- |
| [`./models/clawd_mochi/clawd_mochi_v1.stl`](./models/clawd_mochi/clawd_mochi_v1.stl) | Main case layout with body and back parts |

### Print settings

| Setting      | Value                               |
| ------------ | ----------------------------------- |
| Material     | PLA or PETG                         |
| Layer height | 0.15–0.20 mm                        |
| Infill       | 15% gyroid                          |
| Supports     | Yes — for display window overhang   |
| Orientation  | Face-down, flat back on build plate |

Suggested colors: orange PLA for body, matte black for back plate.

You can also download the models from MakerWorld: [https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)

### 3D Clawd (no electronics)

If you just want a display piece, use the separate 3D Clawd model (no screen or electronics cutouts).

<img src="pics/clawd_3D_squished_eyes_4_3.png" alt="3D printed Clawd model with squished eyes" width="500"/>

Model files:

| File | Description |
| ---- | ----------- |
| [`./models/clawd_3d/clawd_3D_no_AMS.stl`](./models/clawd_3d/clawd_3D_no_AMS.stl) | Original Clawd 3D model |
| [`./models/clawd_3d_squished_eyes/clawd_3D_squished_eyes_no_AMS.stl`](./models/clawd_3d_squished_eyes/clawd_3D_squished_eyes_no_AMS.stl) | Squished eyes variant |

You can also download the models from MakerWorld: [https://makerworld.com/en/models/2576503-clawd-claude-code-mascot#profileId-2841183](https://makerworld.com/en/models/2576503-clawd-claude-code-mascot#profileId-2841183)

---

## Assembly tips

1. Print the case file (body + back) and test-fit the display before gluing anything
2. Thread the 8 wires through the back plate slot before soldering
3. Use double-sided tape to fix the ESP32 against the inside of the back plate
4. Secure the display with 2× M2×6mm screws through the bezel holes
5. Route the USB-C cable through the back plate slot and snap the back on

---

## Customisation

### Eye size and position

Edit these constants near the top of `clawd_mochi.ino`:

```cpp
#define EYE_W   30    // eye width in pixels
#define EYE_H   60    // eye height in pixels
#define EYE_GAP 120   // gap between eyes
#define EYE_OX  0     // horizontal offset
#define EYE_OY  40    // vertical offset upward
```

### Logo animation duration

```cpp
// In animLogoReveal() — how long logo holds after animation
delay(1500);       // milliseconds — change this number

// Speed of the reveal drawing stroke by stroke
delay(speedMs(8)); // lower = faster
```

---

## Contributing

Contributions are very welcome! Here are some ideas:

- **New animations** — add new expressions, transitions, or idle behaviors
- **New views** — weather display, clock, notification badges, pixel art scenes
- **Sound** — add a small buzzer for sound effects
- **Sensors** — connect a touch sensor or button for physical interaction
- **OTA updates** — add over-the-air firmware updates
- **MQTT / Home Assistant** — connect to smart home platforms

To contribute: fork the repo, make your changes, and open a pull request. Please keep the single-file structure (`clawd_mochi.ino`) so it stays easy for beginners to flash.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

**Note:** 3D models and media assets are licensed under **CC BY-NC-SA 4.0**.
