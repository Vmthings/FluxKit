#include <cstring>
#include "App.h"
#include "Node.h"
#include "Platform.h"

namespace fk {

Theme& theme() { static Theme t; return t; }

// =================================================================== Node
Size Node::measure(Size avail) {
  int aw = avail.w >= Unbounded ? Unbounded : std::max(0, avail.w - mar.horizontal());
  int ah = avail.h >= Unbounded ? Unbounded : std::max(0, avail.h - mar.vertical());
  int bw = fw >= 0 ? fw : aw, bh = fh >= 0 ? fh : ah;
  Size ca{bw >= Unbounded ? Unbounded : std::max(0, bw - pad.horizontal()),
          bh >= Unbounded ? Unbounded : std::max(0, bh - pad.vertical())};
  Size cs = contentSize(ca);
  Size b{cs.w + pad.horizontal(), cs.h + pad.vertical()};
  if (fw >= 0) b.w = fw; else if (flexW() && aw < Unbounded) b.w = aw;
  if (fh >= 0) b.h = fh; else if (flexH() && ah < Unbounded) b.h = ah;
  return Size{b.w + mar.horizontal(), b.h + mar.vertical()};
}

void Node::place(Rect o) {
  frame = Rect{o.x + mar.l, o.y + mar.t, std::max(0, o.w - mar.horizontal()), std::max(0, o.h - mar.vertical())};
  placeContent(contentRect());
}

Font Node::resolvedFont() const {
  for (const Node* n = this; n; n = n->parent) if (n->hasFont) return n->font;
  return Font::regular(17);
}
Color Node::resolvedFg() const {
  for (const Node* n = this; n; n = n->parent) if (n->hasFg) return n->fg;
  return theme().text;
}
float Node::cornerRadius() const {
  if (shape == Shape::Circle || shape == Shape::Capsule) return std::min(frame.w, frame.h) * 0.5f;
  return radius;
}
Rect Node::layerBounds() const {
  Rect b = frame;
  if (hasShadow) b = b.outset(raster::shadowMargin(shadow));
  return b;
}

void Node::paintSelf(Surface& s, int ox, int oy, BakeEnv& env) {
  float x = (float)(frame.x - ox), y = (float)(frame.y - oy), w = (float)frame.w, h = (float)frame.h;
  float r = cornerRadius();
  if (hasShadow && (hasBg || hasMat)) raster::shadow(s, x, y, w, h, r, shadow);
  if (hasMat) {
    const Surface* src = env.backdrop ? env.backdrop : &s;
    int bx = env.backdrop ? 0 : ox, by = env.backdrop ? 0 : oy;
    raster::material(s, frame.x - ox, frame.y - oy, frame.w, frame.h, r, mat, *src, bx, by, frame.x, frame.y);
  }
  if (hasBg) raster::fillRoundRect(s, x, y, w, h, r, bg);
  if (hasBorder) raster::strokeRoundRect(s, x, y, w, h, r, borderW, borderColor);
  paintContent(s, ox, oy, env);
}

void Node::paintTree(Surface& s, int ox, int oy, BakeEnv& env, std::vector<Node*>& deferred) {
  paintSelf(s, ox, oy, env);
  for (auto& k : kids) {
    if (k->isLayerRoot()) deferred.push_back(k.get());
    else k->paintTree(s, ox, oy, env, deferred);
  }
}

Layer* Node::makeLayer(BakeEnv& env, Layer* parent, Rect b, const char*) {
  auto L = std::make_unique<Layer>();
  L->x = b.x - parent->absX; L->y = b.y - parent->absY;
  L->w = b.w; L->h = b.h; L->absX = b.x; L->absY = b.y;
  L->opacity = L->restOpacity = opacity;
  L->name = id;
  if (hasAppear) { L->hasAppear = true; L->appear = appear; }
  Layer* raw = parent->add(std::move(L));
  if (!id.empty() && env.ids) (*env.ids)[id] = raw;
  return raw;
}

Layer* Node::bakeGeneric(BakeEnv& env, Layer* parent) {
  Rect b = layerBounds();
  auto surf = std::make_shared<Surface>(b.w, b.h);
  std::vector<Node*> deferred;
  paintTree(*surf, b.x, b.y, env, deferred);
  Layer* L = makeLayer(env, parent, b);
  L->frames.push_back(surf);
  L->opaque = surf->isOpaque();
  if (L->opaque && !tap && allowCompact()) {  // opaque static layer: half the memory, memcpy-fast compositing
    L->p565 = raster::toRGB565(*surf);
    L->frames.clear();
  }
  if (tap) {
    if (surf->data) {
      auto dark = std::make_shared<Surface>(b.w, b.h);
      memcpy(dark->data, surf->data, surf->bytes());
      raster::dim(*dark, 0.82f);  // pre-rendered "pressed" state
      L->frames.push_back(dark);
    }
    L->pressFeedback = true;
    L->hit = true;
    L->onTap = tap;
  }
  for (Node* d : deferred) d->bake(env, L);
  return L;
}

Layer* Node::bakeBox(BakeEnv& env, Layer* parent) {
  Rect b = layerBounds();
  Layer* L = makeLayer(env, parent, b);
  if (hasBg || hasBorder || hasMat || hasShadow) {
    auto surf = std::make_shared<Surface>(b.w, b.h);
    paintSelf(*surf, b.x, b.y, env);
    L->opaque = surf->isOpaque();
    if (L->opaque) L->p565 = raster::toRGB565(*surf); else L->frames.push_back(surf);
  }
  L->w = b.w; L->h = b.h;
  return L;
}

// =================================================================== containers
class StackNode : public Node {
 public:
  bool vertical;
  explicit StackNode(bool v) : vertical(v) {}
  void add(const std::shared_ptr<Node>& c) { c->parent = this; kids.push_back(c); }
  int stackAxis() const override { return vertical ? 1 : 0; }
  int mainOf(Size s) const { return vertical ? s.h : s.w; }
  int crossOf(Size s) const { return vertical ? s.w : s.h; }
  Size mk(int main, int cross) const { return vertical ? Size{cross, main} : Size{main, cross}; }
  bool flexMain(Node* k) const { return vertical ? k->flexH() : k->flexW(); }
  bool flexCross(Node* k) const { return vertical ? k->flexW() : k->flexH(); }
  bool contentFlexW() const override { for (auto& k : kids) if (k->flexW()) return true; return false; }
  bool contentFlexH() const override { for (auto& k : kids) if (k->flexH()) return true; return false; }

