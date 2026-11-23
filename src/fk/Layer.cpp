#include "Layer.h"
#include <cstring>
#include "Platform.h"
#include "Ppa.h"
#include "Raster.h"

namespace fk {

void Compositor::init(int w, int h) {
  w_ = w; h_ = h;
  if (tile_) platform::release(tile_);
  size_t bytes = ((size_t)w * h * 2 + 127) & ~(size_t)127;
  tile_ = (uint16_t*)platform::allocAligned(bytes, 128);  // over-aligned: valid PPA output buffer
  if (!tile_) platform::log("out of memory: cannot allocate the %u KB frame buffer", (unsigned)(bytes / 1024));
  root.w = w; root.h = h;
  root.wclip = Rect{0, 0, w, h};
}

static inline Rect rectOf(float x, float y, float w, float h) {
  int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
  int x1 = (int)std::ceil(x + w), y1 = (int)std::ceil(y + h);
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

void Compositor::walk(Layer* l, const World& p) {
  float cx = l->x + l->w * 0.5f, cy = l->y + l->h * 0.5f;
  l->ws = p.ws * l->scale;
  l->wx = p.wx + p.ws * (cx + l->scale * (l->x - cx) + l->tx);
  l->wy = p.wy + p.ws * (cy + l->scale * (l->y - cy) + l->ty);
  l->wo = p.wo * l->opacity;
  l->wvis = p.wvis && l->visible;
  l->wclip = p.wclip;
  order_.push_back(l);

  l->rect = rectOf(l->wx, l->wy, l->w * l->ws, l->h * l->ws).intersect(l->wclip);
  l->bounds = Rect{};
  l->cover = Rect{};
  if (l->wvis && l->wo > 0.002f && l->isSolid) {
    int x0 = (int)std::lround(l->wx), y0 = (int)std::lround(l->wy);
    int x1 = (int)std::lround(l->wx + l->w * l->ws * l->reveal), y1 = (int)std::lround(l->wy + l->h * l->ws);
    l->bounds = Rect{x0, y0, x1 - x0, y1 - y0}.intersect(l->wclip);
    l->cover = l->bounds;
  } else if (l->wvis && l->wo > 0.002f && l->hasPixels()) {
    float rw = l->w * l->ws * l->reveal, rh = l->h * l->ws;
    bool simple = std::fabs(l->ws - 1.f) < 1e-4f && l->pixelScale == 1.f;
    if (simple) {
      int ox = (int)std::lround(l->wx), oy = (int)std::lround(l->wy);
      int rw_i = (int)std::lround(l->pixW() * l->reveal);
      Rect r{ox, oy, rw_i, l->pixH()};
      l->bounds = r.intersect(l->wclip);
      l->cover = l->bounds;
    } else {
      l->bounds = rectOf(l->wx, l->wy, rw, rh).outset(1).intersect(l->wclip);
      int cx0 = (int)std::ceil(l->wx) + 1, cy0 = (int)std::ceil(l->wy) + 1;
      int cx1 = (int)std::floor(l->wx + rw) - 1, cy1 = (int)std::floor(l->wy + rh) - 1;
      l->cover = Rect{cx0, cy0, cx1 - cx0, cy1 - cy0}.intersect(l->wclip);
    }
  }

  World child{l->ws, l->wx, l->wy, l->wo, l->wvis, l->clip ? l->wclip.intersect(l->rect) : l->wclip};
  for (auto& k : l->kids) walk(k.get(), child);
}

bool Compositor::prepare(Rect& dirty) {
  order_.clear();
  World top{1.f, 0.f, 0.f, 1.f, true, Rect{0, 0, w_, h_}};
  for (auto& k : root.kids) walk(k.get(), top);

  dirty = Rect{};
  const Rect screen{0, 0, w_, h_};
  for (Layer* l : order_) {
    auto& L = l->last;
    bool vis = !l->bounds.empty();
    bool changed = L.bounds != l->bounds || L.vis != vis || L.frame != l->frame || L.ver != l->ver ||
                   (vis && (L.wo != l->wo || L.ws != l->ws || L.wx != l->wx || L.wy != l->wy || L.reveal != l->reveal));
    if (changed) {
      dirty = dirty.unite(L.bounds).unite(l->bounds);
      L.bounds = l->bounds; L.vis = vis; L.frame = l->frame; L.ver = l->ver;
      L.wo = l->wo; L.ws = l->ws; L.wx = l->wx; L.wy = l->wy; L.reveal = l->reveal;
    }
  }
  dirty = dirty.unite(extraDirty_);
  extraDirty_ = Rect{};
  if (forceFull_) { dirty = screen; forceFull_ = false; }
  dirty = dirty.intersect(screen);
  return !dirty.empty();
}

Layer* Compositor::hitTest(int x, int y) {
  for (size_t i = order_.size(); i-- > 0;) {
    Layer* l = order_[i];
    if (l->wvis && l->hit && l->wo > 0.002f && l->rect.contains(x, y)) return l;
  }
  return nullptr;
}

void Compositor::bringToFront(Layer* l) {
  Layer* p = l->parent;
  if (!p) return;
  for (size_t i = 0; i < p->kids.size(); i++)
    if (p->kids[i].get() == l) { std::rotate(p->kids.begin() + i, p->kids.begin() + i + 1, p->kids.end()); return; }
}

// ------------------------------------------------------------------ pixel helpers
// 4x4 ordered dither: hides RGB565 banding in gradients, shadows and blurs at ~zero cost
static const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static inline uint16_t pack565(uint32_t r, uint32_t g, uint32_t b, uint32_t dm) {
  r = std::min<uint32_t>(255, r + (dm >> 1)); g = std::min<uint32_t>(255, g + (dm >> 2)); b = std::min<uint32_t>(255, b + (dm >> 1));
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline uint16_t blend565(uint16_t d, uint32_t s, uint32_t dm) {
  uint32_t sa = s >> 24;
  if (sa == 255) return pack565((s >> 16) & 255, (s >> 8) & 255, s & 255, dm);
  if (sa == 0) return d;
  uint32_t ia = 255 - sa;
  uint32_t dr = (d >> 8) & 0xF8, dg = (d >> 3) & 0xFC, db = (d << 3) & 0xF8;
  dr |= dr >> 5; dg |= dg >> 6; db |= db >> 5;
  uint32_t r = std::min<uint32_t>(255, ((s >> 16) & 255) + (((dr * ia + 128) * 257) >> 16));
  uint32_t g = std::min<uint32_t>(255, ((s >> 8) & 255) + (((dg * ia + 128) * 257) >> 16));
  uint32_t b = std::min<uint32_t>(255, (s & 255) + (((db * ia + 128) * 257) >> 16));
  return pack565(r, g, b, dm);
}

static inline uint32_t lerpPx(uint32_t a, uint32_t b, uint32_t f) {
  uint32_t rb = (((a & 0x00FF00FFu) * (256 - f) + (b & 0x00FF00FFu) * f) >> 8) & 0x00FF00FFu;
  uint32_t ag = ((((a >> 8) & 0x00FF00FFu) * (256 - f) + ((b >> 8) & 0x00FF00FFu) * f)) & 0xFF00FF00u;
  return rb | ag;
}

// straight <-> premultiplied alpha (the PPA wants straight alpha; the CPU rasterizer works premultiplied)
static inline uint32_t premulPx(uint32_t p) {
  uint32_t a = p >> 24;
  if (a == 255) return p;
  if (a == 0) return 0;
  return (a << 24) | ((((p >> 16) & 255) * a + 127) / 255 << 16) | ((((p >> 8) & 255) * a + 127) / 255 << 8) | (((p & 255) * a + 127) / 255);
}
static void toStraight(Surface& s) {
  if (s.straight || !s.data) return;
  for (size_t i = 0, n = (size_t)s.w * s.h; i < n; i++) {
    uint32_t p = s.data[i], a = p >> 24;
    if (a == 0 || a == 255) continue;
    uint32_t r = std::min<uint32_t>(255, (((p >> 16) & 255) * 255 + a / 2) / a);
    uint32_t g = std::min<uint32_t>(255, (((p >> 8) & 255) * 255 + a / 2) / a);
    uint32_t b = std::min<uint32_t>(255, ((p & 255) * 255 + a / 2) / a);
    s.data[i] = (a << 24) | (r << 16) | (g << 8) | b;
  }
  s.straight = true;
}

static void walkSurfaces(Layer* l, const std::function<void(Surface&)>& fn) {
  for (auto& f : l->frames) if (f) fn(*f);
  for (auto& k : l->kids) walkSurfaces(k.get(), fn);
}
void Compositor::makeStraightAlpha() { walkSurfaces(&root, [](Surface& s) { toStraight(s); }); }

// d + (s-d)*a/32 on packed RGB565 (Bodmer's SWAR blend)
static inline uint16_t mix565(uint16_t d, uint16_t s, uint32_t a) {
  uint32_t D = (d | ((uint32_t)d << 16)) & 0x07E0F81Fu, S = (s | ((uint32_t)s << 16)) & 0x07E0F81Fu;
  uint32_t R = ((((S - D) * a) >> 5) + D) & 0x07E0F81Fu;
  return (uint16_t)(R | (R >> 16));
}

// ------------------------------------------------------------------ drawing (CPU)
void Compositor::draw565(Layer* l, const Rect& d, const Rect& clip, uint16_t* tile) {
  Rect r = l->bounds.intersect(clip);
  if (r.empty()) return;
  const Surface565& S = *l->p565;
  const uint32_t op = (uint32_t)std::lround(std::clamp(l->wo, 0.f, 1.f) * 255.f);
  const uint32_t a = (op * 32 + 127) / 255;
  if (std::fabs(l->ws - 1.f) < 1e-4f && l->pixelScale == 1.f) {
    int ox = (int)std::lround(l->wx), oy = (int)std::lround(l->wy);
    int lim = (int)std::lround(S.w * l->reveal);
    int x0 = std::max(r.x, ox), x1 = std::min(r.right(), ox + lim);
    if (x1 <= x0) return;
    int n = x1 - x0;
    for (int y = r.y; y < r.bottom(); y++) {
      int sy = y - oy;
      if (sy < 0 || sy >= S.h) continue;
      const uint16_t* sp = S.data + (size_t)sy * S.w + (x0 - ox);
      uint16_t* dp = tile + (size_t)(y - d.y) * d.w + (x0 - d.x);
      if (op >= 255) memcpy(dp, sp, (size_t)n * 2);
      else for (int i = 0; i < n; i++) dp[i] = mix565(dp[i], sp[i], a);
    }
    return;
  }
  float inv = 1.f / l->ws;  // scaled opaque layer (rare): nearest neighbour
  for (int y = r.y; y < r.bottom(); y++) {
    int sy = (int)((y + 0.5f - l->wy) * inv);
    if (sy < 0 || sy >= S.h) continue;
    uint16_t* dp = tile + (size_t)(y - d.y) * d.w - d.x;
    for (int x = r.x; x < r.right(); x++) {
      int sx = (int)((x + 0.5f - l->wx) * inv);
      if (sx < 0 || sx >= (int)(S.w * l->reveal)) continue;
      uint16_t s = S.data[(size_t)sy * S.w + sx];
      dp[x] = op >= 255 ? s : mix565(dp[x], s, a);
    }
  }
}

void Compositor::drawSolid(Layer* l, const Rect& d, const Rect& clip, uint16_t* tile) {
  Rect r = l->bounds.intersect(clip);
  if (r.empty()) return;
  uint32_t op = (uint32_t)std::lround(std::clamp(l->wo, 0.f, 1.f) * 255.f), a = (op * 32 + 127) / 255;
  for (int y = r.y; y < r.bottom(); y++) {
    uint16_t* dp = tile + (size_t)(y - d.y) * d.w + (r.x - d.x);
    if (op >= 255) std::fill(dp, dp + r.w, l->solid565);
    else for (int i = 0; i < r.w; i++) dp[i] = mix565(dp[i], l->solid565, a);
  }
}

// Offload one layer to the PPA. Returns true if the hardware drew it (or there was nothing to draw).
bool Compositor::drawPPA(Layer* l, const Rect& d, uint16_t* tile) {
  Rect r = l->bounds.intersect(d);
  if (r.empty()) return true;
  if ((long)r.w * r.h < ppaMinPixels) return false;  // tiny layers: a PPA transaction costs more than the CPU blit
  const uint32_t op = (uint32_t)std::lround(std::clamp(l->wo, 0.f, 1.f) * 255.f);
  bool ok = false;
  if (l->isSolid) {
    if (op < 255) return false;
    ok = ppa::fill565(tile, d.w, d.h, r.x - d.x, r.y - d.y, r.w, r.h, l->solid565);
  } else {
    if (std::fabs(l->ws - 1.f) >= 1e-4f || l->pixelScale != 1.f) return false;  // scaled layers stay on the CPU
    int ox = (int)std::lround(l->wx), oy = (int)std::lround(l->wy);
    int lim = (int)std::lround(l->pixW() * l->reveal);
    int x0 = std::max(r.x, ox), x1 = std::min(r.right(), ox + lim), y0 = std::max(r.y, oy), y1 = std::min(r.bottom(), oy + l->pixH());
    if (x1 <= x0 || y1 <= y0) return true;
    int w = x1 - x0, h = y1 - y0;
    if (l->p565) {
      ok = ppa::blend565(tile, d.w, d.h, x0 - d.x, y0 - d.y, w, h, l->p565->data, l->p565->w, l->p565->h, x0 - ox, y0 - oy, op);
    } else {
      Surface& S = *l->frames[std::clamp(l->frame, 0, (int)l->frames.size() - 1)];
      if (!S.data) return true;
      toStraight(S);
      ok = ppa::blendArgb(tile, d.w, d.h, x0 - d.x, y0 - d.y, w, h, S.data, S.w, S.h, x0 - ox, y0 - oy, op);
    }
  }
  if (ok) { ppaFails_ = 0; return true; }
  if (++ppaFails_ >= 8) {
    ppa = false;
    platform::log("PPA offload disabled after repeated errors: falling back to the CPU compositor");
  }
  return false;
}

void Compositor::drawLayer(Layer* l, const Rect& d, const Rect& clip, uint16_t* tile) {
  if (ppa && clip == d && drawPPA(l, d, tile)) return;
  if (l->isSolid) { drawSolid(l, d, clip, tile); return; }
  if (l->p565) { draw565(l, d, clip, tile); return; }
  Rect r = l->bounds.intersect(clip);
  if (r.empty()) return;
  const Surface& S = *l->frames[std::clamp(l->frame, 0, (int)l->frames.size() - 1)];
  if (!S.data) return;
  const bool straight = S.straight;
  const uint32_t op = (uint32_t)std::lround(std::clamp(l->wo, 0.f, 1.f) * 255.f);
  const bool simple = std::fabs(l->ws - 1.f) < 1e-4f && l->pixelScale == 1.f;

  if (simple) {
    int ox = (int)std::lround(l->wx), oy = (int)std::lround(l->wy);
    int lim = (int)std::lround(S.w * l->reveal);
    for (int y = r.y; y < r.bottom(); y++) {
      int sy = y - oy;
      if (sy < 0 || sy >= S.h) continue;
      const uint32_t* sp = S.row(sy);
      uint16_t* dp = tile + (size_t)(y - d.y) * d.w - d.x;
      for (int x = r.x; x < r.right(); x++) {
        int sx = x - ox;
        if (sx < 0 || sx >= lim) continue;
        uint32_t p = sp[sx];
        if (straight) p = premulPx(p);
        if (op < 255) p = raster::scale(p, op);
        if (p >> 24) dp[x] = blend565(dp[x], p, kBayer[((y & 3) << 2) | (x & 3)]);
      }
    }
    return;
  }

  // scaled / low-res layer: bilinear, inverse mapping
  const float inv = 1.f / l->ws, ps = l->pixelScale;
  const float limX = l->w * l->reveal;
  std::vector<int> xa(r.w), xb(r.w);
  std::vector<uint8_t> xf(r.w), xv(r.w);
  for (int i = 0; i < r.w; i++) {
    float qx = (r.x + i + 0.5f - l->wx) * inv;
    xv[i] = (qx >= 0 && qx < limX) ? 1 : 0;
    float u = qx * ps - 0.5f;
    int x0 = (int)std::floor(u);
    xf[i] = (uint8_t)std::clamp((int)((u - x0) * 256.f), 0, 255);
    xa[i] = std::clamp(x0, 0, S.w - 1);
    xb[i] = std::clamp(x0 + 1, 0, S.w - 1);
  }
  for (int y = r.y; y < r.bottom(); y++) {
    float qy = (y + 0.5f - l->wy) * inv;
    if (qy < 0 || qy >= l->h) continue;
    float v = qy * ps - 0.5f;
    int y0 = (int)std::floor(v);
    uint32_t fy = (uint32_t)std::clamp((int)((v - y0) * 256.f), 0, 255);
    const uint32_t* ra = S.row(std::clamp(y0, 0, S.h - 1));
    const uint32_t* rb = S.row(std::clamp(y0 + 1, 0, S.h - 1));
    uint16_t* dp = tile + (size_t)(y - d.y) * d.w + (r.x - d.x);
    for (int i = 0; i < r.w; i++) {
      if (!xv[i]) continue;
      uint32_t p00 = ra[xa[i]], p10 = ra[xb[i]], p01 = rb[xa[i]], p11 = rb[xb[i]];
      if (straight) { p00 = premulPx(p00); p10 = premulPx(p10); p01 = premulPx(p01); p11 = premulPx(p11); }
      uint32_t p = lerpPx(lerpPx(p00, p10, xf[i]), lerpPx(p01, p11, xf[i]), fy);
      if (op < 255) p = raster::scale(p, op);
      if (p >> 24) dp[i] = blend565(dp[i], p, kBayer[((y & 3) << 2) | ((r.x + i) & 3)]);
    }
  }
}

void Compositor::composeTo(Rect d, uint16_t* tile) {
  std::vector<Layer*> list;
  for (Layer* l : order_)
    if (!l->bounds.empty() && !l->bounds.intersect(d).empty()) list.push_back(l);
  size_t start = 0;
  bool covered = false;
  for (size_t i = list.size(); i-- > 0;) {
    Layer* l = list[i];
    if (l->opaque && l->wo >= 0.999f && !l->cover.empty() && l->cover.intersect(d) == d) { start = i; covered = true; break; }
  }
  auto run = [&](const Rect& clip) {
    if (!covered)
      for (int y = clip.y; y < clip.bottom(); y++) memset(tile + (size_t)(y - d.y) * d.w + (clip.x - d.x), 0, (size_t)clip.w * 2);
    for (size_t i = start; i < list.size(); i++) drawLayer(list[i], d, clip, tile);
  };
  int n = (ppa || threads <= 1) ? 1 : std::min(threads, d.h / 24);  // PPA transactions are issued from one thread
  if (n > 1 && (long)d.w * d.h >= 16384) {
    platform::parallelFor(n, [&](int k) {
      int y0 = d.y + d.h * k / n, y1 = d.y + d.h * (k + 1) / n;
      run(Rect{d.x, y0, d.w, y1 - y0});
    });
  } else run(d);
}

void Compositor::render(Rect d) {
  d = d.intersect(Rect{0, 0, w_, h_});
  if (d.empty() || !tile_) return;
  uint64_t t0 = profile ? platform::micros() : 0;
  composeTo(d, tile_);
  uint64_t t1 = profile ? platform::micros() : 0;
  platform::present(d.x, d.y, d.w, d.h, tile_);
  if (profile) {
    uint64_t t2 = platform::micros();
    if (!profT0_) profT0_ = t0;
    profCompose_ += t1 - t0; profPresent_ += t2 - t1; profPixels_ += (uint64_t)d.w * d.h; profFrames_++;
    if (t2 - profT0_ >= 1000000ULL) {
      platform::log("profile: %d frames/s, compose %.1f ms, present %.1f ms, %.0f%% of screen per frame",
                    profFrames_, profCompose_ / 1000.0 / profFrames_, profPresent_ / 1000.0 / profFrames_,
                    100.0 * profPixels_ / profFrames_ / ((double)w_ * h_));
      profT0_ = t2; profCompose_ = profPresent_ = profPixels_ = 0; profFrames_ = 0;
    }
  }
}

void Compositor::snapshot(Surface& out) {
  Rect full{0, 0, w_, h_};
  Rect dummy;
  prepare(dummy);
  if (!tile_) return;  // framebuffer tile could not be allocated
  composeTo(full, tile_);
  for (int y = 0; y < out.h && y < h_; y++)
    for (int x = 0; x < out.w && x < w_; x++) {
      uint16_t p = tile_[(size_t)y * w_ + x];
      uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
      r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
      out.row(y)[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

}  // namespace fk
