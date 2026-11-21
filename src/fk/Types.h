// FluxKit - basic value types
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fk {

constexpr int Unbounded = 1 << 20;  // "no limit" in layout

struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;
  constexpr Color() = default;
  // Color(0xRRGGBB) or Color(0xRRGGBB, alpha 0..255)
  constexpr Color(uint32_t rgb, uint8_t alpha = 255)
      : r((rgb >> 16) & 255), g((rgb >> 8) & 255), b(rgb & 255), a(alpha) {}
  static constexpr Color rgba(uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255) {
    Color c; c.r = R; c.g = G; c.b = B; c.a = A; return c;
  }
  Color opacity(float o) const {
    Color c = *this; c.a = (uint8_t)std::lround(std::clamp(o, 0.f, 1.f) * a); return c;
  }
  // premultiplied 0xAARRGGBB
  uint32_t premul() const {
    uint32_t A = a;
    return (A << 24) | (((r * A + 127) / 255) << 16) | (((g * A + 127) / 255) << 8) | ((b * A + 127) / 255);
  }
  static Color mix(Color x, Color y, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto m = [t](int p, int q) { return (uint8_t)std::lround(p + (q - p) * t); };
    return rgba(m(x.r, y.r), m(x.g, y.g), m(x.b, y.b), m(x.a, y.a));
  }
  static const Color White, Black, Clear, Red, Green, Blue, Gray, Orange, Yellow, Purple, Pink, Teal;
};
inline const Color Color::White{0xFFFFFF};
inline const Color Color::Black{0x000000};
inline const Color Color::Clear{0x000000, 0};
inline const Color Color::Red{0xFF453A};
inline const Color Color::Green{0x30D158};
inline const Color Color::Blue{0x0A84FF};
inline const Color Color::Gray{0x8E8E93};
inline const Color Color::Orange{0xFF9F0A};
inline const Color Color::Yellow{0xFFD60A};
inline const Color Color::Purple{0xBF5AF2};
inline const Color Color::Pink{0xFF375F};
inline const Color Color::Teal{0x64D2FF};

struct Size { int w = 0, h = 0; };

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool empty() const { return w <= 0 || h <= 0; }
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  Rect intersect(const Rect& o) const {
    int x0 = std::max(x, o.x), y0 = std::max(y, o.y);
    int x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
    if (x1 <= x0 || y1 <= y0) return Rect{};
    return Rect{x0, y0, x1 - x0, y1 - y0};
  }
  Rect unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    int x0 = std::min(x, o.x), y0 = std::min(y, o.y);
    int x1 = std::max(right(), o.right()), y1 = std::max(bottom(), o.bottom());
    return Rect{x0, y0, x1 - x0, y1 - y0};
  }
  Rect outset(int d) const { return Rect{x - d, y - d, w + 2 * d, h + 2 * d}; }
  bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
  bool operator!=(const Rect& o) const { return !(*this == o); }
};

struct Insets {
  int t = 0, l = 0, b = 0, r = 0;
  int horizontal() const { return l + r; }
  int vertical() const { return t + b; }
};

enum class Align { Leading, Top = Leading, Center, Trailing, Bottom = Trailing };

// Solid color or two-stop linear gradient.
struct Paint {
  Color a, b;
  bool gradient = false;
  float angle = 90.f;  // degrees; 0 = left->right, 90 = top->bottom
  Paint() = default;
  Paint(Color c) : a(c), b(c) {}
  Paint(uint32_t hex) : a(hex), b(hex) {}
  Color at(float t) const { return gradient ? Color::mix(a, b, t) : a; }
};
inline Paint LinearGradient(Color from, Color to, float angleDeg = 90.f) {
  Paint p; p.a = from; p.b = to; p.gradient = true; p.angle = angleDeg; return p;
}

struct Shadow {
  Color color{0x000000, 110};
  float radius = 14.f;  // ~ gaussian sigma in px
  float x = 0.f, y = 6.f;
};

// Frosted glass: blurs what is already painted behind the view.
struct Material {
  float blur = 24.f;
  Color tint{0xFFFFFF, 34};
};

}  // namespace fk
