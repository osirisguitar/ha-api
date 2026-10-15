#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ST77922.h>
#include <ST77922_Touch.h>
#include <lvgl.h>
#include "secrets.h"

// ---------------------------------------------------------------------------
// Config — control panel for the five ha-api switches (Home Assistant backend)
// Freenove FNK0104N: ESP32-S3, 3.5" ST77922 QSPI 320x480 IPS, capacitive touch
// ---------------------------------------------------------------------------
constexpr unsigned long REFRESH_MS = 10000;

const char* SWITCH_NAMES[5] = {"alfa", "beta", "gamma", "sovrum", "utomhus"};
constexpr int SWITCH_COUNT = 5;

// Neon theme: everything sits on the dark blue background, buttons are only
// outlined in bright cyan/teal with a soft glow — no lavender anywhere
const lv_color_t COL_BG     = lv_color_hex(0x0a1a2f);
const lv_color_t COL_TEXT   = lv_color_hex(0xdff6ff);
const lv_color_t COL_MUTED  = lv_color_hex(0x6f9db5);
const lv_color_t COL_ACCENT = lv_color_hex(0x00e0ff);  // bright cyan outline
const lv_color_t COL_GLOW   = lv_color_hex(0x00bcd4);  // teal glow
const lv_color_t COL_OFF    = lv_color_hex(0x1c4a5a);  // dim teal (idle outline)
const lv_color_t COL_ERR    = lv_color_hex(0xff6b6b);

struct SwitchInfo {
  bool on = false;
  bool known = false;
};
SwitchInfo switches[SWITCH_COUNT];

int pendingToggle = -1;  // index scheduled from the LVGL event handler
bool pendingOn = false;

struct SwitchUi {
  lv_obj_t* card;
  lv_obj_t* circle;
  lv_obj_t* symbol;
  lv_obj_t* stateLabel;
};
SwitchUi ui[SWITCH_COUNT];
lv_obj_t* statusLabel;
lv_obj_t* statusDot;

unsigned long lastRefresh = 0;
bool hasError = true;
String errorMessage = "Connecting...";

// ---------------------------------------------------------------------------
// Display + capacitive touch (both integrated in the ST77922 TDDI)
// ---------------------------------------------------------------------------
ST77922 tft;
ST77922_TOUCH touch;

// ---------------------------------------------------------------------------
// LVGL plumbing: full-frame render flushed over QSPI, capacitive touch pointer
// The ST77922 QSPI block-writer only renders full-screen single calls
// reliably on this board (small windows come out scrambled), so LVGL renders
// the whole frame into one buffer and every flush is one full-screen write.
// ---------------------------------------------------------------------------
constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 320;

// The QSPI block-writer takes any w/h; hardware needs no area rounding in LVGL 9

void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* pxMap) {
  const int w = area->x2 - area->x1 + 1;
  const int h = area->y2 - area->y1 + 1;
  // LVGL renders little-endian RGB565; Fill_Colors puts the buffer on the wire
  // as-is (MSB first), so exactly one swap is required here — the ST77922
  // driver does none of its own
  lv_draw_sw_rgb565_swap(pxMap, w * h);
  tft.Fill_Colors(area->x1, area->y1, w, h, (uint16_t*)pxMap);
  lv_display_flush_ready(disp);
}

