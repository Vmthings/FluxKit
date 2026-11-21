#include "Layer.h"
#include "Platform.h"
#include "Raster.h"
#include <cstring>

namespace fk {

void Compositor::init(int w, int h) {
  w_ = w; h_ = h;
  if (tile_) platform::release(tile_);
  tile_ = (uint16_t*)platform::alloc((size_t)w * h * 2);
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
  if (l->wvis && l->wo > 0.002f && !l->frames.empty() && l->frames[0]->w > 0) {
    float rw = l->w * l->ws * l->reveal, rh = l->h * l->ws;
    bool simple = std::fabs(l->ws - 1.f) < 1e-4f && l->pixelScale == 1.f;
    if (simple) {
      int ox = (int)std::lround(l->wx), oy = (int)std::lround(l->wy);
      int rw_i = (int)std::lround(l->frames[0]->w * l->reveal);
      Rect r{ox, oy, rw_i, l->frames[0]->h};
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
    bool changed = L.bounds != l->bounds || L.vis != vis || L.frame != l->frame ||
                   (vis && (L.wo != l->wo || L.ws != l->ws || L.wx != l->wx || L.wy != l->wy || L.reveal != l->reveal));
    if (changed) {
      dirty = dirty.unite(L.bounds).unite(l->bounds);
      L.bounds = l->bounds; L.vis = vis; L.frame = l->frame;
      L.wo = l->wo; L.ws = l->ws; L.wx = l->wx; L.wy = l->wy; L.reveal = l->reveal;
    }
  }
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

// ------------------------------------------------------------------ drawing
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

void Compositor::drawLayer(Layer* l, const Rect& d, uint16_t* tile) {
  Rect r = l->bounds.intersect(d);
  if (r.empty()) return;
  const Surface& S = *l->frames[std::clamp(l->frame, 0, (int)l->frames.size() - 1)];
  if (!S.data) return;
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
  std::vector<uint8_t> xf(r.w);
  std::vector<uint8_t> xv(r.w);
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
      uint32_t t = lerpPx(ra[xa[i]], ra[xb[i]], xf[i]);
      uint32_t b = lerpPx(rb[xa[i]], rb[xb[i]], xf[i]);
      uint32_t p = lerpPx(t, b, fy);
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
  if (!covered) memset(tile, 0, (size_t)d.w * d.h * 2);
  for (size_t i = start; i < list.size(); i++) drawLayer(list[i], d, tile);
}

void Compositor::render(Rect d) {
  d = d.intersect(Rect{0, 0, w_, h_});
  if (d.empty() || !tile_) return;
  composeTo(d, tile_);
  platform::present(d.x, d.y, d.w, d.h, tile_);
}

void Compositor::snapshot(Surface& out) {
  Rect full{0, 0, w_, h_};
  Rect dummy;
  prepare(dummy);
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
