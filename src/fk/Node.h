// FluxKit - internal view tree (layout + painting + baking). Not part of the public API.
#pragma once
#include <unordered_map>
#include "Layer.h"
#include "View.h"

namespace fk {

struct BakeEnv {
  Compositor* comp = nullptr;
  const Surface* backdrop = nullptr;  // absolute-origin screen snapshot for material (popups)
  std::unordered_map<std::string, Layer*>* ids = nullptr;
  std::unordered_map<std::string, class Node*>* nodes = nullptr;  // id -> view node (for live re-rendering)
};

class Node : public std::enable_shared_from_this<Node> {
 public:
  virtual ~Node() = default;

  // style
  Insets pad, mar;
  int fw = -1, fh = -1;
  bool fillW = false, fillH = false;
  bool hasBg = false; Paint bg; float radius = 0;
  bool hasBorder = false; Color borderColor; float borderW = 1;
  bool hasShadow = false; Shadow shadow;
  bool hasMat = false; Material mat;
  float opacity = 1.f;
  bool hasFont = false; Font font;
  bool hasFg = false; Color fg;
  Align crossAlign = Align::Center, zH = Align::Center, zV = Align::Center;
  Align textAlign = Align::Leading;
  int spacing = 8, lineLimit = 0;
  std::function<void()> tap;
  std::string id;
  bool forceLayer = false;
  std::string reserveText;                 // Text: keep room for the widest string it may show (avoids re-layout)
  Align blockAlign = Align::Center;        // Text: where the text sits inside its frame
  int maxLen = 0; bool secureText = false;                       // TextField
  std::string returnLabel = "Done", extraCharset, initialText;     // TextField
  bool canDisable = false, startDisabled = false;  // .disabled(): pre-render enabled + disabled + error looks
  bool hasAppear = false; Appear appear;
  enum class Shape { None, Rect, Circle, Capsule } shape = Shape::None;

  Node* parent = nullptr;
  std::vector<std::shared_ptr<Node>> kids;
  Rect frame;

  // layout
  Size measure(Size avail);  // outer size (incl. margin)
  void place(Rect outer);
  bool flexW() const { return fw < 0 && (fillW || contentFlexW()); }
  bool flexH() const { return fh < 0 && (fillH || contentFlexH()); }
  Rect contentRect() const { return Rect{frame.x + pad.l, frame.y + pad.t, std::max(0, frame.w - pad.horizontal()), std::max(0, frame.h - pad.vertical())}; }
  virtual Size contentSize(Size) { return Size{}; }
  virtual bool contentFlexW() const { return false; }
  virtual bool contentFlexH() const { return false; }
  virtual void placeContent(Rect) {}
  virtual int stackAxis() const { return -1; }  // 0 = horizontal, 1 = vertical

  // painting / baking
  virtual bool isLayerRoot() const { return forceLayer || tap || canDisable || !id.empty() || opacity < 1.f || hasAppear; }
  virtual void paintContent(Surface&, int /*ox*/, int /*oy*/, BakeEnv&) {}
  virtual Layer* bake(BakeEnv& env, Layer* parent) { return bakeGeneric(env, parent); }
  virtual bool allowCompact() const { return true; }
  // Live re-rendering: repaint this node into its existing layer (same size). False = needs fk::relayout().
  virtual bool inPlaceRerender() const { return !canDisable; }
  virtual bool rerender(BakeEnv& env, Layer* layer);
  virtual Node* findText() { for (auto& k : kids) if (!k->isLayerRoot()) if (Node* t = k->findText()) return t; return nullptr; }  // may be stored as RGB565 when fully opaque
  Font resolvedFont() const;
  Color resolvedFg() const;
  Rect layerBounds() const;

 protected:
  float cornerRadius() const;
  void paintSelf(Surface& s, int ox, int oy, BakeEnv& env);
  void paintTree(Surface& s, int ox, int oy, BakeEnv& env, std::vector<Node*>& deferred);
  Layer* bakeGeneric(BakeEnv& env, Layer* parent);
  Layer* bakeBox(BakeEnv& env, Layer* parent);
  Layer* bakeDisableable(BakeEnv& env, Layer* parent);  // own box only (no children), for containers
  Layer* makeLayer(BakeEnv& env, Layer* parent, Rect bounds, const char* name = "");
  void finishLayer(BakeEnv& env, Layer* l);
  friend class TextNode;
};

// Change the first Text inside `owner` and repaint. 0 = no Text found, 1 = repainted in place, 2 = text grew: relayout needed.
int liveSetText(Node* owner, const std::string& s, BakeEnv& env, Layer* layer);

}  // namespace fk
