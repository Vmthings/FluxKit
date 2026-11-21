#include "Raster.h"
#include <cstring>

namespace fk {
namespace raster {

static inline float rad(float d) { return d * 3.14159265f / 180.f; }

static inline void blendColor(uint32_t& d, Color c, float cov) {
  int a = (int)(c.a * cov + 0.5f);
  if (a <= 0) return;
  uint32_t A = (uint32_t)a;
  uint32_t s = (A << 24) | (((c.r * A + 127) / 255) << 16) | (((c.g * A + 127) / 255) << 8) | ((c.b * A + 127) / 255);
  d = over(s, d);
}

// signed distance to a rounded box (negative inside)
static inline float sdRound(float px, float py, float cx, float cy, float hw, float hh, float r) {
  float qx = std::fabs(px - cx) - hw, qy = std::fabs(py - cy) - hh;
  float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
  if (ox == 0.f && oy == 0.f) return std::max(qx, qy) - r;
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

void fillRoundRect(Surface& s, float x, float y, float w, float h, float r, const Paint& p) {
  if (w <= 0 || h <= 0 || !s.data) return;
  r = std::clamp(r, 0.f, std::min(w, h) * 0.5f);
  int x0 = std::max(0, (int)std::floor(x)), y0 = std::max(0, (int)std::floor(y));
  int x1 = std::min(s.w, (int)std::ceil(x + w)), y1 = std::min(s.h, (int)std::ceil(y + h));
  float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f - r, hh = h * 0.5f - r;
  float ca = std::cos(rad(p.angle)), sa = std::sin(rad(p.angle));
  float L = std::max(1.f, std::fabs(w * ca) + std::fabs(h * sa));
  for (int j = y0; j < y1; j++) {
    uint32_t* row = s.row(j);
    for (int i = x0; i < x1; i++) {
      float d = sdRound(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r);
      float cov = std::clamp(0.5f - d, 0.f, 1.f);
      if (cov <= 0.f) continue;
      Color c = p.a;
      if (p.gradient) c = p.at(((i + 0.5f - cx) * ca + (j + 0.5f - cy) * sa) / L + 0.5f);
      blendColor(row[i], c, cov);
    }
  }
}

void strokeRoundRect(Surface& s, float x, float y, float w, float h, float r, float t, Color c) {
  if (w <= 0 || h <= 0 || t <= 0 || !s.data) return;
  r = std::clamp(r, 0.f, std::min(w, h) * 0.5f);
  int x0 = std::max(0, (int)std::floor(x)), y0 = std::max(0, (int)std::floor(y));
  int x1 = std::min(s.w, (int)std::ceil(x + w)), y1 = std::min(s.h, (int)std::ceil(y + h));
  float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f - r, hh = h * 0.5f - r;
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) {
      float d = sdRound(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r);
      float cov = std::min(std::clamp(0.5f - d, 0.f, 1.f), std::clamp(0.5f + d + t, 0.f, 1.f));
      if (cov > 0.f) blendColor(s.row(j)[i], c, cov);
    }
}

void fillCircle(Surface& s, float cx, float cy, float r, const Paint& p) {
  fillRoundRect(s, cx - r, cy - r, 2 * r, 2 * r, r, p);
}

void strokeCircle(Surface& s, float cx, float cy, float r, float t, Color c) {
  if (!s.data) return;
  int x0 = std::max(0, (int)std::floor(cx - r - t)), x1 = std::min(s.w, (int)std::ceil(cx + r + t));
  int y0 = std::max(0, (int)std::floor(cy - r - t)), y1 = std::min(s.h, (int)std::ceil(cy + r + t));
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) {
      float d = std::hypot(i + 0.5f - cx, j + 0.5f - cy);
      float cov = std::clamp(0.5f - (std::fabs(d - r) - t * 0.5f), 0.f, 1.f);
      if (cov > 0.f) blendColor(s.row(j)[i], c, cov);
    }
}

void line(Surface& s, float ax, float ay, float bx, float by, float t, Color c) {
  if (!s.data) return;
  float hw = t * 0.5f;
  int x0 = std::max(0, (int)std::floor(std::min(ax, bx) - hw - 1)), x1 = std::min(s.w, (int)std::ceil(std::max(ax, bx) + hw + 1));
  int y0 = std::max(0, (int)std::floor(std::min(ay, by) - hw - 1)), y1 = std::min(s.h, (int)std::ceil(std::max(ay, by) + hw + 1));
  float dx = bx - ax, dy = by - ay, len2 = std::max(1e-6f, dx * dx + dy * dy);
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) {
      float px = i + 0.5f - ax, py = j + 0.5f - ay;
      float u = std::clamp((px * dx + py * dy) / len2, 0.f, 1.f);
      float d = std::hypot(px - dx * u, py - dy * u);
      float cov = std::clamp(0.5f - (d - hw), 0.f, 1.f);
      if (cov > 0.f) blendColor(s.row(j)[i], c, cov);
    }
}

