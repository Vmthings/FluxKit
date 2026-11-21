// FluxKit - app runtime: screens, popups, navigation, touch input, render loop.
#pragma once
#include "Anim.h"
#include "View.h"

namespace fk {

struct Layer;

struct Config {
  int rotation = -1;            // -1: keep M5 default
  int blurRadius = 26;          // popup backdrop blur (px, full-res equivalent)
  float scrim = 0.35f;          // popup backdrop darkening 0..1
  int blurSteps = 3;            // pre-rendered blur levels cross-faded while a popup opens
  int backdropDownscale = 4;    // blur backdrops are stored at 1/N resolution
  bool log = true;
};

enum class Transition { None, Fade, Push, Cover };
enum class PopupStyle { Alert, Sheet, Fullscreen };

// ---- setup (call from setup(), after M5.begin())
void begin(const Config& cfg = Config());
void screen(const std::string& name, View root);
void popup(const std::string& name, View content, PopupStyle style = PopupStyle::Alert, bool dismissOnTapOutside = true);
void bake();  // renders EVERYTHING now: every screen, popup, blur backdrop, text state, animation frame

// ---- runtime
void show(const std::string& screenName, Transition t = Transition::None);
void push(const std::string& screenName, Transition t = Transition::Push);
bool pop();
void present(const std::string& popupName);
void dismiss();
bool popupOpen();
void update();  // call from loop(): touch + animation + compositing

Animator& animator();
uint32_t screenWidth();
uint32_t screenHeight();
size_t memoryUsed();  // bytes of pre-rendered surfaces

// Handle to a baked layer (a view with .id("name")). Changing it never re-rasterizes anything.
class LayerRef {
 public:
  explicit LayerRef(Layer* l = nullptr) : l_(l) {}
  explicit operator bool() const { return l_ != nullptr; }
  LayerRef& offset(float dx, float dy, const Animation& a = Animation::none());
  LayerRef& scale(float s, const Animation& a = Animation::none());
  LayerRef& opacity(float o, const Animation& a = Animation::none());
  LayerRef& reveal(float fraction, const Animation& a = Animation::none());
  LayerRef& frame(int index);        // switch pre-rendered state (TextStates, Canvas frames)
  LayerRef& play(float fps);         // loop a pre-rendered frame sequence (0 = stop)
  LayerRef& visible(bool v);
 private:
  Layer* l_;
};
LayerRef layer(const std::string& id);

}  // namespace fk