  Size contentSize(Size ca) override {
    int n = (int)kids.size(), used = 0, cross = 0;
    bool anyFlex = false;
    for (auto& k : kids) {
      Size s = k->measure(ca);
      if (flexMain(k.get())) anyFlex = true; else used += mainOf(s);
      cross = std::max(cross, crossOf(s));
    }
    used += spacing * std::max(0, n - 1);
    int cm = mainOf(ca);
    return mk((anyFlex && cm < Unbounded) ? cm : used, cross);
  }

  void placeContent(Rect cr) override {
    int n = (int)kids.size();
    if (!n) return;
    std::vector<Size> sz(n);
    int used = 0, flexN = 0;
    for (int i = 0; i < n; i++) {
      if (flexMain(kids[i].get())) flexN++;
      else { sz[i] = kids[i]->measure(Size{cr.w, cr.h}); used += mainOf(sz[i]); }
    }
    int crMain = mainOf(Size{cr.w, cr.h}), crCross = crossOf(Size{cr.w, cr.h});
    int left = std::max(0, crMain - used - spacing * (n - 1));
    int per = flexN ? left / flexN : 0, rem = flexN ? left % flexN : 0;
    int pos = vertical ? cr.y : cr.x;
    for (int i = 0; i < n; i++) {
      Node* k = kids[i].get();
      bool fm = flexMain(k);
      int m = fm ? per + (rem-- > 0 ? 1 : 0) : mainOf(sz[i]);
      Size ms = fm ? k->measure(mk(m, crCross)) : sz[i];
      int c = flexCross(k) ? crCross : crossOf(ms);
      int off = crossAlign == Align::Leading ? 0 : crossAlign == Align::Center ? (crCross - c) / 2 : crCross - c;
      Rect r = vertical ? Rect{cr.x + off, pos, c, m} : Rect{pos, cr.y + off, m, c};
      k->place(r);
      pos += m + spacing;
    }
  }
};

class ZStackNode : public Node {
 public:
  void add(const std::shared_ptr<Node>& c) { c->parent = this; kids.push_back(c); }
  bool contentFlexW() const override { for (auto& k : kids) if (k->flexW()) return true; return false; }
  bool contentFlexH() const override { for (auto& k : kids) if (k->flexH()) return true; return false; }
  Size contentSize(Size ca) override {
    int w = 0, h = 0;
    for (auto& k : kids) { Size s = k->measure(ca); w = std::max(w, s.w); h = std::max(h, s.h); }
    return Size{w, h};
  }
  static int off(Align a, int space) { return a == Align::Leading ? 0 : a == Align::Center ? space / 2 : space; }
  void placeContent(Rect cr) override {
    for (auto& k : kids) {
      Size s = k->measure(Size{cr.w, cr.h});
      int w = k->flexW() ? cr.w : s.w, h = k->flexH() ? cr.h : s.h;
      k->place(Rect{cr.x + off(zH, cr.w - w), cr.y + off(zV, cr.h - h), w, h});
    }
  }
};

// =================================================================== leaves
class SpacerNode : public Node {
 public:
  int minLen = 0;
  bool contentFlexW() const override { return parent && parent->stackAxis() == 0; }
  bool contentFlexH() const override { return parent && parent->stackAxis() == 1; }
  Size contentSize(Size) override { return (parent && parent->stackAxis() == 0) ? Size{minLen, 0} : Size{0, minLen}; }
};

class ShapeNode : public Node {
 public:
  bool contentFlexW() const override { return true; }
  bool contentFlexH() const override { return true; }
};

class TextNode : public Node {
 public:
  std::vector<std::string> variants;
  int paintIndex = 0;