void arc(Surface& s, float cx, float cy, float r, float t, float startDeg, float sweepDeg, Color c) {
  if (!s.data) return;
  int x0 = std::max(0, (int)std::floor(cx - r - t)), x1 = std::min(s.w, (int)std::ceil(cx + r + t));
  int y0 = std::max(0, (int)std::floor(cy - r - t)), y1 = std::min(s.h, (int)std::ceil(cy + r + t));
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) {
      float px = i + 0.5f - cx, py = j + 0.5f - cy;
      float a = std::atan2(py, px) * 180.f / 3.14159265f - startDeg;
      a = std::fmod(a, 360.f); if (a < 0) a += 360.f;
      if (a > sweepDeg) continue;
      float d = std::hypot(px, py);
      float cov = std::clamp(0.5f - (std::fabs(d - r) - t * 0.5f), 0.f, 1.f);
      if (cov > 0.f) blendColor(s.row(j)[i], c, cov);
    }
  // round caps
  float a0 = rad(startDeg), a1 = rad(startDeg + sweepDeg);
  fillCircle(s, cx + r * std::cos(a0), cy + r * std::sin(a0), t * 0.5f, Paint(c));
  fillCircle(s, cx + r * std::cos(a1), cy + r * std::sin(a1), t * 0.5f, Paint(c));
}

static void blurLine(const uint32_t* in, uint32_t* out, int n, int r) {
  const int win = 2 * r + 1;
  const uint32_t inv = (65536u + win - 1) / win;
  int sa = 0, sr = 0, sg = 0, sb = 0;
  auto at = [&](int i) { return in[i < 0 ? 0 : (i >= n ? n - 1 : i)]; };
  for (int i = -r; i <= r; i++) {
    uint32_t p = at(i);
    sa += p >> 24; sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255;
  }
  for (int i = 0; i < n; i++) {
    uint32_t a = std::min<uint32_t>(255, ((uint32_t)sa * inv + 32768) >> 16);
    uint32_t R = std::min<uint32_t>(a, ((uint32_t)sr * inv + 32768) >> 16);
    uint32_t G = std::min<uint32_t>(a, ((uint32_t)sg * inv + 32768) >> 16);
    uint32_t B = std::min<uint32_t>(a, ((uint32_t)sb * inv + 32768) >> 16);
    out[i] = (a << 24) | (R << 16) | (G << 8) | B;
    uint32_t po = at(i - r), pi = at(i + r + 1);
    sa += (int)(pi >> 24) - (int)(po >> 24);
    sr += (int)((pi >> 16) & 255) - (int)((po >> 16) & 255);
    sg += (int)((pi >> 8) & 255) - (int)((po >> 8) & 255);
    sb += (int)(pi & 255) - (int)(po & 255);
  }
}

void boxBlur(Surface& s, int r, int passes) {
  if (r < 1 || !s.data) return;
  std::vector<uint32_t> in(std::max(s.w, s.h)), out(std::max(s.w, s.h));
  for (int p = 0; p < passes; p++) {
    for (int y = 0; y < s.h; y++) {
      uint32_t* row = s.row(y);
      blurLine(row, out.data(), s.w, r);
      memcpy(row, out.data(), (size_t)s.w * 4);
    }
    for (int x = 0; x < s.w; x++) {
      for (int y = 0; y < s.h; y++) in[y] = s.data[(size_t)y * s.w + x];
      blurLine(in.data(), out.data(), s.h, r);
      for (int y = 0; y < s.h; y++) s.data[(size_t)y * s.w + x] = out[y];
    }
  }
}

