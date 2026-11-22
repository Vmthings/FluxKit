// M5Stack boards (Tab5 etc.) via M5Unified / M5GFX.
#include "../Backend.h"
#if defined(FK_IS_M5)
#include <M5Unified.h>
#include <cstdarg>
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "../Platform.h"

namespace fk {
namespace platform {

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
void log(const char* fmt, ...) {
  char buf[192];
  va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
  printf("[FluxKit] %s\n", buf);
}
void init(int rotation) { if (rotation >= 0) M5.Display.setRotation(rotation); }
int width() { return M5.Display.width(); }
int height() { return M5.Display.height(); }
void present(int x, int y, int w, int h, const uint16_t* px) {
  M5.Display.startWrite();
#ifdef FK_SWAP565
  M5.Display.pushImage(x, y, w, h, (const lgfx::swap565_t*)px);
#else
  M5.Display.pushImage(x, y, w, h, (const lgfx::rgb565_t*)px);
#endif
  M5.Display.endWrite();
}
bool touch(int& x, int& y) {
  auto t = M5.Touch.getDetail(0);
  if (t.isPressed()) { x = t.x; y = t.y; return true; }
  return false;
}
void pump() { M5.update(); }
void powerOff() {
  M5.Display.setBrightness(0);
  M5.Power.powerOff();     // cuts power on boards that support it
  esp_deep_sleep_start();  // fallback: lowest-power state we can reach
}

}  // namespace platform
}  // namespace fk
#endif
