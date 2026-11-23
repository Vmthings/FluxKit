#include "Surface.h"
#include "Platform.h"

namespace fk {

static size_t g_live = 0, g_failed = 0;
size_t Surface::failedAllocs() { return g_failed; }

Surface::Surface(int w_, int h_) {
  if (w_ <= 0 || h_ <= 0) return;
  platform::yield();  // long bakes must not starve the idle task / watchdog
  data = (uint32_t*)platform::alloc((size_t)w_ * h_ * 4);
  if (!data) { if (g_failed++ < 3) platform::log("out of memory: could not allocate a %dx%d surface", w_, h_); return; }
  w = w_; h = h_;
  g_live += bytes();
}
Surface::~Surface() {
  if (data) { g_live -= bytes(); platform::release(data); }
}
size_t Surface::totalBytes() { return g_live; }

Surface565::Surface565(int w_, int h_) {
  if (w_ <= 0 || h_ <= 0) return;
  data = (uint16_t*)platform::alloc((size_t)w_ * h_ * 2);
  if (!data) { if (g_failed++ < 3) platform::log("out of memory: could not allocate a %dx%d surface", w_, h_); return; }
  w = w_; h = h_;
  g_live += (size_t)w * h * 2;
}
Surface565::~Surface565() {
  if (data) { g_live -= (size_t)w * h * 2; platform::release(data); }
}

bool Surface::isOpaque() const {
  for (size_t i = 0, n = (size_t)w * h; i < n; i++)
    if ((data[i] >> 24) != 255) return false;
  return w > 0 && h > 0;
}

SurfacePtr Surface::fromRGB565(const uint16_t* px, int w, int h) {
  auto s = std::make_shared<Surface>(w, h);
  for (size_t i = 0, n = (size_t)s->w * s->h; i < n; i++) {
    uint32_t p = px[i], r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
    s->data[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
  return s;
}

SurfacePtr Surface::fromARGB(const uint32_t* px, int w, int h, bool pre) {
  auto s = std::make_shared<Surface>(w, h);
  for (size_t i = 0, n = (size_t)s->w * s->h; i < n; i++) {
    uint32_t p = px[i];
    if (!pre) {
      uint32_t a = p >> 24;
      p = (a << 24) | ((((p >> 16) & 255) * a / 255) << 16) | ((((p >> 8) & 255) * a / 255) << 8) | ((p & 255) * a / 255);
    }
    s->data[i] = p;
  }
  return s;
}

}  // namespace fk