  std::vector<std::string> linesOf(const Font& f, const std::string& s, int w) const {
    auto lines = text::layout(f, s, w);
    if (lineLimit > 0 && (int)lines.size() > lineLimit) lines.resize(lineLimit);
    return lines;
  }
  Size contentSize(Size ca) override {
    Font f = resolvedFont();
    int w = 0, h = 0;
    for (auto& v : variants) {
      auto lines = linesOf(f, v, ca.w);
      for (auto& l : lines) w = std::max(w, (int)std::ceil(text::measure(f, l)));
      h = std::max(h, (int)lines.size() * text::lineHeight(f));
    }
    return Size{w, h};
  }
  bool isLayerRoot() const override { return Node::isLayerRoot() || variants.size() > 1; }
  bool allowCompact() const override { return variants.size() <= 1; }
  void paintContent(Surface& s, int ox, int oy, BakeEnv&) override {
    Font f = resolvedFont();
    Color c = resolvedFg();
    Rect cr = contentRect();
    auto lines = linesOf(f, variants[paintIndex], cr.w);
    int lh = text::lineHeight(f), asc = text::ascent(f), blockW = 0;
    for (auto& l : lines) blockW = std::max(blockW, (int)std::ceil(text::measure(f, l)));
    int blockH = (int)lines.size() * lh;
    float x0 = (float)(cr.x - ox) + std::floor((cr.w - blockW) * 0.5f);
    float y0 = (float)(cr.y - oy) + std::floor((cr.h - blockH) * 0.5f);
    for (size_t i = 0; i < lines.size(); i++) {
      float lw = text::measure(f, lines[i]);
      float lx = x0 + (textAlign == Align::Leading ? 0 : textAlign == Align::Center ? (blockW - lw) * 0.5f : blockW - lw);
      text::draw(s, f, lines[i], std::round(lx), y0 + (float)(i * lh + asc), c);
    }
  }
  Layer* bake(BakeEnv& env, Layer* parent) override {
    Layer* L = bakeGeneric(env, parent);
    if (variants.size() > 1) {
      L->frames.resize(1); L->pressFeedback = false;  // text states and tap feedback don't combine
      Rect b = layerBounds();
      for (size_t i = 1; i < variants.size(); i++) {
        paintIndex = (int)i;
        auto surf = std::make_shared<Surface>(b.w, b.h);
        std::vector<Node*> dummy;
        paintTree(*surf, b.x, b.y, env, dummy);
        L->frames.push_back(surf);
      }
      paintIndex = 0;
    }
    return L;
  }
};

class ImageNode : public Node {
 public:
  SurfacePtr img;
  SurfacePtr scaled;
  Size contentSize(Size) override { return img ? Size{img->w, img->h} : Size{}; }
  void paintContent(Surface& s, int ox, int oy, BakeEnv&) override {
    Rect cr = contentRect();
    if (!img || cr.empty()) return;
    SurfacePtr src = img;
    if (img->w != cr.w || img->h != cr.h) src = raster::resize(*img, cr.w, cr.h);
    if (radius > 0 || shape != Shape::None) {
      auto copy = std::make_shared<Surface>(src->w, src->h);
      memcpy(copy->data, src->data, src->bytes());
      float r = (shape == Shape::None) ? radius : std::min(cr.w, cr.h) * 0.5f;
      raster::maskRoundRect(*copy, 0, 0, (float)cr.w, (float)cr.h, r);
      src = copy;
    }
    raster::blend(s, *src, cr.x - ox, cr.y - oy);
  }
};

// Pre-rendered frame sequence. Every frame is drawn at boot; playback is just switching layer frames.
class CanvasNode : public Node {
 public:
  int cw = 0, ch = 0, frames = 1;
  float fps = 0;
  std::function<void(Painter&, int, int)> draw;
  Size contentSize(Size) override { return Size{cw, ch}; }
  bool isLayerRoot() const override { return true; }
  Layer* bake(BakeEnv& env, Layer* parent) override {
    Rect b = layerBounds();
    Layer* L = makeLayer(env, parent, b);
    Rect cr = contentRect();
    for (int i = 0; i < std::max(1, frames); i++) {
      auto surf = std::make_shared<Surface>(b.w, b.h);
      paintSelf(*surf, b.x, b.y, env);
      auto cv = std::make_shared<Surface>(cr.w, cr.h);
      Painter p(*cv);
      if (draw) draw(p, i, std::max(1, frames));
      raster::blend(*surf, *cv, cr.x - b.x, cr.y - b.y);
      L->frames.push_back(surf);
    }
    L->fps = fps;
    L->hit = (bool)tap;
    L->onTap = tap;
    return L;
  }
};

static std::unique_ptr<Layer> newSubLayer(Layer* parent, SurfacePtr s, int lx, int ly, int lw, int lh) {
  auto L = std::make_unique<Layer>();
  L->x = lx; L->y = ly; L->w = lw; L->h = lh;
  L->absX = parent->absX + lx; L->absY = parent->absY + ly;
  if (s) { L->opaque = false; L->frames.push_back(std::move(s)); }
  return L;
}

class ToggleNode : public Node {
 public:
  bool on = false;
  std::function<void(bool)> cb;
  Size contentSize(Size) override { return Size{52, 32}; }
  bool isLayerRoot() const override { return true; }
  Layer* bake(BakeEnv& env, Layer* parent) override {
    Rect f = frame;
    Layer* L = makeLayer(env, parent, f);
    L->hit = true; L->pressFeedback = true;
    int w = f.w, h = f.h;
    auto offS = std::make_shared<Surface>(w, h);
    raster::fillRoundRect(*offS, 0, 0, (float)w, (float)h, h * 0.5f, Paint(theme().track));
    auto onS = std::make_shared<Surface>(w, h);
    raster::fillRoundRect(*onS, 0, 0, (float)w, (float)h, h * 0.5f, Paint(theme().accent));
    L->add(newSubLayer(L, offS, 0, 0, w, h));
    Layer* onL = L->add(newSubLayer(L, onS, 0, 0, w, h));
    int d = h - 6, m = 10;
    auto kS = std::make_shared<Surface>(d + 2 * m, d + 2 * m);
    Shadow sh; sh.radius = 3.f; sh.y = 2.f; sh.color = Color(0x000000, 90);
    raster::shadow(*kS, (float)m, (float)m, (float)d, (float)d, d * 0.5f, sh);
    raster::fillCircle(*kS, m + d * 0.5f, m + d * 0.5f, d * 0.5f, Paint(Color::White));
    Layer* knob = L->add(newSubLayer(L, kS, 3 - m, 3 - m, d + 2 * m, d + 2 * m));
    float travel = (float)(w - h);
    knob->tx = on ? travel : 0.f;
    onL->opacity = on ? 1.f : 0.f;
    L->onTap = [this, knob, onL, travel]() {
      on = !on;
      animator().animate(&knob->tx, on ? travel : 0.f, Animation::spring(420, 0.25f));
      animator().animate(&onL->opacity, on ? 1.f : 0.f, Animation::easeOut(200));
      if (cb) cb(on);
    };
    return L;
  }
};

class SliderNode : public Node {
 public:
  float mn = 0, mx = 1, value = 0;
  std::function<void(float)> cb;
  bool contentFlexW() const override { return true; }
  Size contentSize(Size ca) override { return Size{ca.w >= Unbounded ? 240 : 0, 44}; }
  bool isLayerRoot() const override { return true; }
  Layer* bake(BakeEnv& env, Layer* parent) override {
    Rect f = frame;
    Layer* L = makeLayer(env, parent, f);
    L->hit = true;
    int w = f.w, h = f.h, th = 6, kd = 28, m = 10;
    float usable = (float)std::max(1, w - kd);
    auto trk = std::make_shared<Surface>(w, h);
    raster::fillRoundRect(*trk, 0, (h - th) * 0.5f, (float)w, (float)th, th * 0.5f, Paint(theme().track));
    auto fil = std::make_shared<Surface>(w, h);
    raster::fillRoundRect(*fil, 0, (h - th) * 0.5f, (float)w, (float)th, th * 0.5f, Paint(theme().accent));
    L->add(newSubLayer(L, trk, 0, 0, w, h));
    Layer* fill = L->add(newSubLayer(L, fil, 0, 0, w, h));
    auto kS = std::make_shared<Surface>(kd + 2 * m, kd + 2 * m);
    Shadow sh; sh.radius = 3.f; sh.y = 2.f; sh.color = Color(0x000000, 100);
    raster::shadow(*kS, (float)m, (float)m, (float)kd, (float)kd, kd * 0.5f, sh);
    raster::fillCircle(*kS, m + kd * 0.5f, m + kd * 0.5f, kd * 0.5f, Paint(Color::White));
    Layer* knob = L->add(newSubLayer(L, kS, -m, (h - kd) / 2 - m, kd + 2 * m, kd + 2 * m));
    auto setT = [=](float t) {
      t = std::clamp(t, 0.f, 1.f);
      knob->tx = t * usable;
      fill->reveal = std::clamp((kd * 0.5f + t * usable) / (float)w, 0.f, 1.f);
    };
    setT((value - mn) / std::max(1e-6f, mx - mn));
    L->onTouch = [=](int, float lx, float) {
      float t = std::clamp((lx - kd * 0.5f) / usable, 0.f, 1.f);
      setT(t);
      if (cb) cb(mn + t * (mx - mn));
    };
    return L;
  }
};

class ProgressNode : public Node {
 public:
  float value = 0;
  bool contentFlexW() const override { return true; }
  Size contentSize(Size) override { return Size{0, 8}; }
  bool isLayerRoot() const override { return true; }
  Layer* bake(BakeEnv& env, Layer* parent) override {
    Rect f = frame;
    Layer* L = makeLayer(env, parent, f);
    int w = f.w, h = f.h;
    auto trk = std::make_shared<Surface>(w, h);
    raster::fillRoundRect(*trk, 0, 0, (float)w, (float)h, h * 0.5f, Paint(theme().track));
    auto fil = std::make_shared<Surface>(w, h);
    raster::fillRoundRect(*fil, 0, 0, (float)w, (float)h, h * 0.5f, hasBg ? bg : Paint(theme().accent));
    L->add(newSubLayer(L, trk, 0, 0, w, h));
    Layer* fill = L->add(newSubLayer(L, fil, 0, 0, w, h));
    fill->reveal = std::clamp(value, 0.f, 1.f);
    if (!id.empty() && env.ids) (*env.ids)[id] = fill;  // fk::layer(id).reveal(v) drives the fill
    return L;
  }
};

class ScrollNode : public Node {
 public:
  int contentH = 0;
  bool contentFlexW() const override { return true; }
  bool contentFlexH() const override { return true; }
  Size contentSize(Size ca) override {
    if (kids.empty()) return Size{};
    if (ca.h >= Unbounded || ca.w >= Unbounded) { Size s = kids[0]->measure(Size{ca.w, Unbounded}); return s; }
    return Size{};
  }
  void placeContent(Rect cr) override {
    if (kids.empty()) return;
    Size cs = kids[0]->measure(Size{cr.w, Unbounded});
    contentH = cs.h;
    kids[0]->place(Rect{cr.x, cr.y, cr.w, cs.h});
  }
  bool isLayerRoot() const override { return true; }
  Layer* bake(BakeEnv& env, Layer* parent) override {
    Layer* box = bakeBox(env, parent);
    box->clip = true; box->hit = true; box->scrollable = true;
    if (!kids.empty()) {
      box->scrollContent = kids[0]->bake(env, box);
      box->scrollMin = std::min(0.f, (float)(frame.h - contentH));
    }
    return box;
  }
};

// =================================================================== View modifiers
#define N (n_)
View View::padding(int a) const { N->pad = Insets{a, a, a, a}; return *this; }
View View::padding(int v, int h) const { N->pad = Insets{v, h, v, h}; return *this; }
View View::padding(int t, int l, int b, int r) const { N->pad = Insets{t, l, b, r}; return *this; }
View View::margin(int a) const { N->mar = Insets{a, a, a, a}; return *this; }
View View::margin(int v, int h) const { N->mar = Insets{v, h, v, h}; return *this; }
View View::frame(int w, int h) const { if (w >= 0) N->fw = w; if (h >= 0) N->fh = h; return *this; }
View View::width(int w) const { N->fw = w; return *this; }
View View::height(int h) const { N->fh = h; return *this; }
View View::fillWidth() const { N->fillW = true; return *this; }
View View::fillHeight() const { N->fillH = true; return *this; }
View View::fill() const { N->fillW = N->fillH = true; return *this; }
View View::spacing(int s) const { N->spacing = s; return *this; }
View View::align(Align a) const { N->crossAlign = a; return *this; }
View View::alignment(Align h, Align v) const { N->zH = h; N->zV = v; return *this; }
View View::background(Paint p) const { N->hasBg = true; N->bg = p; return *this; }
View View::fill(Paint p) const { N->hasBg = true; N->bg = p; return *this; }
View View::cornerRadius(float r) const { N->radius = r; return *this; }
View View::border(Color c, float w) const { N->hasBorder = true; N->borderColor = c; N->borderW = w; return *this; }
View View::shadow(Shadow s) const { N->hasShadow = true; N->shadow = s; return *this; }
View View::shadow(float radius, float y, Color c) const { Shadow s; s.radius = radius; s.y = y; s.color = c; return shadow(s); }
View View::material(float blur, Color tint) const { N->hasMat = true; N->mat = Material{blur, tint}; return *this; }
View View::opacity(float o) const { N->opacity = o; return *this; }
View View::font(Font f) const { N->hasFont = true; N->font = f; return *this; }
View View::font(const std::string& name, int size) const { return font(Font::named(name, size)); }
View View::foreground(Color c) const { N->hasFg = true; N->fg = c; return *this; }
View View::multilineAlign(Align a) const { N->textAlign = a; return *this; }
View View::lineLimit(int n) const { N->lineLimit = n; return *this; }
View View::onTap(std::function<void()> fn) const { N->tap = std::move(fn); return *this; }
View View::id(const std::string& s) const { N->id = s; return *this; }
View View::appear(Appear a) const { N->hasAppear = true; N->appear = a; return *this; }
View View::layer() const { N->forceLayer = true; return *this; }
#undef N

// =================================================================== factories
static View stack(bool vertical, std::vector<View>& kids) {
  auto n = std::make_shared<StackNode>(vertical);
  for (auto& k : kids) if (k.valid()) n->add(k.node());
  return View(n);
}
View VStack(std::vector<View> c) { return stack(true, c); }
View HStack(std::vector<View> c) { return stack(false, c); }
View ZStack(std::vector<View> c) {
  auto n = std::make_shared<ZStackNode>();
  for (auto& k : c) if (k.valid()) n->add(k.node());
  return View(n);
}
View ScrollView(View content) {
  auto n = std::make_shared<ScrollNode>();
  if (content.valid()) { content.node()->parent = n.get(); n->kids.push_back(content.node()); }
  return View(n);
}
std::vector<View> ForEach(int count, const std::function<View(int)>& make) {
  std::vector<View> v;
  for (int i = 0; i < count; i++) v.push_back(make(i));
  return v;
}

View Text(const std::string& s) {
  auto n = std::make_shared<TextNode>();
  n->variants = {s};
  return View(n);
}
View TextStates(std::vector<std::string> states) {
  auto n = std::make_shared<TextNode>();
  n->variants = states.empty() ? std::vector<std::string>{""} : std::move(states);
  return View(n);
}
View Image(SurfacePtr img, int w, int h) {
  auto n = std::make_shared<ImageNode>();
  n->img = std::move(img);
  if (w >= 0) n->fw = w;
  if (h >= 0) n->fh = h;
  return View(n);
}
View Canvas(int w, int h, int frames, std::function<void(Painter&, int, int)> draw, float fps) {
  auto n = std::make_shared<CanvasNode>();
  n->cw = w; n->ch = h; n->frames = frames; n->draw = std::move(draw); n->fps = fps;
  return View(n);
}
View Spinner(int size, float thickness) {
  auto n = std::make_shared<CanvasNode>();
  CanvasNode* raw = n.get();
  n->cw = n->ch = size; n->frames = 36; n->fps = 30;
  n->draw = [raw, size, thickness](Painter& p, int i, int count) {
    Color c = raw->hasFg ? raw->fg : theme().accent;
    float r = (size - thickness) * 0.5f, cx = size * 0.5f, cy = size * 0.5f;
    p.strokeCircle(cx, cy, r, thickness, theme().track);
    p.arc(cx, cy, r, thickness, -90.f + 360.f * i / count, 100.f, c);
  };
  return View(n);
}

View Spacer(int minLength) {
  auto n = std::make_shared<SpacerNode>();
  n->minLen = minLength;
  return View(n);
}
static View shape(Node::Shape s, float r = 0) {
  auto n = std::make_shared<ShapeNode>();
  n->shape = s; n->radius = r;
  return View(n);
}
View Rectangle() { return shape(Node::Shape::Rect); }
View RoundedRectangle(float r) { return shape(Node::Shape::Rect, r); }
View Circle() { return shape(Node::Shape::Circle); }
View Capsule() { return shape(Node::Shape::Capsule); }
View Divider() {
  auto n = std::make_shared<ShapeNode>();
  n->fh = 1; n->fillW = true; n->hasBg = true; n->bg = Paint(Color(0xFFFFFF, 36));
  return View(n);
}

View Button(View label, std::function<void()> action) {
  auto n = std::make_shared<ZStackNode>();  // centres its label like SwiftUI
  if (label.valid()) n->add(label.node());
  n->pad = Insets{14, 28, 14, 28};
  n->hasBg = true; n->bg = Paint(theme().accent); n->radius = 14.f;
  n->hasFont = true; n->font = Font::semibold(20);
  n->hasFg = true; n->fg = Color::White;
  n->tap = action ? std::move(action) : [] {};
  return View(n);
}
View Button(const std::string& label, std::function<void()> action) { return Button(Text(label), std::move(action)); }

View Toggle(bool on, std::function<void(bool)> cb) {
  auto n = std::make_shared<ToggleNode>();
  n->on = on; n->cb = std::move(cb);
  return View(n);
}
View Slider(float mn, float mx, float value, std::function<void(float)> cb) {
  auto n = std::make_shared<SliderNode>();
  n->mn = mn; n->mx = mx; n->value = value; n->cb = std::move(cb);
  return View(n);
}
View ProgressBar(float v) {
  auto n = std::make_shared<ProgressNode>();
  n->value = v;
  return View(n);
}

}  // namespace fk