void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
  // Capacitive, factory calibrated — no matrix, no pressure threshold
  if (touch.Get_Touch()) {
    bool found = false;
    for (int i = 0; i < touch.max_points && !found; i++) {
      if (touch.touch.x[i] > 0 || touch.touch.y[i] > 0) {
        data->point.x = touch.touch.x[i];
        data->point.y = touch.touch.y[i];
        found = true;
      }
      touch.touch.x[i] = 0;
      touch.touch.y[i] = 0;
    }
    data->state = found ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ---------------------------------------------------------------------------
// Layout copy of a Home Assistant sections dashboard: header row + tile grid
// (3 tiles per row, last two spanning wider below)
// ---------------------------------------------------------------------------
constexpr int HEADER_H = 36;
constexpr int TILE_GAP = 8;
constexpr int TILE_X0 = 8;
constexpr int TILE_Y0 = HEADER_H + TILE_GAP;
constexpr int TILE_H = 108;
constexpr int TILE_W3 = (SCREEN_W - 2 * TILE_X0 - 2 * TILE_GAP) / 3;   // top row
constexpr int TILE_W2 = (SCREEN_W - 2 * TILE_X0 - TILE_GAP) / 2;       // bottom row
constexpr int CIRCLE_D = 44;

void tilePos(int index, int& x, int& y, int& w) {
  if (index < 3) {
    x = TILE_X0 + index * (TILE_W3 + TILE_GAP);
    y = TILE_Y0;
    w = TILE_W3;
  } else {
    x = TILE_X0 + (index - 3) * (TILE_W2 + TILE_GAP);
    y = TILE_Y0 + TILE_H + TILE_GAP;
    w = TILE_W2;
  }
}

void updateTile(int index) {
  const bool known = switches[index].known;
  const bool on = known && switches[index].on;
  // Idle: bright outline + glow. Active: outline filled with the accent
  // itself so the on state reads instantly.
  lv_obj_set_style_border_color(ui[index].card, COL_ACCENT, 0);
  lv_obj_set_style_shadow_color(ui[index].card, COL_GLOW, 0);
  lv_obj_set_style_shadow_opa(ui[index].card, on ? LV_OPA_70 : LV_OPA_40, 0);

  if (on) {
    lv_obj_set_style_bg_color(ui[index].circle, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(ui[index].circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ui[index].circle, COL_ACCENT, 0);
    lv_obj_set_style_text_color(ui[index].symbol, COL_BG, 0);
  } else {
    lv_obj_set_style_bg_opa(ui[index].circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ui[index].circle, known ? COL_ACCENT : COL_OFF, 0);
    lv_obj_set_style_text_color(ui[index].symbol, known ? COL_ACCENT : COL_MUTED, 0);
  }
  lv_label_set_text(ui[index].stateLabel, known ? (on ? "På" : "Av") : "Unknown");
  lv_obj_set_style_text_color(ui[index].stateLabel, on ? COL_ACCENT : COL_MUTED, 0);
}

void tileClickedCb(lv_event_t* e) {
  const intptr_t index = (intptr_t)lv_event_get_user_data(e);
  if (pendingToggle >= 0) return;  // one request at a time
  pendingToggle = (int)index;
  pendingOn = !(switches[index].known && switches[index].on);
  // Optimistic update, like the HA frontend while the call is in flight
  switches[index].on = pendingOn;
  switches[index].known = true;
  updateTile(index);
}

void tile(int index) {
  int x, y, w;
  tilePos(index, x, y, w);

  lv_obj_t* card = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, TILE_H);
  // Button = background colour, outlined bright cyan/teal with a glow
  lv_obj_set_style_bg_color(card, COL_BG, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, COL_ACCENT, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_shadow_color(card, COL_GLOW, 0);
  lv_obj_set_style_shadow_width(card, 16, 0);
  lv_obj_set_style_shadow_spread(card, 1, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x11314a), LV_STATE_PRESSED);
  lv_obj_set_clickable(card, true);
  lv_obj_set_ext_click_area(card, 8);
  lv_obj_add_event_cb(card, tileClickedCb, LV_EVENT_CLICKED, (void*)(intptr_t)index);
  ui[index].card = card;

  lv_obj_t* circle = lv_obj_create(card);
  lv_obj_remove_style_all(circle);
  lv_obj_set_size(circle, CIRCLE_D, CIRCLE_D);
  lv_obj_set_pos(circle, 10, (TILE_H - CIRCLE_D) / 2);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(circle, COL_OFF, 0);
  lv_obj_set_style_border_width(circle, 2, 0);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);  // let taps fall through to the card
  ui[index].circle = circle;

  lv_obj_t* symbol = lv_label_create(circle);
  lv_label_set_text(symbol, LV_SYMBOL_POWER);
  lv_obj_set_style_text_font(symbol, &lv_font_sw_20, 0);
  lv_obj_center(symbol);
  ui[index].symbol = symbol;

  char name[24];
  name[0] = toupper(SWITCH_NAMES[index][0]);
  strncpy(name + 1, SWITCH_NAMES[index] + 1, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';

  lv_obj_t* label = lv_label_create(card);
  lv_label_set_text(label, name);
  lv_obj_set_style_text_color(label, COL_TEXT, 0);
  lv_obj_set_style_text_font(label, &lv_font_sw_20, 0);
  lv_obj_set_pos(label, CIRCLE_D + 16, 32);

  lv_obj_t* stateLabel = lv_label_create(card);
  lv_label_set_text(stateLabel, "Unknown");
  lv_obj_set_style_text_color(stateLabel, COL_MUTED, 0);
  lv_obj_set_style_text_font(stateLabel, &lv_font_sw_16, 0);
  lv_obj_set_pos(stateLabel, CIRCLE_D + 16, 61);
  ui[index].stateLabel = stateLabel;
}

void buildUi() {
  lv_obj_set_style_bg_color(lv_screen_active(), COL_BG, 0);
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

  lv_obj_t* title = lv_label_create(lv_screen_active());
  lv_label_set_text(title, "Rättargården");
  lv_obj_set_style_text_color(title, COL_TEXT, 0);
  lv_obj_set_style_text_font(title, &lv_font_sw_20, 0);
  lv_obj_set_pos(title, 12, 8);

  statusDot = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(statusDot);
  lv_obj_set_size(statusDot, 10, 10);
  lv_obj_set_pos(statusDot, SCREEN_W - 18, 14);
  lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(statusDot, COL_ERR, 0);
  lv_obj_set_style_bg_opa(statusDot, LV_OPA_COVER, 0);

  statusLabel = lv_label_create(lv_screen_active());
  lv_label_set_text(statusLabel, "Connecting...");
  lv_obj_set_style_text_color(statusLabel, COL_MUTED, 0);
  lv_obj_set_style_text_font(statusLabel, &lv_font_sw_14, 0);
  lv_obj_set_pos(statusLabel, 12, SCREEN_H - 24);

  for (int i = 0; i < SWITCH_COUNT; i++) {
    tile(i);
    updateTile(i);
  }
}

void setStatus(bool ok, const char* text) {
  hasError = !ok;
  errorMessage = text;
  lv_obj_set_style_bg_color(statusDot, ok ? COL_ACCENT : COL_ERR, 0);
  lv_label_set_text(statusLabel, text);
  lv_obj_set_style_text_color(statusLabel, ok ? COL_MUTED : COL_ERR, 0);
}

// ---------------------------------------------------------------------------
// ha-api calls
// ---------------------------------------------------------------------------
int fetchStates() {
  HTTPClient http;
  http.begin(String(API_BASE) + "/switches");
  http.setTimeout(5000);
  const int code = http.GET();
  if (code != 200) {
    http.end();
    return code == -1 ? -1 : -code;
  }
  const String body = http.getString();
  http.end();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) return -100;

  for (JsonVariantConst item : doc.as<JsonArrayConst>()) {
    const char* name = item["name"] | "";
    for (int i = 0; i < SWITCH_COUNT; i++) {
      if (strcmp(name, SWITCH_NAMES[i]) == 0) {
        switches[i].on = strcmp(item["state"] | "", "on") == 0;
        switches[i].known = true;
      }
    }
  }
  return 200;
}

