#ifndef LVGL_EINK_BRIDGE_H
#define LVGL_EINK_BRIDGE_H

#include <Arduino.h>
#include <heltec-eink-modules.h>

#if __has_include(<src/lvgl.h>)
#include <src/lvgl.h>
#elif __has_include(<lvgl/src/lvgl.h>)
#include <lvgl/src/lvgl.h>
#elif __has_include(<lvgl/lvgl.h>)
#include <lvgl/lvgl.h>
#else
#error "LVGL headers not found. Please ensure lib_deps has lvgl/lvgl@8.3.x."
#endif

class LvglEinkBridge;
extern LvglEinkBridge *g_lvgl_eink_bridge;

class LvglEinkBridge {
public:
  LvglEinkBridge(BaseDisplay &display, lv_color_t *buf1, uint32_t buf_pixel_count, lv_color_t *buf2 = nullptr)
      : display_(display), draw_buf_pixels_(buf_pixel_count), initialized_(false) {
    lv_disp_draw_buf_init(&draw_buf_, buf1, buf2, buf_pixel_count);
    lv_disp_drv_init(&disp_drv_);
    disp_drv_.flush_cb = &LvglEinkBridge::flushCallback;
    disp_drv_.draw_buf = &draw_buf_;
    disp_drv_.user_data = this;
  }

  lv_disp_t *begin(bool use_fastmode = true, bool clear_screen = true) {
    disp_drv_.hor_res = static_cast<lv_coord_t>(display_.width());
    disp_drv_.ver_res = static_cast<lv_coord_t>(display_.height());

    if (use_fastmode) {
      display_.fastmodeOn();
    } else {
      display_.fastmodeOff();
    }

    if (clear_screen) {
      display_.clear();
      display_.clearMemory();
      display_.update();
    }

    lv_disp_t *disp = lv_disp_drv_register(&disp_drv_);
    g_lvgl_eink_bridge = this;
    initialized_ = true;
    return disp;
  }

  bool isInitialized() const { return initialized_; }
  uint32_t drawBufferPixels() const { return draw_buf_pixels_; }

  void commitUpdate() {
    if (initialized_) {
      display_.update();
    }
  }

  void clearPanelMemory() {
    display_.clear();
    display_.clearMemory();
  }

private:
  static void flushCallback(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    LvglEinkBridge *self = static_cast<LvglEinkBridge *>(disp_drv->user_data);
    self->flush(area, color_p);
    lv_disp_flush_ready(disp_drv);
  }

  void flush(const lv_area_t *area, const lv_color_t *color_p) {
    int16_t x1 = area->x1 < 0 ? 0 : area->x1;
    int16_t y1 = area->y1 < 0 ? 0 : area->y1;
    int16_t x2 = area->x2 >= display_.width() ? static_cast<int16_t>(display_.width() - 1) : area->x2;
    int16_t y2 = area->y2 >= display_.height() ? static_cast<int16_t>(display_.height() - 1) : area->y2;

    if (x2 < x1 || y2 < y1) {
      return;
    }

    const lv_color_t *src = color_p;
    for (int16_t y = y1; y <= y2; ++y) {
      for (int16_t x = x1; x <= x2; ++x) {
        display_.drawPixel(x, y, toEinkColor(*src));
        ++src;
      }
    }
  }

  uint16_t toEinkColor(const lv_color_t &c) const {
#if LV_COLOR_DEPTH == 1
    return c.full ? BLACK : WHITE;
#elif LV_COLOR_DEPTH == 8
    return c.full > 127 ? BLACK : WHITE;
#elif LV_COLOR_DEPTH == 16
    uint32_t r = (c.ch.red * 255) / 31;
    uint32_t g = (c.ch.green * 255) / 63;
    uint32_t b = (c.ch.blue * 255) / 31;
    uint32_t luminance = r * 299 + g * 587 + b * 114;
    return (luminance < (128 * 1000)) ? BLACK : WHITE;
#elif LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
    uint8_t r = c.ch.red;
    uint8_t g = c.ch.green;
    uint8_t b = c.ch.blue;
    uint16_t luminance = static_cast<uint16_t>(r) * 299 + static_cast<uint16_t>(g) * 587 + static_cast<uint16_t>(b) * 114;
    return (luminance < (128 * 1000)) ? BLACK : WHITE;
#else
    return BLACK;
#endif
  }

  BaseDisplay &display_;
  lv_disp_draw_buf_t draw_buf_;
  lv_disp_drv_t disp_drv_;
  uint32_t draw_buf_pixels_;
  bool initialized_;
};

static inline void lvgl_eink_commit_update() {
  if (g_lvgl_eink_bridge) {
    g_lvgl_eink_bridge->commitUpdate();
  }
}

static inline void lvgl_eink_clear_panel() {
  if (g_lvgl_eink_bridge) {
    g_lvgl_eink_bridge->clearPanelMemory();
  }
}

#endif
