#include "Anim.h"
#include "Platform.h"

namespace fk {

float evalCurve(Curve c, float t, float bounce) {
  t = std::clamp(t, 0.f, 1.f);
  switch (c) {
    case Curve::Linear: return t;
    case Curve::EaseIn: return t * t * t;
    case Curve::EaseOut: { float u = 1.f - t; return 1.f - u * u * u; }
    case Curve::EaseInOut: return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
    case Curve::Spring: {
      if (t >= 1.f) return 1.f;
      float zeta = std::clamp(1.f - bounce, 0.12f, 1.f);
      float k = 6.2f;  // decay so the spring has settled by t = 1
      if (zeta >= 0.999f) return 1.f - std::exp(-k * t) * (1.f + k * t);
      float w = k / zeta, wd = w * std::sqrt(1.f - zeta * zeta);
      return 1.f - std::exp(-k * t) * (std::cos(wd * t) + (k / wd) * std::sin(wd * t));
    }
  }
  return t;
}

void Animator::cancel(float* target) {
  for (size_t i = 0; i < tw_.size();)
    if (tw_[i].target == target) tw_.erase(tw_.begin() + i); else i++;
}

void Animator::animate(float* target, float to, const Animation& a, std::function<void()> done) {
  cancel(target);
  if (a.duration == 0 && a.delayMs == 0) {
    *target = to;
    if (done) done();
    return;
  }
  Tween t;
  t.target = target; t.from = *target; t.to = to;
  t.start = platform::millis() + a.delayMs; t.dur = std::max<uint32_t>(1, a.duration);
  t.curve = a.curve; t.bounce = a.bounce; t.done = std::move(done);
  tw_.push_back(std::move(t));
}

void Animator::animateValue(float from, float to, const Animation& a, std::function<void(float)> upd, std::function<void()> done) {
  Tween t;
  t.from = from; t.to = to;
  t.start = platform::millis() + a.delayMs; t.dur = std::max<uint32_t>(1, a.duration);
  t.curve = a.curve; t.bounce = a.bounce; t.upd = std::move(upd); t.done = std::move(done);
  if (t.upd) t.upd(from);
  tw_.push_back(std::move(t));
}

void Animator::step(uint32_t now) {
  std::vector<std::function<void()>> finished;
  for (size_t i = 0; i < tw_.size();) {
    Tween& t = tw_[i];
    if ((int32_t)(now - t.start) < 0) { i++; continue; }
    float p = std::min(1.f, (float)(now - t.start) / (float)t.dur);
    float v = t.from + (t.to - t.from) * evalCurve(t.curve, p, t.bounce);
    if (p >= 1.f) v = t.to;
    if (t.target) *t.target = v;
    if (t.upd) t.upd(v);
    if (p >= 1.f) {
      if (t.done) finished.push_back(std::move(t.done));
      tw_.erase(tw_.begin() + i);
    } else i++;
  }
  for (auto& f : finished) f();
}

}  // namespace fk
