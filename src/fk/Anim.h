// FluxKit - animation. Animations only ever change layer properties (offset, scale, opacity, frame, reveal),
// so playing them costs compositing only: nothing is rasterized at runtime.
#pragma once
#include "Types.h"

namespace fk {

enum class Curve { Linear, EaseIn, EaseOut, EaseInOut, Spring, CubicBezier };

struct Animation {
  uint32_t duration = 300;  // ms; 0 = apply immediately
  Curve curve = Curve::EaseOut;
  uint32_t delayMs = 0;
  float bounce = 0.3f;  // Spring only: 0 = no overshoot, ~0.5 = bouncy
  float bz[4] = {0.42f, 0.f, 0.58f, 1.f};  // CubicBezier control points x1,y1,x2,y2 (CSS ease-in-out by default)

  static Animation none() { return Animation{0, Curve::Linear, 0, 0.f}; }
  static Animation linear(uint32_t ms = 300) { return Animation{ms, Curve::Linear, 0, 0.f}; }
  static Animation easeIn(uint32_t ms = 300) { return Animation{ms, Curve::EaseIn, 0, 0.f}; }
  static Animation easeOut(uint32_t ms = 300) { return Animation{ms, Curve::EaseOut, 0, 0.f}; }
  static Animation easeInOut(uint32_t ms = 300) { return Animation{ms, Curve::EaseInOut, 0, 0.f}; }
  static Animation spring(uint32_t ms = 480, float bounce = 0.3f) { return Animation{ms, Curve::Spring, 0, bounce}; }
  // CSS-style cubic-bezier(x1, y1, x2, y2)
  static Animation cubicBezier(float x1, float y1, float x2, float y2, uint32_t ms = 300) {
    Animation a; a.duration = ms; a.curve = Curve::CubicBezier; a.bounce = 0.f;
    a.bz[0] = x1; a.bz[1] = y1; a.bz[2] = x2; a.bz[3] = y2; return a;
  }
  // cubic-bezier(0.42, 0, 0.58, 1): the standard "ease-in-out"
  static Animation bezierInOut(uint32_t ms = 300) { return cubicBezier(0.42f, 0.f, 0.58f, 1.f, ms); }
  Animation delay(uint32_t ms) const { Animation a = *this; a.delayMs = ms; return a; }
};

float evalCurve(Curve c, float t, float bounce, const float* bz = nullptr);

// "Appear" = how a view enters when its screen is shown (start state -> rest state).
struct Appear {
  float dx = 0, dy = 0, scale = 1.f, opacity = 1.f;
  Animation anim = Animation::easeOut(420);
  static Appear fade(uint32_t ms = 350) { Appear a; a.opacity = 0; a.anim = Animation::easeOut(ms); return a; }
  static Appear slideUp(float px = 48) { Appear a; a.dy = px; a.opacity = 0; a.anim = Animation::spring(520, 0.15f); return a; }
  static Appear slideDown(float px = 48) { Appear a; a.dy = -px; a.opacity = 0; a.anim = Animation::spring(520, 0.15f); return a; }
  static Appear slideIn(float px = 64) { Appear a; a.dx = px; a.opacity = 0; a.anim = Animation::spring(520, 0.15f); return a; }
  static Appear pop() { Appear a; a.scale = 0.82f; a.opacity = 0; a.anim = Animation::spring(520, 0.35f); return a; }
  Appear delay(uint32_t ms) const { Appear a = *this; a.anim.delayMs = ms; return a; }
};

class Animator {
 public:
  void animate(float* target, float to, const Animation& a, std::function<void()> done = {});
  void animateValue(float from, float to, const Animation& a, std::function<void(float)> update, std::function<void()> done = {});
  void cancel(float* target);
  void step(uint32_t now);
  bool active() const { return !tw_.empty(); }

 private:
  struct Tween {
    float* target = nullptr;
    float from = 0, to = 0;
    uint32_t start = 0, dur = 0;
    Curve curve = Curve::Linear;
    float bounce = 0;
    float bz[4] = {0.42f, 0.f, 0.58f, 1.f};
    std::function<void(float)> upd;
    std::function<void()> done;
  };
  std::vector<Tween> tw_;
};

}  // namespace fk
