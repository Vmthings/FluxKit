#include "Platform.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef FLUXKIT_HOST
// ---------------------------------------------------------------- host stub
namespace fk { namespace platform {
static std::vector<uint16_t> g_fb;
static int g_w = 720, g_h = 1280;
static uint32_t g_time = 0;
static bool g_down = false;
static int g_tx = 0, g_ty = 0;

void* alloc(size_t n) { return calloc(1, n ? n : 1); }
void release(void* p) { free(p); }
uint32_t millis() { return g_time; }
void log(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
void init(int) { g_fb.assign((size_t)g_w * g_h, 0); }
int width() { return g_w; }
int height() { return g_h; }
void present(int x, int y, int w, int h, const uint16_t* px) {
  if (g_fb.size() != (size_t)g_w * g_h) g_fb.assign((size_t)g_w * g_h, 0);
  for (int j = 0; j < h; j++) memcpy(&g_fb[(size_t)(y + j) * g_w + x], px + (size_t)j * w, (size_t)w * 2);
}
bool touch(int& x, int& y) { x = g_tx; y = g_ty; return g_down; }
void pump() {}
const std::vector<uint16_t>& hostFramebuffer() { return g_fb; }
void hostSetTime(uint32_t ms) { g_time = ms; }
void hostSetTouch(bool d, int x, int y) { g_down = d; g_tx = x; g_ty = y; }
void hostSetSize(int w, int h) { g_w = w; g_h = h; }
}}  // namespace

#else
// ------------------------------------------------------- M5Unified / M5GFX
#include <M5Unified.h>
#include "esp_heap_caps.h"

namespace fk { namespace platform {

void* alloc(size_t n) {
  if (!n) n = 1;
  void* p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(1, n, MALLOC_CAP_8BIT);
  return p;
}
void release(void* p) { heap_caps_free(p); }
uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
void log(const char* fmt, ...) {
  char buf[192];
  va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
  printf("[FluxKit] %s\n", buf);
}
void init(int rotation) {
  if (rotation >= 0) M5.Display.setRotation(rotation);
}
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

}}  // namespace
#endif