int callSwitch(int index, bool on) {
  HTTPClient http;
  http.begin(String(API_BASE) + "/switch/" + SWITCH_NAMES[index] + (on ? "/on" : "/off"));
  http.setTimeout(8000);
  const int code = http.POST("");
  if (code != 200) {
    http.end();
    return code == -1 ? -1 : -code;
  }
  const String body = http.getString();
  http.end();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) return -100;
  switches[index].on = strcmp(doc["state"] | "", "on") == 0;
  switches[index].known = true;
  return 200;
}

void applyStates() {
  for (int i = 0; i < SWITCH_COUNT; i++) {
    updateTile(i);
  }
}

void refresh() {
  const int code = fetchStates();
  if (code == 200) {
    applyStates();
    setStatus(true, "ha-api connected");
  } else {
    char msg[40];
    snprintf(msg, sizeof(msg), "Error: %d", code);
    setStatus(false, msg);
  }
}

void processToggle() {
  if (pendingToggle < 0) return;
  const int index = pendingToggle;
  pendingToggle = -1;

  const int code = callSwitch(index, pendingOn);
  if (code == 200) {
    updateTile(index);
    setStatus(true, "ha-api connected");
  } else {
    // Revert to the last known server state
    if (fetchStates() == 200) applyStates();
    char msg[48];
    snprintf(msg, sizeof(msg), "%s: error %d", SWITCH_NAMES[index], code);
    setStatus(false, msg);
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  tft.Set_Rotation(1);  // landscape 480x320
  touch.init();         // TP_RESX reset + register settle, else touch stays dead
  touch.Set_Rotation(1); // match display: ly = 319 - rawX, lx = rawY
  delay(1000);          // touch controller init time per ST77922 protocol

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });

  lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  static uint8_t* frame = (uint8_t*)heap_caps_malloc(SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
  lv_display_set_buffers(disp, frame, NULL, SCREEN_W * SCREEN_H * 2, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(disp, flushCb);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchReadCb);

  buildUi();
  lv_timer_handler();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
  }
  lastRefresh = 0;
}

void loop() {
  lv_timer_handler();
  delay(5);

  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (!wifiUp) {
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 5000) {
      lastRetry = millis();
      if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
      setStatus(false, "WiFi disconnected");
    }
    return;
  }

  if (pendingToggle >= 0) {
    processToggle();
    lastRefresh = millis();
    return;
  }

  if (millis() - lastRefresh >= REFRESH_MS) {
    lastRefresh = millis();
    refresh();
  }
}
