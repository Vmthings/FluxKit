#include "App.h"
#include <cstring>
#include <unordered_map>
#include "Node.h"
#include "Platform.h"

namespace fk {
namespace {

struct ScreenRec {
  std::string name;
  View root;
  Layer* layer = nullptr;
  Layer* backdrop = nullptr;
  std::vector<Layer*> levels;
};
struct PopupRec {
  std::string name;
  View content;
  PopupStyle style = PopupStyle::Alert;
  bool tapOut = true;
  Layer* layer = nullptr;
  Layer* contentLayer = nullptr;
};
struct TouchState {
  bool down = false, scrolling = false;
  int sx = 0, sy = 0, lastX = 0, lastY = 0;
  Layer* target = nullptr;
  Layer* scroller = nullptr;
  uint32_t lastT = 0;
  float vel = 0;
};

struct G {
  Config cfg;
  Compositor comp;
  Animator anim;
  std::vector<ScreenRec> screens;
  std::vector<PopupRec> popups;
  std::unordered_map<std::string, Layer*> ids;
  Layer* screensRoot = nullptr;
  Layer* popupsRoot = nullptr;
  int cur = -1, openPopup = -1;
  bool popupBusy = false, began = false;
  std::vector<std::pair<int, Transition>> stack;
  TouchState ts;
  uint32_t lastUpdate = 0;
  int W = 0, H = 0;
};
G& g() { static G x; return x; }

void resetProps(Layer* l) { l->tx = l->ty = 0; l->scale = 1.f; l->opacity = l->restOpacity; }

Layer* container(Layer* parent, int w, int h, bool visible) {
  auto L = std::make_unique<Layer>();
  L->w = w; L->h = h; L->visible = visible;
  L->absX = parent->absX; L->absY = parent->absY;
  return parent->add(std::move(L));
}

void applyAppear(Layer* l) {
  if (l->hasAppear) {
    const Appear& a = l->appear;
    float rest = l->restOpacity;
    l->tx = a.dx; l->ty = a.dy; l->scale = a.scale; l->opacity = rest * a.opacity;
    auto& an = g().anim;
    if (a.dx != 0) an.animate(&l->tx, 0.f, a.anim);
    if (a.dy != 0) an.animate(&l->ty, 0.f, a.anim);
    if (a.scale != 1.f) an.animate(&l->scale, 1.f, a.anim);
    if (a.opacity != 1.f) an.animate(&l->opacity, rest, Animation::easeOut(a.anim.duration).delay(a.anim.delayMs));
  }
  for (auto& k : l->kids) applyAppear(k.get());
}

void setLevels(const std::vector<Layer*>& lv, float t) {
  int n = (int)lv.size();
  for (int k = 0; k < n; k++) lv[k]->opacity = std::clamp(t * n - k, 0.f, 1.f);
}

void bakeBackdrop(ScreenRec& s, Surface& snap) {
  auto& G = g();
  int N = std::max(1, G.cfg.blurSteps), f = std::max(1, G.cfg.backdropDownscale);
  auto small = raster::downscale(snap, f);
  s.backdrop = container(s.layer, G.W, G.H, false);
  for (int k = 1; k <= N; k++) {
    auto lvl = std::make_shared<Surface>(small->w, small->h);
    if (lvl->data) memcpy(lvl->data, small->data, small->bytes());
    raster::boxBlur(*lvl, std::max(1, (int)std::lround(G.cfg.blurRadius / (float)f * k / N)), 3);
    raster::tint(*lvl, Color(0x000000, (uint8_t)std::lround(std::clamp(G.cfg.scrim, 0.f, 1.f) * 255 * k / N)));
    // Upscale ONCE here (boot), store as dithered RGB565: at runtime a level is a memcpy / integer blend.
    auto big = raster::resize(*lvl, G.W, G.H);
    auto L = std::make_unique<Layer>();
    L->w = G.W; L->h = G.H;
    L->opaque = true;
    L->p565 = raster::toRGB565(*big);
    L->opacity = 0.f;
    s.levels.push_back(s.backdrop->add(std::move(L)));
  }
}

void hideScreen(int i) {
  auto& s = g().screens[i];
  s.layer->visible = false;
  resetProps(s.layer);
}

void switchTo(int ni, Transition t, bool rev) {
  auto& G = g();
  if (ni == G.cur || ni < 0 || ni >= (int)G.screens.size()) return;
  int oi = G.cur;
  G.cur = ni;
  Layer* nl = G.screens[ni].layer;
  resetProps(nl);
  nl->visible = true;
  if (oi < 0 || t == Transition::None) {
    if (oi >= 0) hideScreen(oi);
    applyAppear(nl);
    return;
  }
  Layer* ol = G.screens[oi].layer;
  float W = (float)G.W, H = (float)G.H;
  auto fin = [ol]() { ol->visible = false; resetProps(ol); };
  auto& an = G.anim;
  switch (t) {
    case Transition::Fade:
      G.comp.bringToFront(nl);
      nl->opacity = 0.f;
      an.animate(&nl->opacity, 1.f, Animation::easeOut(300), fin);
      break;
    case Transition::Push:
      if (!rev) {
        G.comp.bringToFront(nl);
        nl->tx = W;
        an.animate(&nl->tx, 0.f, Animation::easeOut(380), fin);
        an.animate(&ol->tx, -W * 0.28f, Animation::easeOut(380));
      } else {
        G.comp.bringToFront(ol);
        nl->tx = -W * 0.28f;
        an.animate(&nl->tx, 0.f, Animation::easeOut(380));
        an.animate(&ol->tx, W, Animation::easeOut(380), fin);
      }
      break;
    case Transition::Cover:
      if (!rev) {
        G.comp.bringToFront(nl);
        nl->ty = H;
        an.animate(&nl->ty, 0.f, Animation::easeOut(400), fin);
      } else {
        G.comp.bringToFront(ol);
        an.animate(&ol->ty, H, Animation::easeIn(320), fin);
      }
      break;
    default: break;
  }
  applyAppear(nl);
}

void releaseFeedback(Layer* l) {
  if (l && l->onPress) { l->onPress(false); return; }
  if (l && l->pressFeedback) {
    l->frame = 0;
    g().anim.animate(&l->scale, 1.f, Animation::spring(300, 0.3f));
  }
}

bool isDescendant(Layer* l, Layer* anc) {
  for (; l; l = l->parent) if (l == anc) return true;
  return false;
}

void localPos(Layer* l, int x, int y, float& lx, float& ly) {
  lx = (x - l->wx) / l->ws; ly = (y - l->wy) / l->ws;
}

void handleTouch(uint32_t now) {
  auto& G = g();
  auto& T = G.ts;
  int x = 0, y = 0;
  bool t = platform::touch(x, y);
  float lx, ly;
  if (t && !T.down) {
    T = TouchState{};
    T.down = true; T.sx = x; T.sy = y; T.lastX = x; T.lastY = y; T.lastT = now;
    T.target = G.comp.hitTest(x, y);
    const auto& ord = G.comp.order();
    for (size_t i = ord.size(); i-- > 0;) {
      Layer* l = ord[i];
      if (l->scrollable && l->wvis && l->rect.contains(x, y)) { T.scroller = l; break; }
    }
    if (T.scroller && T.target && !isDescendant(T.target, T.scroller)) T.scroller = nullptr;
    if (T.scroller) T.scroller->scrollVel = 0;
    if (T.target) {
      if (T.target->onTouch) { localPos(T.target, x, y, lx, ly); T.target->onTouch(0, lx, ly); }
      if (T.target->onPress) T.target->onPress(true);
      else if (T.target->pressFeedback) {
        if (T.target->frames.size() > 1) T.target->frame = 1;
        G.anim.animate(&T.target->scale, 0.97f, Animation::easeOut(90));
      }
    }
  } else if (t && T.down) {
    if (T.scroller && !T.scrolling && !(T.target && T.target->onTouch) && std::abs(y - T.sy) > 10) {
      T.scrolling = true;
      releaseFeedback(T.target);
    }
    if (T.scrolling && T.scroller->scrollContent) {
      Layer* c = T.scroller->scrollContent;
      float dy = (float)(y - T.lastY);
      c->ty = std::clamp(c->ty + dy, T.scroller->scrollMin, T.scroller->scrollMax);
      float dt = std::max(1.f, (float)(now - T.lastT));
      T.vel = 0.6f * T.vel + 0.4f * (dy * 1000.f / dt);
    } else if (T.target && T.target->onTouch) {
      localPos(T.target, x, y, lx, ly);
      T.target->onTouch(1, lx, ly);
    }
    T.lastX = x; T.lastY = y; T.lastT = now;
  } else if (!t && T.down) {
    Layer* tg = T.target;
    if (T.scrolling) {
      if (T.scroller) T.scroller->scrollVel = T.vel;
    } else if (tg) {
      if (tg->onTouch) { localPos(tg, T.lastX, T.lastY, lx, ly); tg->onTouch(2, lx, ly); }
      releaseFeedback(tg);
      std::function<void()> tap = tg->onTap;
      if (tap && tg->rect.contains(T.lastX, T.lastY)) tap();
    }
    T.down = false; T.scrolling = false; T.target = nullptr; T.scroller = nullptr;
  }
}

void stepPlayers(uint32_t now) {
  for (Layer* l : g().comp.order()) {
    if (l->fps > 0 && l->frames.size() > 1 && l->wvis) {
      if (!l->playStart) l->playStart = now ? now : 1;
      l->frame = (int)(((uint64_t)(now - l->playStart) * (uint64_t)(l->fps * 1000) / 1000000ULL) % l->frames.size());
    }
  }
}

void stepFling(float dt) {
  for (Layer* l : g().comp.order()) {
    if (!l->scrollable || !l->scrollContent || std::fabs(l->scrollVel) < 8.f) continue;
    if (g().ts.down && g().ts.scroller == l) continue;
    Layer* c = l->scrollContent;
    float ny = c->ty + l->scrollVel * dt / 1000.f;
    if (ny < l->scrollMin || ny > l->scrollMax) { ny = std::clamp(ny, l->scrollMin, l->scrollMax); l->scrollVel = 0; }
    c->ty = ny;
    l->scrollVel *= std::exp(-dt * 0.0035f);
  }
}

}  // namespace

// ----------------------------------------------------------------- public API
Animator& animator() { return g().anim; }
uint32_t screenWidth() { return (uint32_t)g().W; }
uint32_t screenHeight() { return (uint32_t)g().H; }
size_t memoryUsed() { return Surface::totalBytes(); }

void begin(const Config& cfg) {
  auto& G = g();
  G.cfg = cfg;
  platform::init(cfg.rotation);
  G.W = platform::width(); G.H = platform::height();
  G.comp.init(G.W, G.H);
  G.comp.profile = cfg.profile;
  G.screensRoot = container(&G.comp.root, G.W, G.H, true);
  G.popupsRoot = container(&G.comp.root, G.W, G.H, true);
  G.began = true;
  // touch the built-in family so a broken font shows up immediately
  if (!montserrat(Weight::Regular)->ok()) platform::log("built-in Montserrat failed to load");
}

void screen(const std::string& name, View root) {
  g().screens.push_back(ScreenRec{name, root});
}

void popup(const std::string& name, View content, PopupStyle style, bool tapOut) {
  PopupRec p;
  p.name = name; p.content = content; p.style = style; p.tapOut = tapOut;
  g().popups.push_back(std::move(p));
}

void bake() {
  auto& G = g();
  if (!G.began) begin();
  uint32_t t0 = platform::millis();
  BakeEnv env;
  env.comp = &G.comp;
  env.ids = &G.ids;
  std::unique_ptr<Surface> firstSnap;

  for (size_t i = 0; i < G.screens.size(); i++) {
    auto& s = G.screens[i];
    s.layer = container(G.screensRoot, G.W, G.H, false);
    s.layer->name = "screen:" + s.name;
    auto node = s.root.node();
    node->measure(Size{G.W, G.H});
    node->place(Rect{0, 0, G.W, G.H});
    node->bake(env, s.layer);
    if (!G.popups.empty()) {
      s.layer->visible = true;
      auto snap = std::make_unique<Surface>(G.W, G.H);
      G.comp.snapshot(*snap);
      s.layer->visible = false;
      bakeBackdrop(s, *snap);
      if (i == 0) firstSnap = std::move(snap);
    }
    if (G.cfg.log) platform::log("baked screen '%s'", s.name.c_str());
  }

  env.backdrop = firstSnap.get();
  for (auto& p : G.popups) {
    p.layer = container(G.popupsRoot, G.W, G.H, false);
    auto catcher = std::make_unique<Layer>();
    catcher->w = G.W; catcher->h = G.H; catcher->hit = true;
    if (p.tapOut) catcher->onTap = []() { dismiss(); };
    p.layer->add(std::move(catcher));
    auto node = p.content.node();
    Size avail = p.style == PopupStyle::Alert ? Size{G.W - 80, G.H - 160} : Size{G.W, G.H};
    Size sz = node->measure(avail);
    Rect r;
    switch (p.style) {
      case PopupStyle::Alert: r = Rect{(G.W - sz.w) / 2, (G.H - sz.h) / 2, sz.w, sz.h}; break;
      case PopupStyle::Sheet: r = Rect{0, G.H - sz.h, G.W, sz.h}; break;
      case PopupStyle::Fullscreen: r = Rect{0, 0, G.W, G.H}; break;
    }
    node->place(r);
    p.contentLayer = node->bake(env, p.layer);
    p.contentLayer->hit = true;  // absorbs touches so the "tap outside" catcher doesn't fire
    if (G.cfg.log) platform::log("baked popup '%s'", p.name.c_str());
  }
  firstSnap.reset();

  G.comp.invalidateAll();
  if (G.cur < 0 && !G.screens.empty()) {
    G.cur = 0;
    G.screens[0].layer->visible = true;
    applyAppear(G.screens[0].layer);
  }
  if (G.cfg.log)
    platform::log("bake done in %u ms, %u KB of pre-rendered surfaces", (unsigned)(platform::millis() - t0), (unsigned)(Surface::totalBytes() / 1024));
}

void show(const std::string& name, Transition t) {
  auto& G = g();
  for (size_t i = 0; i < G.screens.size(); i++)
    if (G.screens[i].name == name) { G.stack.clear(); switchTo((int)i, t, false); return; }
  platform::log("show: unknown screen '%s'", name.c_str());
}

void push(const std::string& name, Transition t) {
  auto& G = g();
  for (size_t i = 0; i < G.screens.size(); i++)
    if (G.screens[i].name == name) { G.stack.push_back({G.cur, t}); switchTo((int)i, t, false); return; }
  platform::log("push: unknown screen '%s'", name.c_str());
}

bool pop() {
  auto& G = g();
  if (G.stack.empty()) return false;
  auto e = G.stack.back();
  G.stack.pop_back();
  switchTo(e.first, e.second, true);
  return true;
}

bool popupOpen() { return g().openPopup >= 0; }

void present(const std::string& name) {
  auto& G = g();
  if (G.openPopup >= 0 || G.popupBusy || G.cur < 0) return;
  for (size_t i = 0; i < G.popups.size(); i++) {
    if (G.popups[i].name != name) continue;
    PopupRec& p = G.popups[i];
    ScreenRec& s = G.screens[G.cur];
    G.openPopup = (int)i;
    p.layer->visible = true;
    resetProps(p.contentLayer);
    if (s.backdrop) {
      s.backdrop->visible = true;
      std::vector<Layer*> lv = s.levels;
      G.anim.animateValue(0.f, 1.f, Animation::easeOut(300), [lv](float t) { setLevels(lv, t); });
    }
    Layer* c = p.contentLayer;
    switch (p.style) {
      case PopupStyle::Alert:
        c->scale = 0.9f; c->opacity = 0.f;
        G.anim.animate(&c->scale, 1.f, Animation::spring(460, 0.28f));
        G.anim.animate(&c->opacity, c->restOpacity, Animation::easeOut(180));
        break;
      case PopupStyle::Sheet:
        c->ty = (float)(c->h + 40);
        G.anim.animate(&c->ty, 0.f, Animation::spring(520, 0.12f));
        break;
      case PopupStyle::Fullscreen:
        c->opacity = 0.f;
        G.anim.animate(&c->opacity, c->restOpacity, Animation::easeOut(260));
        break;
    }
    return;
  }
  platform::log("present: unknown popup '%s'", name.c_str());
}

void dismiss() {
  auto& G = g();
  if (G.openPopup < 0 || G.cur < 0) return;
  PopupRec& p = G.popups[G.openPopup];
  ScreenRec& s = G.screens[G.cur];
  G.openPopup = -1;
  G.popupBusy = true;
  Layer* c = p.contentLayer;
  Layer* pl = p.layer;
  Layer* bd = s.backdrop;
  std::vector<Layer*> lv = s.levels;
  auto done = [c, pl, bd]() {
    pl->visible = false;
    resetProps(c);
    if (bd) bd->visible = false;
    g().popupBusy = false;
  };
  if (bd) G.anim.animateValue(1.f, 0.f, Animation::easeIn(220), [lv](float t) { setLevels(lv, t); });
  switch (p.style) {
    case PopupStyle::Alert:
      G.anim.animate(&c->scale, 0.94f, Animation::easeIn(180));
      G.anim.animate(&c->opacity, 0.f, Animation::easeIn(180), done);
      break;
    case PopupStyle::Sheet:
      G.anim.animate(&c->ty, (float)(c->h + 40), Animation::easeIn(260), done);
      break;
    case PopupStyle::Fullscreen:
      G.anim.animate(&c->opacity, 0.f, Animation::easeIn(200), done);
      break;
  }
}

void update() {
  auto& G = g();
  platform::pump();
  uint32_t now = platform::millis();
  float dt = G.lastUpdate ? (float)(now - G.lastUpdate) : 16.f;
  G.lastUpdate = now;
  handleTouch(now);
  G.anim.step(now);
  stepFling(dt);
  stepPlayers(now);
  Rect d;
  if (G.comp.prepare(d)) G.comp.render(d);
}

// ----------------------------------------------------------------- LayerRef
LayerRef layer(const std::string& id) {
  auto& m = g().ids;
  auto it = m.find(id);
  return LayerRef(it == m.end() ? nullptr : it->second);
}
LayerRef& LayerRef::offset(float dx, float dy, const Animation& a) {
  if (l_) { animator().animate(&l_->tx, dx, a); animator().animate(&l_->ty, dy, a); }
  return *this;
}
LayerRef& LayerRef::scale(float s, const Animation& a) { if (l_) animator().animate(&l_->scale, s, a); return *this; }
LayerRef& LayerRef::opacity(float o, const Animation& a) { if (l_) animator().animate(&l_->opacity, o, a); return *this; }
LayerRef& LayerRef::reveal(float f, const Animation& a) { if (l_) animator().animate(&l_->reveal, std::clamp(f, 0.f, 1.f), a); return *this; }
LayerRef& LayerRef::frame(int i) { if (l_ && !l_->frames.empty()) l_->frame = std::clamp(i, 0, (int)l_->frames.size() - 1); return *this; }
LayerRef& LayerRef::play(float fps) { if (l_) { l_->fps = fps; l_->playStart = platform::millis(); } return *this; }
LayerRef& LayerRef::enabled(bool on) { if (l_ && l_->setEnabled) l_->setEnabled(on); return *this; }
LayerRef& LayerRef::visible(bool v) { if (l_) l_->visible = v; return *this; }

}  // namespace fk
