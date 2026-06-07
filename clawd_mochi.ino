/*
 * ╔══════════════════════════════════════════════════════════════╗
 *   CLAWD MOCHI — ESP32-C3 Super Mini + ST7789 1.54" 240×240
 *
 *   Wiring:
 *     SDA → GPIO 10  (hardware SPI MOSI)
 *     SCL → GPIO 8   (hardware SPI SCK)
 *     RST → GPIO 2
 *     DC  → GPIO 1
 *     CS  → GPIO 4
 *     BL  → GPIO 3
 *     VCC → 3V3
 *     GND → GND
 *
 *   WiFi: "ClaWD-Mochi"  pw: clawd1234  → http://192.168.4.1
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS  4
#define TFT_DC  1
#define TFT_RST 2
#define TFT_BLK 3

#define BUZZER_PIN 5   // passive piezo buzzer (PWM via tone()/LEDC)
#define TOUCH_PIN  0   // TTP223 OUT (active-HIGH by default — touch through case)
#define BOOT_PIN   9   // on-board BOOT button (active-LOW) — dev fallback ack
#define I2C_SDA   20   // freed because USB CDC On Boot means Serial uses USB
#define I2C_SCL   21

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ── WiFi ──────────────────────────────────────────────────────
const char* AP_SSID = "ClaWD-Mochi";
const char* AP_PASS = "clawd1234";
WebServer server(80);

// Provisioning / runtime mode
bool       apMode      = false;   // true while soft-AP provisioning
String     staSsid     = "";      // loaded from NVS on boot
String     staPass     = "";
String     staIpStr    = "";      // human-readable when STA connected
Preferences prefs;

// MPU6050 (optional accelerometer)
Adafruit_MPU6050 mpu;
bool          mpuPresent     = false;
unsigned long lastMpuReadMs  = 0;
unsigned long lastGestureMs  = 0;
String        lastGesture    = "none";   // "tap", "shake", "none"

// ── Display ───────────────────────────────────────────────────
#define DISP_W 240
#define DISP_H 240

// ── Eye constants (shared by both eye views) ──────────────────
#define EYE_W   30
#define EYE_H   60
#define EYE_GAP 120
#define EYE_OX  0     // horizontal offset
#define EYE_OY  40    // vertical offset upward (subtracted from centre)

// ── Colours ───────────────────────────────────────────────────
uint16_t C_ORANGE, C_DARKBG, C_MUTED, C_GREEN;
#define C_WHITE ST77XX_WHITE
#define C_BLACK ST77XX_BLACK

// ── State ─────────────────────────────────────────────────────
#define VIEW_EYES_NORMAL 0
#define VIEW_EYES_SQUISH 1
#define VIEW_CODE        2
#define VIEW_DRAW        3
#define VIEW_WORKING     4
#define VIEW_PERMISSION  5
#define VIEW_ERROR       6

uint8_t  currentView  = VIEW_EYES_NORMAL;
bool     busy         = false;
bool     backlightOn  = true;
uint8_t  animSpeed    = 1;   // 1=slow(default) 2=normal 3=fast

// ── Claude Code session state machine (high-level) ────────────
// Mapped from Mac-side Claude Code hooks via /event endpoint.
enum SessionState {
  SS_IDLE,        // no session active or quiet
  SS_THINKING,    // UserPromptSubmit → model thinking
  SS_WORKING,     // PreToolUse → tool executing
  SS_PERMISSION,  // Notification → permission request pending
  SS_DONE,        // Stop → finished, returns to idle after a beat
  SS_ERROR        // tool / model error
};
SessionState sessionState   = SS_IDLE;
String       sessionMeta    = "";   // tool name, permission desc, etc.
bool         autoMode       = true; // /event drives display when true
unsigned long lastSessionEventMs = 0;
unsigned long sessionAnimNextMs   = 0;   // next tick for breathing animations
uint8_t       sessionAnimPhase    = 0;   // 0/1 toggle for pulse

uint16_t animBgColor  = 0;   // background for eye/logo animations
uint16_t drawBgColor  = 0;   // background for canvas

bool     buzzerMuted  = false;

// ── Terminal ──────────────────────────────────────────────────
#define TERM_COLS      15
#define TERM_ROWS       8
#define TERM_CHAR_W    12
#define TERM_CHAR_H    20
#define TERM_PAD_X      8
#define TERM_PAD_Y     18

bool    termMode    = false;
String  termLines[TERM_ROWS];
uint8_t termRow     = 0;
uint8_t termCol     = 0;

// ── Logo data ─────────────────────────────────────────────────
#define LOGO_CX 120
#define LOGO_CY 105

#define LOGO_TRI_COUNT 162
static const int16_t LOGO_TRIS[][6] PROGMEM = {
  {120,105,65,134,100,114},{120,105,100,114,101,113},{120,105,101,113,100,112},
  {120,105,100,112,99,112},{120,105,99,112,93,111},{120,105,93,111,73,111},
  {120,105,73,111,55,110},{120,105,55,110,38,109},{120,105,38,109,34,108},
  {120,105,34,108,30,103},{120,105,30,103,30,100},{120,105,30,100,34,98},
  {120,105,34,98,39,98},{120,105,39,98,50,99},{120,105,50,99,67,100},
  {120,105,67,100,80,101},{120,105,80,101,98,103},{120,105,98,103,101,103},
  {120,105,101,103,101,102},{120,105,101,102,100,101},{120,105,100,101,100,100},
  {120,105,100,100,82,88},{120,105,82,88,63,76},{120,105,63,76,53,69},
  {120,105,53,69,48,65},{120,105,48,65,45,61},{120,105,45,61,44,54},
  {120,105,44,54,49,49},{120,105,49,49,55,49},{120,105,55,49,57,49},
  {120,105,57,49,64,55},{120,105,64,55,78,66},{120,105,78,66,96,79},
  {120,105,96,79,99,81},{120,105,99,81,100,81},{120,105,100,81,100,80},
  {120,105,100,80,99,78},{120,105,99,78,89,60},{120,105,89,60,78,41},
  {120,105,78,41,73,34},{120,105,73,34,72,29},{120,105,72,29,72,28},
  {120,105,72,28,72,27},{120,105,72,27,71,26},{120,105,71,26,71,25},
  {120,105,71,25,71,24},{120,105,71,24,77,16},{120,105,77,16,80,15},
  {120,105,80,15,87,16},{120,105,87,16,91,19},{120,105,91,19,95,29},
  {120,105,95,29,103,46},{120,105,103,46,114,68},{120,105,114,68,118,75},
  {120,105,118,75,119,81},{120,105,119,81,120,83},{120,105,120,83,121,83},
  {120,105,121,83,121,82},{120,105,121,82,122,69},{120,105,122,69,124,54},
  {120,105,124,54,126,34},{120,105,126,34,126,28},{120,105,126,28,129,21},
  {120,105,129,21,135,18},{120,105,135,18,139,20},{120,105,139,20,143,25},
  {120,105,143,25,142,28},{120,105,142,28,140,42},{120,105,140,42,136,64},
  {120,105,136,64,133,78},{120,105,133,78,135,78},{120,105,135,78,136,76},
  {120,105,136,76,144,67},{120,105,144,67,156,51},{120,105,156,51,162,45},
  {120,105,162,45,168,38},{120,105,168,38,172,35},{120,105,172,35,180,35},
  {120,105,180,35,185,43},{120,105,185,43,183,52},{120,105,183,52,175,62},
  {120,105,175,62,168,71},{120,105,168,71,159,83},{120,105,159,83,153,94},
  {120,105,153,94,154,94},{120,105,154,94,155,94},{120,105,155,94,176,90},
  {120,105,176,90,188,88},{120,105,188,88,201,85},{120,105,201,85,208,88},
  {120,105,208,88,208,91},{120,105,208,91,206,97},{120,105,206,97,191,101},
  {120,105,191,101,174,104},{120,105,174,104,148,110},{120,105,148,110,148,111},
  {120,105,148,111,148,111},{120,105,148,111,160,112},{120,105,160,112,165,112},
  {120,105,165,112,177,112},{120,105,177,112,200,114},{120,105,200,114,205,118},
  {120,105,205,118,209,123},{120,105,209,123,208,126},{120,105,208,126,199,131},
  {120,105,199,131,187,128},{120,105,187,128,159,121},{120,105,159,121,149,119},
  {120,105,149,119,147,119},{120,105,147,119,147,120},{120,105,147,120,156,128},
  {120,105,156,128,170,141},{120,105,170,141,189,158},{120,105,189,158,190,163},
  {120,105,190,163,188,166},{120,105,188,166,185,166},{120,105,185,166,169,153},
  {120,105,169,153,162,148},{120,105,162,148,148,136},{120,105,148,136,147,136},
  {120,105,147,136,147,137},{120,105,147,137,150,142},{120,105,150,142,168,168},
  {120,105,168,168,169,176},{120,105,169,176,168,179},{120,105,168,179,163,180},
  {120,105,163,180,158,179},{120,105,158,179,148,165},{120,105,148,165,137,149},
  {120,105,137,149,129,134},{120,105,129,134,128,135},{120,105,128,135,123,189},
  {120,105,123,189,120,192},{120,105,120,192,115,194},{120,105,115,194,110,191},
  {120,105,110,191,108,185},{120,105,108,185,110,174},{120,105,110,174,113,160},
  {120,105,113,160,116,148},{120,105,116,148,118,134},{120,105,118,134,119,129},
  {120,105,119,129,119,129},{120,105,119,129,118,129},{120,105,118,129,107,144},
  {120,105,107,144,91,166},{120,105,91,166,78,180},{120,105,78,180,75,181},
  {120,105,75,181,70,178},{120,105,70,178,70,173},{120,105,70,173,73,169},
  {120,105,73,169,91,146},{120,105,91,146,102,132},{120,105,102,132,109,124},
  {120,105,109,124,109,123},{120,105,109,123,108,123},{120,105,108,123,61,153},
  {120,105,61,153,52,155},{120,105,52,155,49,151},{120,105,49,151,49,146},
  {120,105,49,146,51,144},{120,105,51,144,65,134},{120,105,65,134,65,134},
};

#define LOGO_SEG_COUNT 162
static const int16_t LOGO_SEGS[][4] PROGMEM = {
  {65,134,100,114},{100,114,101,113},{101,113,100,112},{100,112,99,112},
  {99,112,93,111},{93,111,73,111},{73,111,55,110},{55,110,38,109},
  {38,109,34,108},{34,108,30,103},{30,103,30,100},{30,100,34,98},
  {34,98,39,98},{39,98,50,99},{50,99,67,100},{67,100,80,101},
  {80,101,98,103},{98,103,101,103},{101,103,101,102},{101,102,100,101},
  {100,101,100,100},{100,100,82,88},{82,88,63,76},{63,76,53,69},
  {53,69,48,65},{48,65,45,61},{45,61,44,54},{44,54,49,49},
  {49,49,55,49},{55,49,57,49},{57,49,64,55},{64,55,78,66},
  {78,66,96,79},{96,79,99,81},{99,81,100,81},{100,81,100,80},
  {100,80,99,78},{99,78,89,60},{89,60,78,41},{78,41,73,34},
  {73,34,72,29},{72,29,72,28},{72,28,72,27},{72,27,71,26},
  {71,26,71,25},{71,25,71,24},{71,24,77,16},{77,16,80,15},
  {80,15,87,16},{87,16,91,19},{91,19,95,29},{95,29,103,46},
  {103,46,114,68},{114,68,118,75},{118,75,119,81},{119,81,120,83},
  {120,83,121,83},{121,83,121,82},{121,82,122,69},{122,69,124,54},
  {124,54,126,34},{126,34,126,28},{126,28,129,21},{129,21,135,18},
  {135,18,139,20},{139,20,143,25},{143,25,142,28},{142,28,140,42},
  {140,42,136,64},{136,64,133,78},{133,78,135,78},{135,78,136,76},
  {136,76,144,67},{144,67,156,51},{156,51,162,45},{162,45,168,38},
  {168,38,172,35},{172,35,180,35},{180,35,185,43},{185,43,183,52},
  {183,52,175,62},{175,62,168,71},{168,71,159,83},{159,83,153,94},
  {153,94,154,94},{154,94,155,94},{155,94,176,90},{176,90,188,88},
  {188,88,201,85},{201,85,208,88},{208,88,208,91},{208,91,206,97},
  {206,97,191,101},{191,101,174,104},{174,104,148,110},{148,110,148,111},
  {148,111,148,111},{148,111,160,112},{160,112,165,112},{165,112,177,112},
  {177,112,200,114},{200,114,205,118},{205,118,209,123},{209,123,208,126},
  {208,126,199,131},{199,131,187,128},{187,128,159,121},{159,121,149,119},
  {149,119,147,119},{147,119,147,120},{147,120,156,128},{156,128,170,141},
  {170,141,189,158},{189,158,190,163},{190,163,188,166},{188,166,185,166},
  {185,166,169,153},{169,153,162,148},{162,148,148,136},{148,136,147,136},
  {147,136,147,137},{147,137,150,142},{150,142,168,168},{168,168,169,176},
  {169,176,168,179},{168,179,163,180},{163,180,158,179},{158,179,148,165},
  {148,165,137,149},{137,149,129,134},{129,134,128,135},{128,135,123,189},
  {123,189,120,192},{120,192,115,194},{115,194,110,191},{110,191,108,185},
  {108,185,110,174},{110,174,113,160},{113,160,116,148},{116,148,118,134},
  {118,134,119,129},{119,129,119,129},{119,129,118,129},{118,129,107,144},
  {107,144,91,166},{91,166,78,180},{78,180,75,181},{75,181,70,178},
  {70,178,70,173},{70,173,73,169},{73,169,91,146},{91,146,102,132},
  {102,132,109,124},{109,124,109,123},{109,123,108,123},{108,123,61,153},
  {61,153,52,155},{52,155,49,151},{49,151,49,146},{49,146,51,144},
  {51,144,65,134},{65,134,65,134},
};

// ═════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════

int speedMs(int ms) {
  if (animSpeed == 3) return ms / 2;
  if (animSpeed == 1) return ms * 2;
  return ms;
}

uint16_t hexToRgb565(String hex) {
  hex.replace("#", "");
  if (hex.length() != 6) return C_WHITE;
  long v = strtol(hex.c_str(), nullptr, 16);
  return tft.color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void setBacklight(bool on) {
  backlightOn = on;
  digitalWrite(TFT_BLK, on ? HIGH : LOW);
}

// ── Buzzer ────────────────────────────────────────────────────
// All events Phase A–D may emit. tone() on ESP32 is non-blocking
// (LEDC-backed); multi-note sequences use short blocking delays.
enum BeepEvent {
  BEEP_BOOT,        // ascending 3-note chord at boot
  BEEP_VIEW,        // single short blip on view switch
  BEEP_TYPE,        // tiny click per terminal char
  BEEP_THINK,       // low pulse while Claude is thinking
  BEEP_TOOL,        // soft tick on tool invocation
  BEEP_PERMISSION,  // distinctive 2-tone alert
  BEEP_DONE,        // satisfied 2-note up
  BEEP_ERROR,       // low growl
  BEEP_TOUCH_OK,    // touch short-press confirm
  BEEP_TOUCH_DENY   // touch long-press reject
};

void beep(BeepEvent e) {
  if (buzzerMuted) return;
  switch (e) {
    case BEEP_BOOT:
      tone(BUZZER_PIN, 660,  60); delay(70);
      tone(BUZZER_PIN, 880,  60); delay(70);
      tone(BUZZER_PIN, 1175, 90); break;
    case BEEP_VIEW:       tone(BUZZER_PIN, 1200, 35); break;
    case BEEP_TYPE:       tone(BUZZER_PIN, 2400,  8); break;
    case BEEP_THINK:      tone(BUZZER_PIN, 600,  60); break;
    case BEEP_TOOL:       tone(BUZZER_PIN, 1500, 30); break;
    case BEEP_PERMISSION:
      tone(BUZZER_PIN, 880, 80);  delay(100);
      tone(BUZZER_PIN, 880, 80);  break;
    case BEEP_DONE:
      tone(BUZZER_PIN, 880,  60); delay(70);
      tone(BUZZER_PIN, 1318, 100); break;
    case BEEP_ERROR:
      tone(BUZZER_PIN, 250, 150); delay(170);
      tone(BUZZER_PIN, 180, 200); break;
    case BEEP_TOUCH_OK:   tone(BUZZER_PIN, 1800, 30); break;
    case BEEP_TOUCH_DENY: tone(BUZZER_PIN, 400,  80); break;
  }
}

// ── NVS config (WiFi credentials) ──────────────────────────────
void cfgLoad() {
  prefs.begin("clawd", true);
  staSsid = prefs.getString("ssid", "");
  staPass = prefs.getString("pass", "");
  prefs.end();
}

void cfgSave(const String& ssid, const String& pass) {
  prefs.begin("clawd", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void cfgClear() {
  prefs.begin("clawd", false);
  prefs.clear();
  prefs.end();
}

// ── Touch input (TTP223 capacitive) ────────────────────────────
// Short tap (<800ms) = OK / confirm permission
// Long press (>=800ms) = deny permission / toggle buzzer mute
// Double tap (two releases <400ms apart) = force SS_IDLE

// Forward decl — defined in SESSION VIEWS section below.
void setSessionState(SessionState s, const String& meta = "");

#define TOUCH_DEBOUNCE_MS 25
#define TOUCH_LONG_MS    800
#define TOUCH_DOUBLE_MS  400

enum TouchPhase { TP_IDLE, TP_PRESSED, TP_RELEASED_WAITING };
TouchPhase    touchPhase        = TP_IDLE;
bool          touchPrev         = false;
unsigned long touchDebounceMs   = 0;
unsigned long touchPressedAt    = 0;
unsigned long touchReleasedAt   = 0;
String        touchLastEvent    = "none";
unsigned long touchLastEventMs  = 0;     // millis() stamp — bridge polls for changes

// BOOT key (GPIO 9, active LOW with internal pull-up)
bool          bootPrev          = true;  // pulled HIGH at idle
unsigned long bootDebounceMs    = 0;

void handleTouchShort() {
  touchLastEvent   = "short";
  touchLastEventMs = millis();
  if (sessionState == SS_PERMISSION) {
    // Bridge sees the touchTs change and writes back a permissionDecision.
    setSessionState(SS_THINKING, sessionMeta);
  }
  beep(BEEP_TOUCH_OK);
}

void handleTouchLong() {
  touchLastEvent   = "long";
  touchLastEventMs = millis();
  if (sessionState == SS_PERMISSION) {
    setSessionState(SS_IDLE);
    beep(BEEP_TOUCH_DENY);
  } else {
    buzzerMuted = !buzzerMuted;
    if (!buzzerMuted) beep(BEEP_TOUCH_OK);
  }
}

void handleTouchDouble() {
  touchLastEvent   = "double";
  touchLastEventMs = millis();
  setSessionState(SS_IDLE);
  beep(BEEP_TOUCH_OK);
}

void tickTouch() {
  unsigned long now = millis();
  bool raw = digitalRead(TOUCH_PIN);   // HIGH while finger present

  if (raw != touchPrev) {
    if (now - touchDebounceMs > TOUCH_DEBOUNCE_MS) {
      touchPrev       = raw;
      touchDebounceMs = now;

      if (raw) {  // press edge
        if (touchPhase == TP_RELEASED_WAITING &&
            now - touchReleasedAt < TOUCH_DOUBLE_MS) {
          handleTouchDouble();
          touchPhase = TP_IDLE;
        } else {
          touchPressedAt = now;
          touchPhase     = TP_PRESSED;
        }
      } else {    // release edge
        unsigned long held = now - touchPressedAt;
        if (held >= TOUCH_LONG_MS) {
          handleTouchLong();
          touchPhase = TP_IDLE;
        } else {
          touchReleasedAt = now;
          touchPhase      = TP_RELEASED_WAITING;
        }
      }
    }
  }

  // Resolve a pending short tap once the double-tap window expires
  if (touchPhase == TP_RELEASED_WAITING &&
      now - touchReleasedAt > TOUCH_DOUBLE_MS) {
    handleTouchShort();
    touchPhase = TP_IDLE;
  }
}

// ── BOOT key (GPIO 9, dev fallback) ────────────────────────────
// Sealed cases hide this — useful only with case open / during bring-up.
void tickBoot() {
  unsigned long now = millis();
  bool raw = digitalRead(BOOT_PIN);   // LOW while pressed
  if (raw != bootPrev) {
    if (now - bootDebounceMs > 30) {
      bootPrev       = raw;
      bootDebounceMs = now;
      if (raw == LOW) handleTouchShort();   // press edge → same as tap
    }
  }
}

// ── MPU6050 gestures (tap + shake) ─────────────────────────────
// Sampled at 20Hz. Thresholds are starting points; tune on device.
void tickMpu() {
  if (!mpuPresent) return;
  unsigned long now = millis();
  if (now - lastMpuReadMs < 50) return;
  lastMpuReadMs = now;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  const float ax = a.acceleration.x;
  const float ay = a.acceleration.y;
  const float az = a.acceleration.z;
  const float mag = sqrtf(ax * ax + ay * ay + az * az);

  // ─ Tap: a sharp spike well above gravity (~1.8g).
  // 300ms refractory to avoid double-fire on a single physical hit.
  if (mag > 18.0f && now - lastGestureMs > 300) {
    lastGesture   = "tap";
    lastGestureMs = now;
    handleTouchShort();   // confirms / acks permission, same as TTP223 short
    return;
  }

  // ─ Shake: count X-axis zero-crossings over a 1s window.
  static int8_t prevSign        = 0;
  static uint8_t crossCount     = 0;
  static unsigned long winStart = 0;
  if (now - winStart > 1000) {
    if (crossCount >= 5 && now - lastGestureMs > 600) {
      lastGesture   = "shake";
      lastGestureMs = now;
      setSessionState(SS_IDLE);
      beep(BEEP_TOUCH_OK);
    }
    crossCount = 0;
    winStart   = now;
  }
  int8_t sign = (ax > 2.0f) ? 1 : (ax < -2.0f ? -1 : 0);
  if (sign != 0 && prevSign != 0 && sign != prevSign) crossCount++;
  if (sign != 0) prevSign = sign;
}

void initColours() {
  // C_ORANGE = tft.color565(170, 72, 28);
  C_ORANGE = tft.color565(218, 17, 0);
  C_DARKBG = tft.color565(10,  12,  16);
  C_MUTED  = tft.color565(90,  88,  86);
  C_GREEN  = tft.color565(80, 220, 130);
  animBgColor = C_ORANGE;
  drawBgColor = C_ORANGE;
}

// ═════════════════════════════════════════════════════════════
//  LOGO
// ═════════════════════════════════════════════════════════════

void drawLogoFilled(uint16_t bg, uint16_t fg) {
  tft.fillScreen(bg);
  for (uint16_t i = 0; i < LOGO_TRI_COUNT; i++) {
    tft.fillTriangle(
      pgm_read_word(&LOGO_TRIS[i][0]), pgm_read_word(&LOGO_TRIS[i][1]),
      pgm_read_word(&LOGO_TRIS[i][2]), pgm_read_word(&LOGO_TRIS[i][3]),
      pgm_read_word(&LOGO_TRIS[i][4]), pgm_read_word(&LOGO_TRIS[i][5]),
      fg);
  }
  tft.setTextColor(fg); tft.setTextSize(2);
  tft.setCursor(LOGO_CX - 54, 210); tft.print("Anthropic");
  tft.setCursor(LOGO_CX - 53, 210); tft.print("Anthropic");
}

// ═════════════════════════════════════════════════════════════
//  VIEWS
// ═════════════════════════════════════════════════════════════

// Eye helpers — shared constants via #define EYE_*
inline int16_t eyeLX(int16_t ox) {
  return (DISP_W - (EYE_W * 2 + EYE_GAP)) / 2 + EYE_OX + ox;
}
inline int16_t eyeRX(int16_t ox) { return eyeLX(ox) + EYE_W + EYE_GAP; }
inline int16_t eyeY()            { return (DISP_H - EYE_H) / 2 - EYE_OY; }
inline int16_t eyeCY()           { return eyeY() + EYE_H / 2; }

void drawNormalEyes(int16_t ox = 0, bool blink = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeY();
  if (!blink) {
    tft.fillRect(lx, ey, EYE_W, EYE_H, C_BLACK);
    tft.fillRect(rx, ey, EYE_W, EYE_H, C_BLACK);
  } else {
    tft.fillRect(lx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
    tft.fillRect(rx, ey + EYE_H / 2 - 3, EYE_W, 6, C_BLACK);
  }
}

void drawChevron(int16_t cx, int16_t cy, int16_t arm, int16_t reach,
                 uint8_t thk, bool rightFacing, uint16_t col) {
  for (int8_t t = -(int8_t)thk; t <= (int8_t)thk; t++) {
    if (rightFacing) {
      tft.drawLine(cx - reach/2, cy - arm + t, cx + reach/2, cy + t,      col);
      tft.drawLine(cx + reach/2, cy + t,       cx - reach/2, cy + arm + t, col);
    } else {
      tft.drawLine(cx + reach/2, cy - arm + t, cx - reach/2, cy + t,      col);
      tft.drawLine(cx - reach/2, cy + t,       cx + reach/2, cy + arm + t, col);
    }
  }
}

void drawSquishEyes(bool closed = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  const int16_t arm   = EYE_H / 2;
  const int16_t reach = EYE_W / 2;
  const int16_t lcx   = lx + EYE_W / 2;
  const int16_t rcx   = rx + EYE_W / 2;
  if (!closed) {
    drawChevron(lcx, cy, arm, reach, 10, true,  C_BLACK);
    drawChevron(rcx, cy, arm, reach, 10, false, C_BLACK);
  } else {
    tft.fillRect(lx, cy - 5, EYE_W, 10, C_BLACK);
    tft.fillRect(rx, cy - 5, EYE_W, 10, C_BLACK);
  }
}

void drawCodeView() {
  termMode = false;
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0,          DISP_W, 4, C_ORANGE);
  tft.fillRect(0, DISP_H - 4, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_ORANGE); tft.setTextSize(4);
  tft.setCursor((DISP_W - 144) / 2, DISP_H / 2 - 52); tft.print("Claude");
  tft.setTextColor(C_WHITE);  tft.setTextSize(4);
  tft.setCursor((DISP_W - 96) / 2,  DISP_H / 2 + 8);  tft.print("Code");
  tft.fillRect((DISP_W - 96) / 2, DISP_H / 2 + 52, 96, 3, C_ORANGE);
}

// ═════════════════════════════════════════════════════════════
//  TERMINAL
// ═════════════════════════════════════════════════════════════

void termClear() {
  for (uint8_t i = 0; i < TERM_ROWS; i++) termLines[i] = "";
  termRow = 0; termCol = 0;
}

void termDrawHeader() {
  tft.fillRect(0, 0, DISP_W, TERM_PAD_Y + 1, C_DARKBG);
  tft.setTextColor(C_ORANGE); tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, 4); tft.print("clawd@mochi terminal");
  tft.drawFastHLine(0, TERM_PAD_Y, DISP_W, C_ORANGE);
}

// Prefix "clawd:~$ " in green, drawn only when the row has content
void termDrawPrefix(int16_t yy) {
  tft.setTextColor(C_GREEN); tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, yy + 6);
  tft.print("clawd:~$ ");
}

#define PREFIX_PX 54   // 9 chars × 6px = 54px at textSize 1

void termDrawLine(uint8_t r) {
  const int16_t yy = TERM_PAD_Y + 4 + r * TERM_CHAR_H;
  tft.fillRect(0, yy, DISP_W, TERM_CHAR_H, C_DARKBG);
  // show prefix only on the currently active (cursor) line
  if (r == termRow) termDrawPrefix(yy);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(TERM_PAD_X + PREFIX_PX, yy + 1);
  tft.print(termLines[r]);
  if (r == termRow) {
    const int16_t cx = TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W;
    tft.fillRect(cx, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  }
}

void termDrawLastChar() {
  if (termCol == 0) return;
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  const uint8_t prev  = termCol - 1;
  // erase prev cell (had cursor block)
  tft.fillRect(baseX + prev * TERM_CHAR_W, yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(baseX + prev * TERM_CHAR_W, yy + 1);
  tft.print(termLines[termRow][prev]);
  // new cursor
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
}

void termDrawBackspace() {
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  // erase deleted char + old cursor
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W * 2, TERM_CHAR_H - 1, C_DARKBG);
  // new cursor
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  // if line now empty, erase the prefix too
  if (termLines[termRow].length() == 0) {
    tft.fillRect(0, yy, TERM_PAD_X + PREFIX_PX, TERM_CHAR_H, C_DARKBG);
  }
}

void termFullRedraw() {
  tft.fillScreen(C_DARKBG);
  termDrawHeader();
  for (uint8_t r = 0; r < TERM_ROWS; r++) termDrawLine(r);
}

void termScroll() {
  for (uint8_t i = 0; i < TERM_ROWS - 1; i++) termLines[i] = termLines[i + 1];
  termLines[TERM_ROWS - 1] = "";
  termRow = TERM_ROWS - 1;
  termFullRedraw();
}

void termAddChar(char c) {
  if (c == '\n' || c == '\r') {
    const int16_t yy = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
    // erase cursor on current row
    tft.fillRect(TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W,
                 yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
    termRow++; termCol = 0;
    if (termRow >= TERM_ROWS) { termScroll(); return; }
    termDrawLine(termRow);  // draws prefix on new line
  } else if (c == '\b' || c == 127) {
    if (termCol > 0) {
      termCol--;
      termLines[termRow].remove(termLines[termRow].length() - 1);
      termDrawBackspace();
    }
  } else if (c >= 32 && c < 127) {
    if (termCol >= TERM_COLS) {
      termRow++; termCol = 0;
      if (termRow >= TERM_ROWS) { termScroll(); return; }
    }
    // draw prefix on first char of this line
    if (termCol == 0) termDrawPrefix(TERM_PAD_Y + 4 + termRow * TERM_CHAR_H);
    termLines[termRow] += c;
    termCol++;
    termDrawLastChar();
  }
}

// ═════════════════════════════════════════════════════════════
//  ANIMATIONS
// ═════════════════════════════════════════════════════════════

void animNormalEyes() {
  busy = true;
  const int16_t offs[] = {-16, 16, -16, 16, 0};
  for (uint8_t i = 0; i < 5; i++) { drawNormalEyes(offs[i]); delay(speedMs(80)); }
  drawNormalEyes(0, true);  delay(speedMs(100));
  drawNormalEyes(0, false); delay(speedMs(70));
  drawNormalEyes(0, true);  delay(speedMs(70));
  drawNormalEyes(0, false);
  busy = false;
}

void animSquishEyes() {
  busy = true;
  for (uint8_t i = 0; i < 3; i++) {
    drawSquishEyes(false); delay(speedMs(160));
    drawSquishEyes(true);  delay(speedMs(100));
  }
  drawSquishEyes(false);
  busy = false;
}

void animLogoReveal() {
  busy = true;
  tft.fillScreen(animBgColor);
  for (uint16_t i = 0; i < LOGO_SEG_COUNT; i++) {
    int16_t x1 = pgm_read_word(&LOGO_SEGS[i][0]);
    int16_t y1 = pgm_read_word(&LOGO_SEGS[i][1]);
    int16_t x2 = pgm_read_word(&LOGO_SEGS[i][2]);
    int16_t y2 = pgm_read_word(&LOGO_SEGS[i][3]);
    tft.drawLine(x1, y1, x2, y2, C_WHITE);
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, C_WHITE);
    if (i % 4 == 0) { server.handleClient(); delay(speedMs(8)); }
  }
  drawLogoFilled(animBgColor, C_WHITE);
  delay(1500);
  busy = false;
}

// ═════════════════════════════════════════════════════════════
//  SESSION VIEWS (Claude Code state mirror)
// ═════════════════════════════════════════════════════════════

// Centred text helper for full-width labels (textSize=2, char w=12)
void centredText(const String& s, int16_t y, uint16_t col, uint8_t size) {
  tft.setTextSize(size);
  const int16_t w = s.length() * 6 * size;
  tft.setCursor((DISP_W - w) / 2, y);
  tft.setTextColor(col);
  tft.print(s);
}

void drawWorkingView(const String& tool) {
  tft.fillScreen(C_DARKBG);
  // top strip
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);
  // small concentrating squish eyes, upper third
  const int16_t lx = 60, rx = 150, cy = 70;
  drawChevron(lx + 15, cy, 18, 12, 6, true,  C_WHITE);
  drawChevron(rx + 15, cy, 18, 12, 6, false, C_WHITE);
  // tool label
  centredText("WORKING", 120, C_MUTED, 1);
  String t = tool.length() ? tool : "...";
  if (t.length() > 14) t = t.substring(0, 14);
  centredText(t, 140, C_ORANGE, 2);
  // bottom strip
  tft.fillRect(0, DISP_H - 4, DISP_W, 4, C_ORANGE);
}

void drawPermissionView(const String& desc) {
  tft.fillScreen(C_DARKBG);
  // thick orange frame to signal "attention"
  tft.fillRect(0, 0,          DISP_W, 8, C_ORANGE);
  tft.fillRect(0, DISP_H - 8, DISP_W, 8, C_ORANGE);
  tft.fillRect(0,          0, 8, DISP_H, C_ORANGE);
  tft.fillRect(DISP_W - 8, 0, 8, DISP_H, C_ORANGE);
  // big surprised eyes
  const int16_t ey  = 40;
  const int16_t lx  = 50, rx = 150;
  const int16_t eyW = 40, eyH = 50;
  tft.fillRect(lx, ey, eyW, eyH, C_WHITE);
  tft.fillRect(rx, ey, eyW, eyH, C_WHITE);
  tft.fillCircle(lx + eyW / 2, ey + eyH / 2, 6, C_BLACK);
  tft.fillCircle(rx + eyW / 2, ey + eyH / 2, 6, C_BLACK);
  // labels
  centredText("PERMISSION?", 110, C_ORANGE, 2);
  String d = desc;
  if (d.length() > 26) d = d.substring(0, 23) + "...";
  centredText(d, 140, C_MUTED, 1);
  centredText("tap = OK", 168, C_GREEN, 1);
  centredText("hold = NO", 184, C_ORANGE, 1);
}

void drawErrorView(const String& msg) {
  tft.fillScreen(tft.color565(40, 8, 8));
  // × eyes
  const int16_t lcx = 70, rcx = 170, cy = 80, arm = 22;
  for (int8_t t = -3; t <= 3; t++) {
    tft.drawLine(lcx - arm, cy - arm + t, lcx + arm, cy + arm + t, C_WHITE);
    tft.drawLine(lcx + arm, cy - arm + t, lcx - arm, cy + arm + t, C_WHITE);
    tft.drawLine(rcx - arm, cy - arm + t, rcx + arm, cy + arm + t, C_WHITE);
    tft.drawLine(rcx + arm, cy - arm + t, rcx - arm, cy + arm + t, C_WHITE);
  }
  centredText("ERROR", 150, C_WHITE, 3);
  if (msg.length()) {
    String m = msg;
    if (m.length() > 26) m = m.substring(0, 23) + "...";
    centredText(m, 200, C_MUTED, 1);
  }
}

// Force the screen to redraw whatever the current state says.
void redrawCurrentView();   // forward decl

void setSessionState(SessionState s, const String& meta) {
  sessionState        = s;
  sessionMeta         = meta;
  lastSessionEventMs  = millis();
  sessionAnimPhase    = 0;
  sessionAnimNextMs   = 0;
  if (!autoMode) return;

  switch (s) {
    case SS_IDLE:
      currentView = VIEW_EYES_NORMAL;
      drawNormalEyes();
      break;
    case SS_THINKING:
      currentView = VIEW_EYES_SQUISH;
      drawSquishEyes(false);
      beep(BEEP_THINK);
      break;
    case SS_WORKING:
      currentView = VIEW_WORKING;
      drawWorkingView(meta);
      beep(BEEP_TOOL);
      break;
    case SS_PERMISSION:
      currentView = VIEW_PERMISSION;
      drawPermissionView(meta);
      beep(BEEP_PERMISSION);
      break;
    case SS_DONE:
      currentView = VIEW_EYES_SQUISH;
      drawSquishEyes(false);
      beep(BEEP_DONE);
      break;
    case SS_ERROR:
      currentView = VIEW_ERROR;
      drawErrorView(meta);
      beep(BEEP_ERROR);
      break;
  }
}

// Non-blocking "breathing" for idle / thinking / permission.
void tickSessionAnim() {
  if (!autoMode || busy) return;
  unsigned long now = millis();
  if (now < sessionAnimNextMs) return;

  switch (sessionState) {
    case SS_THINKING:
      // pulse open/closed squish every 600ms
      sessionAnimPhase ^= 1;
      drawSquishEyes(sessionAnimPhase);
      sessionAnimNextMs = now + 600;
      break;
    case SS_IDLE:
      // gentle side-to-side wiggle every 4s, single notch
      sessionAnimPhase = (sessionAnimPhase + 1) % 4;
      drawNormalEyes((sessionAnimPhase == 1) ? -6 :
                     (sessionAnimPhase == 3) ?  6 : 0);
      sessionAnimNextMs = now + 4000;
      break;
    case SS_PERMISSION:
      // flash the orange frame
      sessionAnimPhase ^= 1;
      {
        const uint16_t col = sessionAnimPhase ? C_ORANGE : C_DARKBG;
        tft.fillRect(0, 0,          DISP_W, 8, col);
        tft.fillRect(0, DISP_H - 8, DISP_W, 8, col);
        tft.fillRect(0,          0, 8, DISP_H, col);
        tft.fillRect(DISP_W - 8, 0, 8, DISP_H, col);
      }
      sessionAnimNextMs = now + 500;
      break;
    case SS_DONE:
      // After 3s, drop back to idle
      if (now - lastSessionEventMs > 3000) {
        setSessionState(SS_IDLE);
      } else {
        sessionAnimNextMs = now + 500;
      }
      break;
    default: break;
  }
}

void redrawCurrentView() {
  switch (currentView) {
    case VIEW_EYES_NORMAL: drawNormalEyes(); break;
    case VIEW_EYES_SQUISH: drawSquishEyes(); break;
    case VIEW_CODE:        drawCodeView();   break;
    case VIEW_DRAW:        tft.fillScreen(drawBgColor); break;
    case VIEW_WORKING:     drawWorkingView(sessionMeta); break;
    case VIEW_PERMISSION:  drawPermissionView(sessionMeta); break;
    case VIEW_ERROR:       drawErrorView(sessionMeta); break;
  }
}

// ═════════════════════════════════════════════════════════════
//  WEB PAGE
// ═════════════════════════════════════════════════════════════
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Clawd Mochi</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{background:#1c1c20;font-family:'Courier New',monospace;color:#e8e4dc;
  display:flex;flex-direction:column;align-items:center;
  padding:20px 14px 52px;gap:14px;min-height:100vh}

.hdr{text-align:center;padding:2px 0 4px}
.mascot{font-size:15px;color:#c96a3e;line-height:1.3;font-weight:bold;
  font-family:'Courier New',monospace;display:block;letter-spacing:1px}
.sitename{font-size:10px;color:#5a5048;margin-top:8px;letter-spacing:3px}

.sec{width:100%;max-width:390px;font-size:10px;color:#8a8278;
  letter-spacing:2px;font-weight:bold;padding:0 2px}

/* Busy bar */
.busy{width:100%;max-width:390px;height:2px;background:#2e2a28;
  border-radius:1px;overflow:hidden;opacity:0;transition:opacity .2s}
