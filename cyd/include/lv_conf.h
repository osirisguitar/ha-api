#ifndef LV_CONF_H
#define LV_CONF_H

// Minimal LVGL 9 overrides on top of the built-in defaults (lv_conf_internal.h)

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_MEM_SIZE (32 * 1024U)

#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1

#endif
