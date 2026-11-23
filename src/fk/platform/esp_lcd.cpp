// Any ESP32-P4 / ESP32-S3 board using ESP-IDF's esp_lcd (MIPI-DSI, RGB, SPI, I80 panels).
//
//   esp_lcd_panel_handle_t panel = ...;                 // you create it with your board's panel driver
//   fk::platform::useEspLcd(panel, 1024, 600);          // tell FluxKit about it
//   fk::platform::setTouchReader(my_touch_read);        // optional (GT911, FT5x06...): bool f(int* x, int* y)
//   fk::begin();
#include "../Backend.h"
#if defined(FK_IS_ESP_LCD)
#include <cstdarg>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../Platform.h"

namespace fk {
namespace platform {
static esp_lcd_panel_handle_t g_panel = nullptr;
static int g_w = 0, g_h = 0;
static bool (*g_touch)(int*, int*) = nullptr;
static void (*g_off)() = nullptr;

void useEspLcd(void* panel, int w, int h) { g_panel = (esp_lcd_panel_handle_t)panel; g_w = w; g_h = h; }
void setTouchReader(bool (*r)(int*, int*)) { g_touch = r; }
void setPowerOffHandler(void (*f)()) { g_off = f; }

void* alloc(size_t n) {
  if (!n) n = 1;
  void* p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(1, n, MALLOC_CAP_8BIT);
  return p;
}
void* allocAligned(size_t n, size_t a) {
  n = (n + a - 1) / a * a;
  void* p = heap_caps_aligned_calloc(a, 1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_aligned_calloc(a, 1, n, MALLOC_CAP_8BIT);
  return p;
}
void release(void* p) { heap_caps_free(p); }
uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
uint64_t micros() { return (uint64_t)esp_timer_get_time(); }
void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
void yield() {  // a long bake would otherwise starve the idle task and trip the task watchdog (a reboot)
  static uint64_t last = 0;
  uint64_t now = (uint64_t)esp_timer_get_time();
  if (now - last > 100000ULL) { vTaskDelay(1); last = (uint64_t)esp_timer_get_time(); }
}
size_t freeMemory() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
void log(const char* fmt, ...) {
  char buf[192];
  va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
  printf("[FluxKit] %s\n", buf);
}
void init(int) {
  if (!g_panel) log("esp_lcd backend: call fk::platform::useEspLcd(panel, w, h) before fk::begin()");
}
int width() { return g_w; }
int height() { return g_h; }
void present(int x, int y, int w, int h, const uint16_t* px) {
  if (!g_panel) return;
#ifdef FK_SWAP565  // panels that want big-endian RGB565
  static uint16_t* scratch = nullptr; static size_t cap = 0;
  size_t n = (size_t)w * h;
  if (n > cap) { release(scratch); scratch = (uint16_t*)alloc(n * 2); cap = scratch ? n : 0; }
  if (!scratch) return;
  for (size_t i = 0; i < n; i++) scratch[i] = (uint16_t)((px[i] << 8) | (px[i] >> 8));
  px = scratch;
#endif
  esp_lcd_panel_draw_bitmap(g_panel, x, y, x + w, y + h, px);
}
bool touch(int& x, int& y) {
  int tx = 0, ty = 0;
  if (g_touch && g_touch(&tx, &ty)) { x = tx; y = ty; return true; }
  return false;
}
void pump() {}
void powerOff() {
  if (g_off) { g_off(); return; }
  esp_deep_sleep_start();
}
}  // namespace platform
}  // namespace fk
#endif
