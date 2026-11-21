#include "Splash.h"
#include "Font.h"
#include "Raster.h"

namespace fk {
namespace splash {

static const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

Layer* create(Layer* parent, int W, int H, const Config& cfg) {
  Layer* root = addLayer(parent, nullptr, 0, 0, W, H);
  root->name = "splash";
  root->hit = true;  // swallow touches while rendering

  // vertical gradient, written straight to dithered RGB565 (fast: no per-pixel float maths)
  auto bg = std::make_shared<Surface565>(W, H);
  if (bg->data) {
    for (int y = 0; y < H; y++) {
      Color c = Color::mix(cfg.bootTop, cfg.bootBottom, H > 1 ? (float)y / (H - 1) : 0.f);
      uint16_t* row = bg->data + (size_t)y * W;
      for (int x = 0; x < W; x++) {
        uint32_t dm = kBayer[((y & 3) << 2) | (x & 3)];
        uint32_t r = std::min<uint32_t>(255, c.r + (dm >> 1)), g = std::min<uint32_t>(255, c.g + (dm >> 2)), b = std::min<uint32_t>(255, c.b + (dm >> 1));
        row[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
      }
    }
  }
  Layer* b = addLayer(root, nullptr, 0, 0, W, H);
  b->p565 = bg;
  b->opaque = true;

  const float u = W / 720.f;
  auto line = [&](const std::string& s, Font f, Color c, int centerY) {
    while (text::measure(f, s) > W - 60 && f.size > 12) f.size -= 2;  // long boot text shrinks to fit
    int tw = (int)std::ceil(text::measure(f, s)) + 8, th = text::lineHeight(f) + 8;
    auto surf = std::make_shared<Surface>(tw, th);
    text::draw(*surf, f, s, 4.f, (float)(text::ascent(f) + 4), c);
    addLayer(root, surf, (W - tw) / 2, centerY - th / 2, tw, th);
  };
  line(cfg.bootText, Font::bold((int)std::lround(54 * u)), Color::White, H / 2);
  if (cfg.bootCredit) {
    line("Made with FluxKit", Font::medium((int)std::lround(24 * u)), Color(0xD5DAE3, 150), H - (int)std::lround(96 * u));
    line("https://github.com/vmthings/FluxKit", Font::regular((int)std::lround(19 * u)), Color(0xD5DAE3, 120), H - (int)std::lround(58 * u));
  }
  return root;
}

void release(Layer* root) {
  if (!root) return;
  for (auto& k : root->kids) { k->frames.clear(); k->p565.reset(); }
  root->visible = false;
  root->hit = false;
}

}  // namespace splash
}  // namespace fk
