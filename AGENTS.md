# AGENTS.md

## Cheap Yellow Display (CYD) — reusable hardware guide

Reference projects: the working control panel in `cyd/` (LVGL 9 + TFT_eSPI) and
`~/projects/osiris/csstats/cyd` (TFT_eSPI only). Use these as the source of
truth; do not re-derive the settings below by reasoning, they were debugged on
real hardware.

### Hardware: Freenove FNK0114B (CYD clone, not an original CYD)

- 2.8" ST7789 240x320, ESP32-WROOM-32E (board `esp32dev`, **no PSRAM**)
- Display SPI (HSPI): TFT_MISO=12, TFT_MOSI=13, TFT_SCLK=14, TFT_CS=15, TFT_DC=2, TFT_RST=-1, TFT_BL=21 (active HIGH)
- Resistive touch XPT2046, bit-banged (touch-only pins, NOT the display SPI):
  T_CS=33, T_CLK=25, T_DIN=32, T_DOUT=39
- Always landscape 320x240 (`tft.setRotation(1)`, LVGL display 320x240)

### Color pipeline — the trap that costs hours

LVGL 9 hands the flush callback a little-endian RGB565 buffer, and
TFT_eSPI's `pushColors(data, len, /*swap=*/true)` byte-swaps the buffer itself
while pushing. Therefore:

- **Never call `lv_draw_sw_rgb565_swap()` in the flush callback.** Two swaps
  cancel out and the panel receives raw little-endian bytes. Symptoms:
  cyan renders lime-green, dark blue renders as a light washed-out tint,
  antialiasing edges look color-corrupted. If colors are wrong, suspect a
  swap-count problem before touching anything else (one swap is required;
  check which side is doing it).
- Correct flush body:

  ```cpp
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)px_map, w * h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
  ```

- `TFT_RGB_ORDER=TFT_BGR` is the proven MADCTL order for this panel (from the
  working csstats sketch). Only change it if the R and B channels are
  swapped while everything else is correct.

### Touch calibration

- Raw read cmd 0x90 = axis A (X), 0xD0 = axis B (Y); raw 0..4095, values
  near 0 or 4095 mean "no touch" (treat as released).
- Detect touch by pressure, not by a rail check on the axes: soft fingertip
  contacts often push an axis reading toward a rail, so a rail-gate makes
  finger taps feel like they need several tries while pen/nail taps "just
  work" (classic symptom). Read Z1 (cmd 0xB0) and Z2 (cmd 0xC0), compute
  `z = z1 + 4095 - z2` (rises with contact force, ~0 released) and require
  `z >= Z_THRESHOLD` (~250; 200-350 tunable) before trusting axis reads.
- Discard the first conversion after contact settles; keep the second.
- Extend click areas (`lv_obj_set_ext_click_area(obj, 8)`) so near-miss taps
  register — contributes as much to perceived responsiveness as polling,
  which is fixed at LVGL's display-refresh cadence (~30 ms) anyway.
- Freenove calibration for rotation 1: X raw range 527..3552, Y raw range
  683..3464, mapped with `map()` to 0..319 and 0..239, **no axis inversion**.
- Debugging trick: if taps in the middle row hit the correct button but taps
  in one edge row land on the button in the opposite edge (e.g. top-row
  taps trigger the bottom full-width button), one axis is mapped
  upside-down — flip its `map()` direction. If left/right columns are
  swapped, the X axis is the inverted one.

### Memory budget (no PSRAM — keep it small from the start)

- LVGL pool: `LV_MEM_SIZE (32 * 1024U)` is enough for a card/tile UI
- Render buffers: 2 x 320 x 20 lines (static, `LV_DISPLAY_RENDER_MODE_PARTIAL`)
- Full 320x240 buffers will overflow `dram0_0_seg` at link time; WiFi+HTTP
  stack already eats a large share of DRAM

### lv_conf.h plumbing

- Put a minimal `include/lv_conf.h` (only overrides, values not set fall
  back to defaults) and add to platformio.ini `build_flags`:

  ```
  -D LV_CONF_INCLUDE_SIMPLE
  -I include
  ```

  The `-I include` is required: without it the LVGL library compiles without
  seeing the project's include dir and silently uses defaults.
- The file's include guard must be `#ifndef LV_CONF_H / #define LV_CONF_H`
  (LVGL checks that macro; `#pragma once` alone triggers a confusing
  "Possible failure to include lv_conf.h" pragma warning).

### LVGL 9 API notes (as of lvgl ^9.x in PlatformIO)

- `lv_tick_set_cb()` wants `uint32_t(*)()` — wrap `millis`, don't pass it
  directly
- `lv_indev_create()` takes no args; call `lv_indev_set_type()` and
  `lv_indev_set_read_cb()` afterwards
- Montserrat fonts are off by default; enable the sizes used in lv_conf.h
- Newer releases deprecate `lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE)` in
  favor of `lv_obj_set_clickable(obj, true)`

### Wiring a new panel to this API stack

- Architectural pattern: CYD runs as a client of the HTTP wrapper (ha-api),
  never talks to Home Assistant directly. Config lives in
  `src/secrets.h` (gitignored; `secrets.example.h` is the template).
- Do blocking HTTP in `loop()` (with `lv_timer_handler()` called each pass),
  not inside LVGL event callbacks — schedule toggles via a pending-request
  index and process them in the loop.

### First color sanity check on new hardware

Render: dark blue background, cyan outline, white text. Dark looks light ->
inversion (`TFT_INVERSION_OFF`/`ON`). Hue shifted into greens/oranges with
text AA wrong -> swap-count problem. R/B swapped only -> RGB order flag.