.busy.show{opacity:1}
.busy-i{height:100%;width:30%;background:#c96a3e;border-radius:1px;
  animation:sl 1s linear infinite}
@keyframes sl{0%{margin-left:-30%}100%{margin-left:100%}}

/* Controls */
.ctrl{display:flex;gap:8px;width:100%;max-width:390px}
.cbtn{flex:1;background:#252428;border:1.5px solid #38343a;border-radius:10px;
  color:#b8b4ac;font-family:'Courier New',monospace;font-size:11px;font-weight:bold;
  padding:12px 4px;cursor:pointer;text-align:center;transition:all .12s}
.cbtn:active:not(:disabled){transform:scale(.94)}
.cbtn:disabled{opacity:.3;cursor:default}
.cbtn.on{border-color:#c96a3e;color:#c96a3e;background:#201408}
.cbtn.dim{border-color:#2e2a28;color:#4a4540}

/* View grid */
.vgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px;width:100%;max-width:390px}
.vbtn{background:#252428;border:1.5px solid #38343a;border-radius:12px;
  color:#d8d4cc;font-family:'Courier New',monospace;
  padding:14px 6px 10px;cursor:pointer;text-align:center;
  transition:all .12s;user-select:none}
.vbtn:active:not(:disabled){transform:scale(.94)}
.vbtn:disabled{opacity:.3;cursor:default}
.vbtn .ic{font-size:20px;display:block;margin-bottom:4px;line-height:1;color:#c96a3e}
.vbtn .nm{font-size:12px;font-weight:bold;color:#e8e4dc}
.vbtn .ht{font-size:9px;color:#8a8278;margin-top:3px}
.vbtn.active{border-color:#c96a3e;background:#201408}
.vbtn[data-v="1"].active{border-color:#c96a3e;background:#201408}
.vbtn[data-v="2"].active{border-color:#4a8acd;background:#0c1628}
.vbtn[data-v="3"].active{border-color:#38343a;background:#201c18}

/* Speed slider */
.speed-row{width:100%;max-width:390px;display:flex;align-items:center;gap:10px}
.sl{font-size:10px;color:#6a6058;white-space:nowrap;min-width:36px}
input[type=range]{flex:1;accent-color:#c96a3e;cursor:pointer;height:20px}
.sv{font-size:11px;color:#c96a3e;min-width:44px;text-align:right;font-weight:bold}

/* Terminal */
.twrap{width:100%;max-width:390px;display:none;flex-direction:column;gap:8px}
.twrap.open{display:flex}
.thdr{display:flex;justify-content:space-between;align-items:center}
.tttl{font-size:11px;color:#28b878;letter-spacing:1px;font-weight:bold}
.tx{background:#0c1e12;border:2px solid #1a4828;border-radius:9px;
  color:#28b878;font-family:'Courier New',monospace;font-size:13px;
  font-weight:bold;padding:10px 18px;cursor:pointer}
.tx:active{background:#081410}
.trow{display:flex;gap:6px}
.tin{flex:1;background:#0c1018;border:1.5px solid #1a2820;border-radius:9px;
  color:#40d880;font-family:'Courier New',monospace;font-size:15px;
  padding:11px;outline:none}
.tin::placeholder{color:#2a3828}
.tgo{background:#1a9060;border:none;border-radius:9px;color:#fff;
  font-family:'Courier New',monospace;font-size:22px;font-weight:bold;
  padding:11px 16px;cursor:pointer;min-width:52px}
.tgo:active{background:#0f6040}

/* Canvas */
.cwrap{width:100%;max-width:390px;background:#222028;border:1.5px solid #38343a;
  border-radius:12px;padding:12px;flex-direction:column;gap:10px;display:none}
.cwrap.open{display:flex}
.crow{display:flex;gap:8px}
.ci{display:flex;flex-direction:column;align-items:center;gap:4px;flex:1}
.cl{font-size:10px;color:#7a7068;letter-spacing:1px;font-weight:bold}
.cs{width:100%;height:38px;border-radius:7px;border:1.5px solid #38343a;cursor:pointer;padding:0}
.dacts{display:flex;gap:7px}
.db{flex:1;background:#1c1820;border:1.5px solid #38343a;border-radius:9px;
  color:#c0bab8;font-family:'Courier New',monospace;font-size:11px;
  font-weight:bold;padding:11px 4px;cursor:pointer;transition:all .12s}
.db:active{transform:scale(.95);background:#281838}
.db.hi{border-color:#c96a3e;color:#c96a3e}
canvas{width:100%;border-radius:8px;border:1.5px solid #38343a;
  touch-action:none;cursor:crosshair;display:block}

/* Toast */
.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);
  background:#252428;border:1.5px solid #38343a;border-radius:9px;
  font-size:12px;color:#d8d4cc;padding:7px 16px;opacity:0;
  transition:opacity .18s;pointer-events:none;white-space:nowrap;z-index:99}
.toast.show{opacity:1}
</style>
</head>
<body>

<div class="hdr">
  <span class="mascot">&#x2590;&#x259B;&#x2588;&#x2588;&#x2588;&#x259C;&#x258C;<br>&#x259C;&#x2588;&#x2588;&#x2588;&#x2588;&#x2588;&#x259B;<br>&#x2598;&#x2598;&nbsp;&#x259D;&#x259D;</span>
  <div class="sitename">CLAWD &middot; MOCHI &middot; CONTROLLER</div>
</div>

<div class="busy" id="busy"><div class="busy-i"></div></div>

<div class="sec">// controls</div>
<div class="ctrl">
  <button class="cbtn on" id="blBtn" onclick="toggleBL()">&#9728; display on</button>
  <button class="cbtn on" id="bzBtn" onclick="toggleBZ()">&#9835; sound on</button>
</div>

<div class="sec">// views</div>
<div class="vgrid">
  <button class="vbtn active" data-v="0" onclick="setView(0)">
    <span class="ic">&#9632; &#9632;</span>
    <span class="nm">Normal eyes</span>
    <span class="ht">wiggle + blink</span>
  </button>
  <button class="vbtn" data-v="1" onclick="setView(1)">
    <span class="ic">&gt; &lt;</span>
    <span class="nm">Squish eyes</span>
    <span class="ht">open / close</span>
  </button>
  <button class="vbtn" data-v="2" onclick="setView(2)">
    <span class="ic">{ }</span>
    <span class="nm">Claude Code</span>
    <span class="ht">opens terminal</span>
  </button>
  <button class="vbtn" data-v="3" onclick="toggleCanvas()">
    <span class="ic">&#11035;</span>
    <span class="nm">Canvas</span>
    <span class="ht">draw on display</span>
  </button>
</div>

<div class="sec">// speed</div>
<div class="speed-row">
  <span class="sl">slow</span>
  <input type="range" id="spd" min="1" max="3" value="1" step="1" oninput="setSpeed(this.value)">
  <span class="sv" id="spdV">slow</span>
</div>

<div class="ctrl">
  <div class="ci" style="flex:1;display:flex;flex-direction:column;gap:4px;align-items:stretch">
    <span class="cl" style="font-size:10px;color:#8a8278;letter-spacing:1px;font-weight:bold;text-align:center">BACKGROUND</span>
    <input type="color" class="cs" id="bgCol" value="#aa4818" oninput="onBgChange(this.value)">
  </div>
  <div class="ci" style="flex:1;display:flex;flex-direction:column;gap:4px;align-items:stretch">
    <span class="cl" style="font-size:10px;color:#8a8278;letter-spacing:1px;font-weight:bold;text-align:center">PEN COLOR</span>
    <input type="color" class="cs" id="penCol" value="#000000">
  </div>
</div>

<div class="sec">// terminal</div>
<div class="twrap" id="twrap">
  <div class="thdr">
    <span class="tttl">&#9658; clawd:~$</span>
    <button class="tx" onclick="closeTerm()">&#x2715; exit terminal</button>
  </div>
  <div class="trow">
    <input class="tin" id="tin" type="text" placeholder="type here..."
           autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">
    <button class="tgo" onclick="termEnter()">&#8629;</button>
  </div>
</div>

<div class="cwrap" id="cwrap">
  <div class="dacts">
    <button class="db hi" onclick="clearAll()">&#11035; clear</button>
    <button class="db" style="border-color:#28b878;color:#28b878" onclick="toggleCanvas()">&#10003; done</button>
  </div>
  <canvas id="cvs" width="240" height="240"></canvas>
</div>

<div class="toast" id="toast"></div>

<script>
let activeView  = 0;
let termOpen    = false;
let canvasOpen  = false;
let blOn        = true;
let bzOn        = true;
let isBusy      = false;
let drawing     = false;
let lastX = 0, lastY = 0;
let tt;

const spdLabels = ['','slow','normal','fast'];

// ── Toast ──────────────────────────────────────────────────────
function toast(msg, ok=true) {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.style.borderColor = ok ? '#28b878' : '#c96a3e';
  el.classList.add('show');
  clearTimeout(tt);
  tt = setTimeout(() => el.classList.remove('show'), 1300);
}

// ── Busy ────────────────────────────────────────────────────────
function setBusy(b) {
  isBusy = b;
  document.getElementById('busy').classList.toggle('show', b);
  const locked = b || termOpen;
  document.querySelectorAll('.vbtn').forEach(el => {
    // when canvas open, keep canvas btn (data-v=3) active so user can exit
    el.disabled = canvasOpen ? parseInt(el.dataset.v) !== 3 : locked;
  });
  document.querySelectorAll('.lbtn').forEach(el => el.disabled = locked || canvasOpen);
  document.querySelectorAll('.cbtn').forEach(el => {
    if (el.id !== 'blBtn' && el.id !== 'bzBtn') el.disabled = locked;
  });
}

// ── HTTP ────────────────────────────────────────────────────────
async function req(path) {
  try { const r = await fetch(path); return r.ok; }
  catch(e) { toast('no connection', false); return false; }
}

async function waitNotBusy() {
  for (let i = 0; i < 100; i++) {
    try {
      const r = await fetch('/state');
      const j = await r.json();
      if (!j.busy) return;
    } catch(e) {}
    await new Promise(r => setTimeout(r, 150));
  }
}

// ── Background colour ───────────────────────────────────────────
async function onBgChange(hex) {
  if (canvasOpen) {
    await req('/draw/clear?bg=' + encodeURIComponent(hex));
  } else {
    await req('/redraw?bg=' + encodeURIComponent(hex));
  }
  redrawCanvas(hex);
}

// ── Speed ───────────────────────────────────────────────────────
async function setSpeed(v) {
  document.getElementById('spdV').textContent = spdLabels[v];
  await req('/speed?v=' + v);
}

// ── Views ───────────────────────────────────────────────────────
async function setView(v) {
  if (isBusy || termOpen || canvasOpen) return;
  if (v === 3) { toggleCanvas(); return; }  // canvas button in grid
  const keys = ['w','s','d'];
  if (!await req('/cmd?k=' + keys[v])) return;
  activeView = v;
  document.querySelectorAll('.vbtn').forEach(b =>
    b.classList.toggle('active', parseInt(b.dataset.v) === v));
  if (v === 2) {
    termOpen = true;
    document.getElementById('twrap').classList.add('open');
    setBusy(false);   // re-run to apply termOpen lock
    setBusy(false);
    document.querySelectorAll('.vbtn,.lbtn').forEach(b => b.disabled = true);
    const cvb = document.getElementById('cvBtn'); if (cvb) cvb.disabled = true;
    document.getElementById('tin').focus();
    toast('terminal open');
    return;
  }
  setBusy(true);
  await waitNotBusy();
  setBusy(false);
}

// ── Logo animations (kept for startup, not exposed in UI) ──────

// ── Backlight ───────────────────────────────────────────────────
async function toggleBL() {
  blOn = !blOn;
  await req('/backlight?on=' + (blOn ? 1 : 0));
  const b = document.getElementById('blBtn');
  b.textContent = blOn ? '\u2600 display on' : '\u25cb display off';
  b.classList.toggle('on', blOn);
  b.classList.toggle('dim', !blOn);
}

// \u2500\u2500 Buzzer \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
async function toggleBZ() {
  bzOn = !bzOn;
  await req('/buzzer?on=' + (bzOn ? 1 : 0));
  const b = document.getElementById('bzBtn');
  b.textContent = bzOn ? '\u266b sound on' : '\u266c sound off';
  b.classList.toggle('on', bzOn);
  b.classList.toggle('dim', !bzOn);
}

// ── Canvas toggle ───────────────────────────────────────────────
async function toggleCanvas() {
  canvasOpen = !canvasOpen;
  document.getElementById('cwrap').classList.toggle('open', canvasOpen);
  const b = document.getElementById('cvBtn');
  if (b) { b.classList.toggle('on', canvasOpen); b.textContent = canvasOpen ? '\u2b1b canvas on' : '\u2b1b canvas'; }
  // highlight the canvas vbtn (data-v=3) in the grid
  document.querySelectorAll('.vbtn').forEach(btn =>
    btn.classList.toggle('active', canvasOpen && parseInt(btn.dataset.v) === 3));
  await req('/canvas?on=' + (canvasOpen ? 1 : 0));
  if (canvasOpen) {
    const bg = document.getElementById('bgCol').value;
    redrawCanvas(bg);
    await req('/draw/clear?bg=' + encodeURIComponent(bg));
    // lock all other buttons
    document.querySelectorAll('.vbtn,.lbtn').forEach(b => b.disabled = true);
    toast('canvas active');
  } else {
    setBusy(false);   // re-evaluate locks
    toast('canvas off');
  }
}

// ── Terminal ────────────────────────────────────────────────────
const tin = document.getElementById('tin');
let lastVal = '';
tin.addEventListener('input', async () => {
  const cur = tin.value, prev = lastVal;
  if (cur.length > prev.length) {
    await req('/char?c=' + encodeURIComponent(cur[cur.length - 1]));
  } else if (cur.length < prev.length) {
    await req('/char?c=%08');
  }
  lastVal = cur;
});
async function termEnter() {
  await req('/char?c=%0A');
  tin.value = ''; lastVal = ''; tin.focus();
}
tin.addEventListener('keydown', e => {
  if (e.key === 'Enter') { e.preventDefault(); termEnter(); }
});
async function closeTerm() {
  await req('/cmd?k=q');
  termOpen = false;
  document.getElementById('twrap').classList.remove('open');
  setBusy(false);
  toast('terminal closed');
}

// ── Canvas drawing — send full stroke on finger lift ────────────
const cvs = document.getElementById('cvs');
const ctx = cvs.getContext('2d');
let strokePts = [];

function getPos(e) {
  const r = cvs.getBoundingClientRect();
  const sx = cvs.width / r.width, sy = cvs.height / r.height;
  const s = e.touches ? e.touches[0] : e;
  return { x: (s.clientX - r.left) * sx, y: (s.clientY - r.top) * sy };
}

function redrawCanvas(hex) {
  ctx.fillStyle = hex;
  ctx.fillRect(0, 0, cvs.width, cvs.height);
}

function startDraw(e) {
  e.preventDefault();
  drawing = true;
  strokePts = [];
  const p = getPos(e); lastX = p.x; lastY = p.y;
  strokePts.push({ x: Math.round(p.x), y: Math.round(p.y) });
  // draw dot on canvas preview only — no display send yet
  ctx.beginPath(); ctx.arc(p.x, p.y, 2, 0, Math.PI * 2);
  ctx.fillStyle = document.getElementById('penCol').value; ctx.fill();
}
function moveDraw(e) {
  if (!drawing) return; e.preventDefault();
  const p = getPos(e);
  ctx.beginPath(); ctx.moveTo(lastX, lastY); ctx.lineTo(p.x, p.y);
  ctx.strokeStyle = document.getElementById('penCol').value;
  ctx.lineWidth = 4; ctx.lineCap = 'round'; ctx.stroke();
  strokePts.push({ x: Math.round(p.x), y: Math.round(p.y) });
  lastX = p.x; lastY = p.y;
}
async function endDraw(e) {
  if (!drawing) return; drawing = false;
  if (!canvasOpen || strokePts.length < 1) return;
  const pen = document.getElementById('penCol').value.replace('#', '');
  const pts = strokePts.map(p => p.x + ',' + p.y).join(';');
  await req('/draw/stroke?pen=' + pen + '&pts=' + encodeURIComponent(pts));
  strokePts = [];
}

cvs.addEventListener('mousedown',  startDraw);
cvs.addEventListener('mousemove',  moveDraw);
cvs.addEventListener('mouseup',    endDraw);
cvs.addEventListener('mouseleave', endDraw);
cvs.addEventListener('touchstart', startDraw, {passive:false});
cvs.addEventListener('touchmove',  moveDraw,  {passive:false});
cvs.addEventListener('touchend',   endDraw);

// Clear = clear both web canvas and display
async function clearAll() {
  const bg = document.getElementById('bgCol').value;
  redrawCanvas(bg);
  await req('/draw/clear?bg=' + encodeURIComponent(bg));
  toast('cleared');
}

// Init: sync speed and backlight from ESP32, reset bg to default
(async () => {
  try {
    const r = await fetch('/state');
    const j = await r.json();
    // Sync speed
    const spd = j.speed || 1;
    document.getElementById('spd').value = spd;
    document.getElementById('spdV').textContent = spdLabels[spd];
    // Sync backlight
    if (j.bl === false) {
      blOn = false;
      const b = document.getElementById('blBtn');
      b.textContent = '\u25cb display off';
      b.classList.remove('on'); b.classList.add('dim');
    }
    // Sync buzzer
    if (j.buzzer === false) {
      bzOn = false;
      const b = document.getElementById('bzBtn');
      b.textContent = '\u266c sound off';
      b.classList.remove('on'); b.classList.add('dim');
    }
  } catch(e) {}
  // Always reset bg picker to default orange on page load
  document.getElementById('bgCol').value = '#aa4818';
  redrawCanvas('#aa4818');
})();
</script>
</body>
</html>
)rawhtml";

// ═════════════════════════════════════════════════════════════
//  SETUP PAGE (served at "/" when in AP provisioning mode)
// ═════════════════════════════════════════════════════════════
const char SETUP_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clawd Mochi Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{background:#1c1c20;color:#e8e4dc;font-family:'Courier New',monospace;
  padding:24px 16px;max-width:420px;margin:0 auto;min-height:100vh}
h1{color:#c96a3e;font-size:18px;letter-spacing:2px;text-align:center;margin-bottom:6px}
.sub{color:#8a8278;font-size:11px;text-align:center;letter-spacing:2px;margin-bottom:24px}
label{display:block;color:#8a8278;font-size:11px;letter-spacing:2px;
  font-weight:bold;margin:18px 0 6px;text-transform:uppercase}
input{width:100%;padding:12px;background:#252428;border:1.5px solid #38343a;
  border-radius:10px;color:#e8e4dc;font:14px 'Courier New',monospace;outline:none}
input:focus{border-color:#c96a3e}
button{margin-top:28px;width:100%;padding:14px;background:#c96a3e;border:none;
  border-radius:10px;color:#fff;font:bold 13px 'Courier New',monospace;
  cursor:pointer;letter-spacing:2px}
button:active{transform:scale(.97)}
.hint{color:#5a5048;font-size:10px;margin-top:24px;line-height:1.6;letter-spacing:1px}
.ok{color:#28b878;font-size:13px;margin-top:18px;text-align:center}
</style>
</head>
<body>
<h1>&#x1F980; CLAWD MOCHI</h1>
<div class="sub">FIRST-TIME SETUP</div>
<form id=f action="/provision" method="POST">
  <label>WiFi network (SSID)</label>
  <input name=ssid required autocapitalize=off autocorrect=off spellcheck=false>
  <label>WiFi password</label>
  <input name=pass type=password autocapitalize=off autocorrect=off spellcheck=false>
  <button type=submit>SAVE &amp; REBOOT</button>
</form>
<div class="hint">
  After save, Mochi will reboot and try to connect to your home network.
  The new IP address will appear on Mochi's display.
  Open that address in your browser to use the controller.
</div>
<script>
document.getElementById('f').addEventListener('submit', e => {
  e.preventDefault();
  const fd = new FormData(e.target);
  fetch('/provision', { method:'POST', body: new URLSearchParams(fd) })
    .then(() => {
      document.body.innerHTML = '<h1>&#x1F980; CLAWD MOCHI</h1>'
        + '<div class="ok">Saved! Rebooting Mochi...</div>'
        + '<div class="hint" style="text-align:center;margin-top:24px">'
        + 'Watch the display for the new IP address.</div>';
    });
});
</script>
</body>
</html>
)rawhtml";

// ═════════════════════════════════════════════════════════════
//  DISPLAY HELPERS (WiFi info screens)
// ═════════════════════════════════════════════════════════════

void showApInfoScreen() {
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_WHITE);  tft.setTextSize(2);
  tft.setCursor(12, 16);  tft.print("SETUP MODE");
  tft.setTextColor(C_MUTED);  tft.setTextSize(1);
  tft.setCursor(12, 44);  tft.print("1. join WiFi:");
  tft.setTextColor(C_ORANGE); tft.setTextSize(2);
  tft.setCursor(12, 58);  tft.print(AP_SSID);
  tft.setTextColor(C_MUTED);  tft.setTextSize(1);
  tft.setCursor(12, 84);  tft.print("password: "); tft.print(AP_PASS);
  tft.setCursor(12, 104); tft.print("2. browse to:");
  tft.setTextColor(C_ORANGE); tft.setTextSize(2);
  tft.setCursor(12, 118); tft.print("192.168.4.1");
  tft.setTextColor(C_MUTED);  tft.setTextSize(1);
  tft.setCursor(12, 144); tft.print("3. enter home WiFi");
}

void showConnectingScreen(const String& ssid) {
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_WHITE);  tft.setTextSize(2);
  tft.setCursor(12, 16);  tft.print("Connecting...");
  tft.setTextColor(C_MUTED);  tft.setTextSize(1);
  tft.setCursor(12, 50);  tft.print("ssid: "); tft.print(ssid);
}

void showStaInfoScreen(IPAddress ip) {
  staIpStr = ip.toString();
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0, DISP_W, 4, C_GREEN);
  tft.setTextColor(C_WHITE);  tft.setTextSize(2);
  tft.setCursor(12, 16);  tft.print("Online!");
  tft.setTextColor(C_MUTED);  tft.setTextSize(1);
  tft.setCursor(12, 50);  tft.print("Open browser:");
  tft.setTextColor(C_ORANGE); tft.setTextSize(2);
  tft.setCursor(12, 66);  tft.print(staIpStr);
  tft.setTextColor(C_MUTED);  tft.setTextSize(1);
  tft.setCursor(12, 100); tft.print("ssid: "); tft.print(staSsid);
  tft.setCursor(12, 120); tft.print("waiting for session...");
}

// ═════════════════════════════════════════════════════════════
//  WEB ROUTES
// ═════════════════════════════════════════════════════════════

void routeRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  if (apMode) server.send_P(200, "text/html", SETUP_HTML);
  else        server.send_P(200, "text/html", INDEX_HTML);
}

void routeSetup() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send_P(200, "text/html", SETUP_HTML);
}

void routeProvision() {
  if (!server.hasArg("ssid") || server.arg("ssid").isEmpty()) {
    server.send(400, "text/plain", "missing ssid"); return;
  }
  cfgSave(server.arg("ssid"), server.arg("pass"));
  server.send(200, "application/json", "{\"ok\":1}");
  delay(800);
  ESP.restart();
}

void routeFactoryReset() {
  cfgClear();
  server.send(200, "application/json", "{\"ok\":1}");
  delay(500);
  ESP.restart();
}

// Map event type strings (from Mac bridge) → SessionState
void routeEvent() {
  const String t    = server.arg("type");
  const String meta = server.arg("meta");
  if      (t == "prompt")     setSessionState(SS_THINKING,   meta);
  else if (t == "tool_pre")   setSessionState(SS_WORKING,    meta);
  else if (t == "tool_post")  setSessionState(SS_THINKING,   meta);
  else if (t == "permission") setSessionState(SS_PERMISSION, meta);
  else if (t == "stop")       setSessionState(SS_DONE,       meta);
  else if (t == "error")      setSessionState(SS_ERROR,      meta);
  else if (t == "idle")       setSessionState(SS_IDLE,       meta);
  else { server.send(400, "application/json", "{\"e\":\"unknown type\"}"); return; }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeAuto() {
  if (server.hasArg("on")) autoMode = (server.arg("on") == "1");
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeCmd() {
  if (!server.hasArg("k") || server.arg("k").isEmpty()) {
    server.send(400, "application/json", "{\"e\":1}"); return;
  }
  const char c = server.arg("k")[0];

  if (termMode) {
    if (c == 'q') { termMode = false; drawCodeView(); beep(BEEP_VIEW); }
    server.send(200, "application/json", "{\"ok\":1}"); return;
  }

  server.send(200, "application/json", "{\"ok\":1}");
  beep(BEEP_VIEW);
  switch (c) {
    // Play the blocking demo animation, then settle into the matching
    // ambient session state so the tick keeps the mood going.
    case 'w':
      currentView = VIEW_EYES_NORMAL;
      animNormalEyes();
      setSessionState(SS_IDLE);
      break;
    case 's':
      currentView = VIEW_EYES_SQUISH;
      animSquishEyes();
      setSessionState(SS_THINKING);
      break;
    case 'd':
      currentView = VIEW_CODE; drawCodeView();
      termMode = true; termClear(); termFullRedraw();
      break;
    case 'a':
      // One-shot demo — does not change sessionState.
      animLogoReveal();
      redrawCurrentView();
      break;
  }
}

void routeChar() {
  if (!termMode) { server.send(200, "application/json", "{\"ok\":1}"); return; }
  const String val = server.arg("c");
  if (val.length() > 0) { termAddChar(val[0]); beep(BEEP_TYPE); }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeSpeed() {
  if (server.hasArg("v")) animSpeed = constrain(server.arg("v").toInt(), 1, 3);
  server.send(200, "application/json", "{\"ok\":1}");
}

// /redraw?bg=hex — set animBg and immediately redraw current view
void routeRedraw() {
  if (server.hasArg("bg")) {
    animBgColor = hexToRgb565(server.arg("bg"));
    drawBgColor = animBgColor;
  }
  redrawCurrentView();
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeCanvas() {
  const bool on = server.hasArg("on") && server.arg("on") == "1";
  if (on) { currentView = VIEW_DRAW; tft.fillScreen(drawBgColor); }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeDrawClear() {
  const String bg = server.hasArg("bg") ? server.arg("bg") : "#aa4818";
  drawBgColor = hexToRgb565(bg);
  animBgColor = drawBgColor;  // keep in sync
  currentView = VIEW_DRAW; termMode = false;
  tft.fillScreen(drawBgColor);
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeDrawStroke() {
  if (!server.hasArg("pts") || !server.hasArg("pen")) {
    server.send(200, "application/json", "{\"ok\":1}"); return;
  }
  const uint16_t color = hexToRgb565(server.arg("pen"));
  const String   data  = server.arg("pts");
  currentView = VIEW_DRAW;

  struct Pt { int16_t x, y; };
  Pt prev = {-1, -1};
  int start = 0;
  while (start < (int)data.length()) {
    int semi = data.indexOf(';', start);
    if (semi == -1) semi = data.length();
    String entry = data.substring(start, semi);
    const int comma = entry.indexOf(',');
    if (comma > 0) {
      const int16_t x = entry.substring(0, comma).toInt();
      const int16_t y = entry.substring(comma + 1).toInt();
      if (prev.x >= 0) {
        tft.drawLine(prev.x, prev.y, x, y, color);
        tft.drawLine(prev.x + 1, prev.y, x + 1, y, color);
        tft.drawLine(prev.x, prev.y + 1, x, y + 1, color);
      } else {
        tft.fillCircle(x, y, 2, color);
      }
      prev = {x, y};
    }
    start = semi + 1;
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeBacklight() {
  setBacklight(server.hasArg("on") && server.arg("on") == "1");
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeBuzzer() {
  buzzerMuted = !(server.hasArg("on") && server.arg("on") == "1");
  if (!buzzerMuted) beep(BEEP_VIEW);   // audible confirmation on unmute
  server.send(200, "application/json", "{\"ok\":1}");
}

// Convert RGB565 back to #RRGGBB for state endpoint
String rgb565ToHex(uint16_t c) {
  uint8_t r = ((c >> 11) & 0x1F) << 3;
  uint8_t g = ((c >> 5)  & 0x3F) << 2;
  uint8_t b = (c & 0x1F) << 3;
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
  return String(buf);
}

void routeState() {
  String j = "{\"view\":"; j += currentView;
  j += ",\"busy\":";   j += busy        ? "true" : "false";
  j += ",\"term\":";   j += termMode    ? "true" : "false";
  j += ",\"bl\":";     j += backlightOn ? "true" : "false";
  j += ",\"buzzer\":"; j += buzzerMuted ? "false" : "true";
  j += ",\"speed\":";  j += animSpeed;
  j += ",\"ap\":";     j += apMode      ? "true" : "false";
  j += ",\"ip\":\"";   j += staIpStr;   j += "\"";
  j += ",\"ssid\":\""; j += staSsid;    j += "\"";
  j += ",\"sess\":";   j += (int)sessionState;
  j += ",\"meta\":\""; j += sessionMeta; j += "\"";
  j += ",\"auto\":";   j += autoMode    ? "true" : "false";
  j += ",\"touch\":\"";    j += touchLastEvent;       j += "\"";
  j += ",\"touchTs\":";    j += touchLastEventMs;
  j += ",\"mpu\":";        j += mpuPresent ? "true" : "false";
  j += ",\"gesture\":\""; j += lastGesture;          j += "\"";
  j += "}";
  server.send(200, "application/json", j);
}

void routeNotFound() { server.send(404, "text/plain", "not found"); }

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BLK, OUTPUT);
  setBacklight(true);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BOOT_PIN, INPUT_PULLUP);

  // I2C for MPU6050 (free because USB CDC On Boot moves Serial off GPIO 20/21)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  mpuPresent = mpu.begin(0x68, &Wire);
  if (mpuPresent) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  SPI.begin(8, -1, 10, TFT_CS);   // SCK=8, MOSI=10
  tft.init(240, 240);
  tft.setSPISpeed(40000000);
  tft.setRotation(1);
  initColours();

  // ── Boot splash ────────────────────────────────────────────
  tft.fillScreen(animBgColor);
  tft.setTextColor(C_WHITE); tft.setTextSize(3);
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 - 22); tft.print("Clawd");
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 + 14); tft.print("Mochi");
  delay(1200);

  // ── Logo shown once at startup ─────────────────────────────
  animLogoReveal();
  beep(BEEP_BOOT);

  // ── Start WiFi: STA-first, AP fallback ─────────────────────
  cfgLoad();

  if (staSsid.length() == 0) {
    // No saved network — provisioning AP
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    showApInfoScreen();
  } else {
    // Try to connect to saved network
    apMode = false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(staSsid.c_str(), staPass.c_str());
    showConnectingScreen(staSsid);

    unsigned long start = millis();
    uint8_t dots = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(250);
      // animate progress dots
      tft.setTextColor(C_ORANGE); tft.setTextSize(2);
      tft.setCursor(12 + dots * 12, 86);
      tft.print(".");
      dots = (dots + 1) % 12;
      if (dots == 0) {
        tft.fillRect(12, 80, DISP_W - 24, 22, C_DARKBG);
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      showStaInfoScreen(WiFi.localIP());
      beep(BEEP_DONE);
    } else {
      // STA failed → fall back to AP for re-provisioning
      apMode = true;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_AP);
      WiFi.softAP(AP_SSID, AP_PASS);
      showApInfoScreen();
      beep(BEEP_ERROR);
    }
  }

  // ── Register routes ────────────────────────────────────────
  server.on("/",            HTTP_GET, routeRoot);
  server.on("/cmd",         HTTP_GET, routeCmd);
  server.on("/char",        HTTP_GET, routeChar);
  server.on("/speed",       HTTP_GET, routeSpeed);
  server.on("/redraw",      HTTP_GET, routeRedraw);
  server.on("/canvas",      HTTP_GET, routeCanvas);
  server.on("/draw/clear",  HTTP_GET, routeDrawClear);
  server.on("/draw/stroke", HTTP_GET, routeDrawStroke);
  server.on("/backlight",   HTTP_GET, routeBacklight);
  server.on("/buzzer",      HTTP_GET, routeBuzzer);
  server.on("/setup",       HTTP_GET, routeSetup);
  server.on("/provision",   HTTP_POST, routeProvision);
  server.on("/factoryreset", HTTP_POST, routeFactoryReset);
  server.on("/event",       HTTP_GET, routeEvent);
  server.on("/event",       HTTP_POST, routeEvent);
  server.on("/auto",        HTTP_GET, routeAuto);
  server.on("/state",       HTTP_GET, routeState);
  server.onNotFound(routeNotFound);
  server.begin();

  // WiFi info stays on screen — first button press triggers setView/cmd
  // which will replace it with the correct view
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════

void loop() {
  server.handleClient();
  tickTouch();
  tickBoot();
  tickMpu();
  tickSessionAnim();
}
