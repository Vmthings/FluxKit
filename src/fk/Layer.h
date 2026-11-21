// FluxKit - layers & compositor. Every view that can move/fade/change lives in a pre-rendered layer.
#pragma once
#include "Anim.h"
#include "Surface.h"

namespace fk {

struct Layer {
  std::string name;
  Layer* parent = nullptr;
  std::vector<std::unique_ptr<Layer>> kids;

  // pre-rendered pixels (several frames = pre-baked states / animation frames)
  std::vector<SurfacePtr> frames;
  std::shared_ptr<Surface565> p565;  // alternative to frames[0] for opaque single-frame layers (fast path)
  int frame = 0;
  bool isSolid = false;      // flat colour fill (no memory): used for the power-off fade
  uint16_t solid565 = 0;
  bool hasPixels() const { return isSolid || (p565 ? p565->w > 0 : (!frames.empty() && frames[0]->w > 0)); }
  int pixW() const { return isSolid ? w : p565 ? p565->w : frames[0]->w; }
  int pixH() const { return isSolid ? h : p565 ? p565->h : frames[0]->h; }
  float pixelScale = 1.f;  // surface pixels per logical unit (<1 for low-res blur backdrops)

  // rest geometry, parent-layer space (logical units)
  int x = 0, y = 0, w = 0, h = 0;
  int absX = 0, absY = 0;  // rest position in screen space (used while baking)

  // animatable properties (about the layer centre)
  float tx = 0, ty = 0, scale = 1.f, opacity = 1.f, reveal = 1.f;  // reveal: show left N% only
  float restOpacity = 1.f;
  bool visible = true;
  bool opaque = false;  // every pixel fully opaque -> lets the compositor skip what is below
  bool clip = false;    // clip children to this layer's rect (ScrollView)

  // interaction
  bool hit = false;  // takes touches (absorbs them even without a handler)
  bool pressFeedback = false;
  std::function<void(bool down)> onPress;     // custom press visuals (replaces pressFeedback)
  std::function<void(bool enabled)> setEnabled;  // set on views marked .disabled()
  std::function<void()> onTap;
  int repeatMs = 0;  // hold-to-repeat: fires onTap every N ms while held (backspace)
  std::function<void(int phase, float lx, float ly)> onTouch;  // 0 down, 1 move, 2 up (layer-local coords)
  bool scrollable = false;
  Layer* scrollContent = nullptr;
  float scrollMin = 0, scrollMax = 0, scrollVel = 0;

  // pre-rendered frame sequence playback (Canvas / Spinner)
  float fps = 0;
  uint32_t playStart = 0;

  // appear animation (see Appear)
  bool hasAppear = false;
  Appear appear;

  // ---- computed each frame by the compositor
  float ws = 1, wx = 0, wy = 0, wo = 1;
  bool wvis = true;
  Rect wclip;    // inherited clip (screen space)
  Rect rect;     // screen rect of this layer (even without pixels)
  Rect bounds;   // screen rect actually drawn (empty if invisible)
  Rect cover;    // conservative fully-covered rect (for occlusion)
  struct Last { float ws = -1, wx = 0, wy = 0, wo = 0, reveal = 0; bool vis = false; int frame = -1; Rect bounds; } last;

  Layer* add(std::unique_ptr<Layer> l) {
    l->parent = this;
    kids.push_back(std::move(l));
    return kids.back().get();
  }
};

// Create a child layer from an existing surface (shared, not copied).
inline Layer* addLayer(Layer* parent, SurfacePtr s, int lx, int ly, int lw, int lh) {
  auto L = std::make_unique<Layer>();
  L->x = lx; L->y = ly; L->w = lw; L->h = lh;
  L->absX = parent->absX + lx; L->absY = parent->absY + ly;
  if (s) L->frames.push_back(std::move(s));
  return parent->add(std::move(L));
}

class Compositor {
 public:
  Layer root;
  void init(int w, int h);
  int width() const { return w_; }
  int height() const { return h_; }

  // Recompute transforms; returns true (and the union of changed screen areas) if anything must be redrawn.
  bool prepare(Rect& dirty);
  // Compose `d` and send it to the display.
  void render(Rect d);
  // Compose the whole screen at the current state into an ARGB surface (used to bake blur backdrops).
  void snapshot(Surface& out);
  Layer* hitTest(int x, int y);
  bool profile = false;  // log average frame cost once a second
  const std::vector<Layer*>& order() const { return order_; }
  void bringToFront(Layer* l);
  void invalidateAll() { forceFull_ = true; }  // redraw the whole screen on the next prepare()

 private:
  int w_ = 0, h_ = 0;
  std::vector<Layer*> order_;
  uint16_t* tile_ = nullptr;
  uint64_t profT0_ = 0, profCompose_ = 0, profPresent_ = 0, profPixels_ = 0;
  int profFrames_ = 0;
  struct World { float ws, wx, wy, wo; bool wvis; Rect wclip; };
  bool forceFull_ = false;
  void walk(Layer* l, const World& parent);
  void composeTo(Rect d, uint16_t* tile);
  void drawLayer(Layer* l, const Rect& d, uint16_t* tile);
  void draw565(Layer* l, const Rect& d, uint16_t* tile);
  void drawSolid(Layer* l, const Rect& d, uint16_t* tile);
};

}  // namespace fk