SurfacePtr crop(const Surface& s, Rect r, bool clampEdges) {
  auto d = std::make_shared<Surface>(r.w, r.h);
  if (!d->data || !s.data) return d;
  for (int y = 0; y < d->h; y++) {
    int sy = r.y + y;
    if (clampEdges) sy = std::clamp(sy, 0, s.h - 1);
    else if (sy < 0 || sy >= s.h) continue;
    uint32_t* dr = d->row(y);
    const uint32_t* sr = s.row(sy);
    for (int x = 0; x < d->w; x++) {
      int sx = r.x + x;
      if (clampEdges) sx = std::clamp(sx, 0, s.w - 1);
      else if (sx < 0 || sx >= s.w) continue;
      dr[x] = sr[sx];
    }
  }
  return d;
}

SurfacePtr downscale(const Surface& s, int f) {
  f = std::max(1, f);
  auto d = std::make_shared<Surface>((s.w + f - 1) / f, (s.h + f - 1) / f);
  if (!d->data || !s.data) return d;
  for (int y = 0; y < d->h; y++)
    for (int x = 0; x < d->w; x++) {
      uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
      for (int j = y * f; j < std::min(s.h, y * f + f); j++)
        for (int i = x * f; i < std::min(s.w, x * f + f); i++) {
          uint32_t p = s.row(j)[i];
          a += p >> 24; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
        }
      if (n) d->row(y)[x] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
    }
  return d;
}

static inline uint32_t lerp8(uint32_t a, uint32_t b, uint32_t f) {  // f 0..256
  uint32_t rb = (((a & 0x00FF00FFu) * (256 - f) + (b & 0x00FF00FFu) * f) >> 8) & 0x00FF00FFu;
  uint32_t ag = ((((a >> 8) & 0x00FF00FFu) * (256 - f) + ((b >> 8) & 0x00FF00FFu) * f)) & 0xFF00FF00u;
  return rb | ag;
}

SurfacePtr resize(const Surface& s, int nw, int nh) {
  auto d = std::make_shared<Surface>(nw, nh);
  if (!d->data || !s.data) return d;
  for (int y = 0; y < d->h; y++) {
    float fy = (y + 0.5f) * s.h / d->h - 0.5f;
    int y0 = (int)std::floor(fy); uint32_t wy = (uint32_t)((fy - y0) * 256);
    int ya = std::clamp(y0, 0, s.h - 1), yb = std::clamp(y0 + 1, 0, s.h - 1);
    for (int x = 0; x < d->w; x++) {
      float fx = (x + 0.5f) * s.w / d->w - 0.5f;
      int x0 = (int)std::floor(fx); uint32_t wx = (uint32_t)((fx - x0) * 256);
      int xa = std::clamp(x0, 0, s.w - 1), xb = std::clamp(x0 + 1, 0, s.w - 1);
      uint32_t t = lerp8(s.row(ya)[xa], s.row(ya)[xb], wx);
      uint32_t b = lerp8(s.row(yb)[xa], s.row(yb)[xb], wx);
      d->row(y)[x] = lerp8(t, b, wy);
    }
  }
  return d;
}

void blend(Surface& dst, const Surface& src, int dx, int dy, uint32_t op) {
  for (int y = 0; y < src.h; y++) {
    int ty = dy + y;
    if (ty < 0 || ty >= dst.h) continue;
    for (int x = 0; x < src.w; x++) {
      int tx = dx + x;
      if (tx < 0 || tx >= dst.w) continue;
      uint32_t p = src.row(y)[x];
      if (op < 255) p = scale(p, op);
      dst.row(ty)[tx] = over(p, dst.row(ty)[tx]);
    }
  }
}

