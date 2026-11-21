// FluxKit - animation. Animations only ever change layer properties (offset, scale, opacity, frame, reveal),
// so playing them costs compositing only: nothing is rasterized at runtime.
#pragma once
#include "Types.h"

namespace fk {

enum class Curve { Linear, EaseIn, EaseOut, EaseInOut, Spring };

struct Animation {
  uint32_t duration = 300;  // ms; 0 = apply immediately
  Curve curve = Curve::EaseOut;
  uint32_t delayMs = 0;
  float bounce = 0.3f;  // Spring only: 0 = no overshoot, ~0.5 = bouncy

  static Animation none() { return Animation{0, Curve::Linear, 0, 0.f}; }
  static Animation linear(uint32_t ms = 300) { return Animation{ms, Curve::Linear, 0, 0.f}; }
  static Animation easeIn(uint32_t ms = 300) { return Animation{ms, Curve::EaseIn, 0, 0.f}; }
  static Animation easeOut(uint32_t ms = 300) { return Animation{ms, Curve::EaseOut, 0, 0.f}; }
  static Animation easeInOut(uint32_t ms = 300) { return Animation{ms, Curve::EaseInOut, 0, 0.f}; }
  static Animation spring(uint32_t ms = 480, float bounce = 0.3f) { return Animation{ms, Curve::Spring, 0, bounce}; }
  Animation delay(uint32_t ms) const { Animation a = *this; a.delayMs = ms; return a; }
};

float evalCurve(Curve c, float t, float bounce);

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
    std::function<void(float)> upd;
    std::function<void()> done;
  };
  std::vector<Tween> tw_;
};

}  // namespace fk
