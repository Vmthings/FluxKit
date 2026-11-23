// Host stub: renders into a RAM framebuffer (tests, CI, screenshots).
#include "../Backend.h"
#if defined(FK_IS_HOST)
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include "../Platform.h"

namespace fk {
namespace platform {
static std::vector<uint16_t> g_fb;
static int g_w = 720, g_h = 1280;
static uint32_t g_time = 0;
static bool g_down = false;
static int g_tx = 0, g_ty = 0;

static size_t g_used = 0;
static size_t limitBytes() {  // FLUXKIT_MEM_LIMIT_KB=N makes allocations fail beyond N KB (tests out-of-memory handling)
  static size_t lim = [] { const char* e = getenv("FLUXKIT_MEM_LIMIT_KB"); return e ? (size_t)atol(e) * 1024 : (size_t)0; }();
  return lim;
}
void* alloc(size_t n) {
  void* p = calloc(1, n ? n : 1);
  if (!p) return nullptr;
  size_t u = malloc_usable_size(p);
  if (limitBytes() && g_used + u > limitBytes()) { free(p); return nullptr; }
  g_used += u;
  return p;
}
void* allocAligned(size_t n, size_t a) {
  n = (n + a - 1) / a * a;
  void* p = aligned_alloc(a, n ? n : a);
  if (!p) return nullptr;
  size_t u = malloc_usable_size(p);
  if (limitBytes() && g_used + u > limitBytes()) { free(p); return nullptr; }
  memset(p, 0, n);
  g_used += u;
  return p;
}
void release(void* p) { if (p) { g_used -= malloc_usable_size(p); free(p); } }
uint32_t millis() { return g_time; }
uint64_t micros() { return (uint64_t)g_time * 1000ULL; }
void sleepMs(uint32_t) {}
void yield() {}
size_t freeMemory() { return 0; }
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
void powerOff() { log("power off requested"); }
const std::vector<uint16_t>& hostFramebuffer() { return g_fb; }
void hostSetTime(uint32_t ms) { g_time = ms; }
void hostSetTouch(bool d, int x, int y) { g_down = d; g_tx = x; g_ty = y; }
void hostSetSize(int w, int h) { g_w = w; g_h = h; }
}  // namespace platform
}  // namespace fk
#endif
