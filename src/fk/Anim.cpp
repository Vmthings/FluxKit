#include "Anim.h"
#include <cstring>
#include "Platform.h"

namespace fk {

static float bezierEase(float x, float x1, float y1, float x2, float y2) {
  auto bx = [&](float s) { float u = 1.f - s; return 3.f * u * u * s * x1 + 3.f * u * s * s * x2 + s * s * s; };
  auto by = [&](float s) { float u = 1.f - s; return 3.f * u * u * s * y1 + 3.f * u * s * s * y2 + s * s * s; };
  auto dbx = [&](float s) { float u = 1.f - s; return 3.f * u * u * x1 + 6.f * u * s * (x2 - x1) + 3.f * s * s * (1.f - x2); };
  float s = x;
  for (int i = 0; i < 8; i++) {  // Newton
    float e = bx(s) - x, d = dbx(s);
    if (std::fabs(e) < 1e-6f || std::fabs(d) < 1e-6f) break;
    s -= e / d;
  }
  if (s < 0.f || s > 1.f || std::fabs(bx(s) - x) > 1e-4f) {  // bisection fallback
    float lo = 0.f, hi = 1.f; s = x;
    for (int i = 0; i < 30; i++) { s = 0.5f * (lo + hi); if (bx(s) < x) lo = s; else hi = s; }
  }
  return by(s);
}

float evalCurve(Curve c, float t, float bounce, const float* bz) {
  t = std::clamp(t, 0.f, 1.f);
  switch (c) {
    case Curve::Linear: return t;
    case Curve::EaseIn: return t * t * t;
    case Curve::EaseOut: { float u = 1.f - t; return 1.f - u * u * u; }
    case Curve::EaseInOut: return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
    case Curve::CubicBezier:
      return bz ? bezierEase(t, bz[0], bz[1], bz[2], bz[3]) : t;
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
  t.curve = a.curve; t.bounce = a.bounce; memcpy(t.bz, a.bz, sizeof t.bz); t.done = std::move(done);
  tw_.push_back(std::move(t));
}

void Animator::animateValue(float from, float to, const Animation& a, std::function<void(float)> upd, std::function<void()> done) {
  Tween t;
  t.from = from; t.to = to;
  t.start = platform::millis() + a.delayMs; t.dur = std::max<uint32_t>(1, a.duration);
  t.curve = a.curve; t.bounce = a.bounce; memcpy(t.bz, a.bz, sizeof t.bz); t.upd = std::move(upd); t.done = std::move(done);
  if (t.upd) t.upd(from);
  tw_.push_back(std::move(t));
}

void Animator::step(uint32_t now) {
  std::vector<std::function<void()>> finished;
  for (size_t i = 0; i < tw_.size();) {
    Tween& t = tw_[i];
    if ((int32_t)(now - t.start) < 0) { i++; continue; }
    float p = std::min(1.f, (float)(now - t.start) / (float)t.dur);
    float v = t.from + (t.to - t.from) * evalCurve(t.curve, p, t.bounce, t.bz);
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
