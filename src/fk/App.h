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
  int blurSteps = 2;            // pre-rendered blur levels cross-faded while a popup opens (each costs 1.8 MB per screen)
  int backdropDownscale = 4;    // blur backdrops are stored at 1/N resolution
  bool log = true;
  // boot splash: shown instantly by fk::begin(), fades out when fk::bake() finishes
  bool bootSplash = true;
  std::string bootText = "Rendering";   // the centred text: change it freely
  bool bootCredit = true;               // grey "Made with FluxKit" + repo URL at the bottom
  Color bootTop{0x8FD3FF};              // light blue ...
  Color bootBottom{0x0A1F5C};           // ... to dark blue
  int keyboardBlur = 34;                // frosted-glass blur behind the keyboard
  bool dismissKeyboardOnTapOutside = true;
  bool profile = false;         // log compose/present ms per frame once a second (find your bottleneck)
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

// ---- power off: fades the whole screen to black (default cubic-bezier ease-in-out), then powers down.
// Pass your own `then` to do something else once the screen is black.
void powerOff(const Animation& fade = Animation::bezierInOut(900), std::function<void()> then = {});
void cancelPowerOff(const Animation& fade = Animation::bezierInOut(400));
bool poweringOff();

// ---- text fields / keyboard (see TextField())
std::string fieldText(const std::string& fieldId);
void setFieldText(const std::string& fieldId, const std::string& text);
void focusField(const std::string& fieldId);
void hideKeyboard();
bool keyboardVisible();

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
  LayerRef& enabled(bool on);        // views marked .disabled(): switch enabled/disabled look
 private:
  Layer* l_;
};
LayerRef layer(const std::string& id);

}  // namespace fk
