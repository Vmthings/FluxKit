// FluxKit - software rasterizer (runs at boot, never per frame)
#pragma once
#include "Font.h"
#include "Surface.h"

namespace fk {
namespace raster {

// Premultiplied "source over"
inline uint32_t over(uint32_t s, uint32_t d) {
  uint32_t sa = s >> 24;
  if (sa == 255) return s;
  if (sa == 0) return d;
  uint32_t ia = 255 - sa;
  uint32_t x = (d & 0x00FF00FFu) * ia + 0x00800080u;
  x = ((x + ((x >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
  uint32_t y = ((d >> 8) & 0x00FF00FFu) * ia + 0x00800080u;
  y = (y + ((y >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
  return s + (x | y);
}
// scale all channels by o/255
inline uint32_t scale(uint32_t p, uint32_t o) {
  uint32_t x = (p & 0x00FF00FFu) * o + 0x00800080u;
  x = ((x + ((x >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
  uint32_t y = ((p >> 8) & 0x00FF00FFu) * o + 0x00800080u;
  y = (y + ((y >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
  return x | y;
}

void fillRoundRect(Surface& s, float x, float y, float w, float h, float r, const Paint& p);
void strokeRoundRect(Surface& s, float x, float y, float w, float h, float r, float thickness, Color c);
void fillCircle(Surface& s, float cx, float cy, float r, const Paint& p);
void strokeCircle(Surface& s, float cx, float cy, float r, float thickness, Color c);
void line(Surface& s, float x0, float y0, float x1, float y1, float thickness, Color c);
void arc(Surface& s, float cx, float cy, float r, float thickness, float startDeg, float sweepDeg, Color c);
void shadow(Surface& s, float x, float y, float w, float h, float r, const Shadow& sh);
int shadowMargin(const Shadow& sh);

void boxBlur(Surface& s, int radius, int passes = 3);  // ~gaussian sigma = radius
SurfacePtr crop(const Surface& s, Rect r, bool clampEdges);
SurfacePtr downscale(const Surface& s, int factor);
SurfacePtr resize(const Surface& s, int nw, int nh);   // bilinear
void blend(Surface& dst, const Surface& src, int dx, int dy, uint32_t opacity255 = 255);
std::shared_ptr<Surface565> toRGB565(const Surface& s);  // opaque surface -> dithered RGB565 (dither is baked in)
void tint(Surface& s, Color c);                         // overlay color on every pixel
void dim(Surface& s, float k);                          // multiply RGB (keeps alpha)
void maskRoundRect(Surface& s, float x, float y, float w, float h, float r);

// Frosted glass: blur `backdrop` (at abs origin bx,by) behind abs rect (ax,ay,w,h), draw into dst at (dx,dy)
void material(Surface& dst, int dx, int dy, int w, int h, float radius, const Material& m,
              const Surface& backdrop, int bx, int by, int ax, int ay);

}  // namespace raster

// Immediate-mode painter used by Canvas() draw callbacks.
class Painter {
 public:
  explicit Painter(Surface& s) : s_(s) {}
  int width() const { return s_.w; }
  int height() const { return s_.h; }
  Surface& surface() { return s_; }
  void clear(Color c = Color::Clear);
  void fillRect(float x, float y, float w, float h, Paint p) { raster::fillRoundRect(s_, x, y, w, h, 0, p); }
  void fillRoundRect(float x, float y, float w, float h, float r, Paint p) { raster::fillRoundRect(s_, x, y, w, h, r, p); }
  void fillCircle(float cx, float cy, float r, Paint p) { raster::fillCircle(s_, cx, cy, r, p); }
  void strokeCircle(float cx, float cy, float r, float t, Color c) { raster::strokeCircle(s_, cx, cy, r, t, c); }
  void strokeRoundRect(float x, float y, float w, float h, float r, float t, Color c) { raster::strokeRoundRect(s_, x, y, w, h, r, t, c); }
  void line(float x0, float y0, float x1, float y1, float t, Color c) { raster::line(s_, x0, y0, x1, y1, t, c); }
  void arc(float cx, float cy, float r, float t, float startDeg, float sweepDeg, Color c) { raster::arc(s_, cx, cy, r, t, startDeg, sweepDeg, c); }
  void text(const std::string& str, const Font& f, float x, float y, Color c);  // y = top of line
  void blur(int radius) { raster::boxBlur(s_, radius); }
 private:
  Surface& s_;
};

}  // namespace fk