std::shared_ptr<Surface565> toRGB565(const Surface& s) {
  static const uint8_t kB[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  auto d = std::make_shared<Surface565>(s.w, s.h);
  if (!d->data || !s.data) return d;
  for (int y = 0; y < s.h; y++) {
    const uint32_t* sp = s.row(y);
    uint16_t* dp = d->data + (size_t)y * s.w;
    for (int x = 0; x < s.w; x++) {
      uint32_t p = sp[x], dm = kB[((y & 3) << 2) | (x & 3)];
      uint32_t r = std::min<uint32_t>(255, ((p >> 16) & 255) + (dm >> 1));
      uint32_t g = std::min<uint32_t>(255, ((p >> 8) & 255) + (dm >> 2));
      uint32_t b = std::min<uint32_t>(255, (p & 255) + (dm >> 1));
      dp[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
  }
  return d;
}

void tint(Surface& s, Color c) {
  uint32_t p = c.premul();
  for (size_t i = 0, n = (size_t)s.w * s.h; i < n; i++) s.data[i] = over(p, s.data[i]);
}

void dim(Surface& s, float k) {
  uint32_t m = (uint32_t)std::lround(std::clamp(k, 0.f, 1.f) * 255);
  for (size_t i = 0, n = (size_t)s.w * s.h; i < n; i++) {
    uint32_t p = s.data[i];
    s.data[i] = (p & 0xFF000000u) | (scale(p & 0x00FFFFFFu, m) & 0x00FFFFFFu);
  }
}

void maskRoundRect(Surface& s, float x, float y, float w, float h, float r) {
  r = std::clamp(r, 0.f, std::min(w, h) * 0.5f);
  float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f - r, hh = h * 0.5f - r;
  for (int j = 0; j < s.h; j++)
    for (int i = 0; i < s.w; i++) {
      float cov = std::clamp(0.5f - sdRound(i + 0.5f, j + 0.5f, cx, cy, hw, hh, r), 0.f, 1.f);
      if (cov < 1.f) s.row(j)[i] = scale(s.row(j)[i], (uint32_t)(cov * 255 + 0.5f));
    }
}

int shadowMargin(const Shadow& sh) {
  return (int)std::ceil(sh.radius * 3.f) + 2 + (int)std::ceil(std::max(std::fabs(sh.x), std::fabs(sh.y)));
}

void shadow(Surface& s, float x, float y, float w, float h, float r, const Shadow& sh) {
  int m = (int)std::ceil(sh.radius * 3.f) + 2;
  int tw = (int)std::ceil(w) + 2 * m, th = (int)std::ceil(h) + 2 * m;
  Surface tmp(tw, th);
  if (!tmp.data) return;
  fillRoundRect(tmp, m, m, w, h, r, Paint(Color(0xFFFFFF)));
  boxBlur(tmp, std::max(1, (int)std::lround(sh.radius)), 3);
  int ox = (int)std::floor(x + sh.x) - m, oy = (int)std::floor(y + sh.y) - m;
  for (int j = 0; j < th; j++) {
    int ty = oy + j; if (ty < 0 || ty >= s.h) continue;
    for (int i = 0; i < tw; i++) {
      int tx = ox + i; if (tx < 0 || tx >= s.w) continue;
      uint32_t a = tmp.row(j)[i] >> 24;
      if (!a) continue;
      blendColor(s.row(ty)[tx], sh.color, a / 255.f);
    }
  }
}

void material(Surface& dst, int dx, int dy, int w, int h, float radius, const Material& m,
              const Surface& backdrop, int bx, int by, int ax, int ay) {
  if (w <= 0 || h <= 0) return;
  int mg = (int)std::ceil(m.blur * 1.5f) + 2;
  auto region = crop(backdrop, Rect{ax - bx - mg, ay - by - mg, w + 2 * mg, h + 2 * mg}, true);
  int f = std::clamp((int)(m.blur / 4.f), 1, 8);
  auto small = downscale(*region, f);
  boxBlur(*small, std::max(1, (int)std::lround(m.blur / f)), 3);
  auto big = resize(*small, region->w, region->h);
  auto inner = crop(*big, Rect{mg, mg, w, h}, false);
  tint(*inner, m.tint);
  maskRoundRect(*inner, 0, 0, (float)w, (float)h, radius);
  blend(dst, *inner, dx, dy);
}

}  // namespace raster

void Painter::clear(Color c) {
  uint32_t p = c.premul();
  for (size_t i = 0, n = (size_t)s_.w * s_.h; i < n; i++) s_.data[i] = p;
}

}  // namespace fk
