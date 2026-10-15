#ifndef LV_CONF_H
#define LV_CONF_H

// Minimal LVGL 9 overrides on top of the built-in defaults (lv_conf_internal.h)

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_MEM_SIZE (64 * 1024U)

// Custom fonts with Latin-1 supplement (Swedish å ä ö) + LVGL symbols
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(lv_font_sw_14) LV_FONT_DECLARE(lv_font_sw_16) LV_FONT_DECLARE(lv_font_sw_20)
#define LV_FONT_DEFAULT &lv_font_sw_14
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_28 0

#endif
