// FluxKit - SwiftUI-style declarative views.
//
//   VStack({
//     Text("Hello").font(Font::bold(40)).foreground(Color::White),
//     Button("Tap me", [] { ... }).background(Color::Blue).cornerRadius(18),
//   }).padding(32).background(LinearGradient(0x141E30, 0x243B55));
//
// All views are rasterized once, at fk::bake() (boot). Modifiers only describe the look.
#pragma once
#include "Anim.h"
#include "Font.h"
#include "Raster.h"

namespace fk {

class Node;

struct Theme {
  Color accent{0x0A84FF};
  Color text{0xFFFFFF};
  Color secondaryText{0x8E8E93};
  Color track{0x3A3A3C};
  Color card{0x1C1C1E};
};
Theme& theme();  // change these before fk::bake()

class View {
 public:
  View() = default;
  explicit View(std::shared_ptr<Node> n) : n_(std::move(n)) {}
  bool valid() const { return (bool)n_; }
  std::shared_ptr<Node> node() const { return n_; }

  // ---- layout
  View padding(int all) const;
  View padding(int vertical, int horizontal) const;
  View padding(int top, int leading, int bottom, int trailing) const;
  View margin(int all) const;                  // space outside the background
  View margin(int vertical, int horizontal) const;
  View frame(int width, int height = -1) const;  // -1 = leave that axis alone
  View width(int w) const;
  View height(int h) const;
  View fillWidth() const;                      // like .frame(maxWidth: .infinity)
  View fillHeight() const;
  View fill() const;                           // both
  View spacing(int s) const;                   // stacks
  View align(Align cross) const;               // stacks: cross-axis alignment
  View alignment(Align horizontal, Align vertical) const;  // ZStack

  // ---- appearance
  View background(Paint p) const;
  View fill(Paint p) const;                    // shapes (same as background)
  View cornerRadius(float r) const;
  View border(Color c, float width = 1.f) const;
  View shadow(Shadow s) const;
  View shadow(float radius, float y = 6.f, Color c = Color(0x000000, 110)) const;
  View material(float blur = 24.f, Color tint = Color(0xFFFFFF, 34)) const;  // frosted glass
  View opacity(float o) const;

  // ---- text
  View font(Font f) const;
  View font(const std::string& registeredName, int size) const;  // imported font by name
  View foreground(Color c) const;
  View multilineAlign(Align a) const;
  View lineLimit(int n) const;

  // ---- behaviour
  View onTap(std::function<void()> fn) const;  // lifts the view into an interactive layer
  View id(const std::string& name) const;      // lets you find it later: fk::layer("name")
  View appear(Appear a) const;                 // entrance animation when its screen is shown
  View layer() const;                          // force its own layer (so you can animate it)

 private:
  std::shared_ptr<Node> n_;
};

// ---- containers
View VStack(std::vector<View> children);
View HStack(std::vector<View> children);
View ZStack(std::vector<View> children);
View ScrollView(View content);                 // vertical, with momentum; content is pre-rendered whole
std::vector<View> ForEach(int count, const std::function<View(int)>& make);

// ---- content
View Text(const std::string& s);
View TextStates(std::vector<std::string> states);  // every state pre-rendered; switch with fk::layer(id).frame(i)
View Image(SurfacePtr img, int width = -1, int height = -1);
View Canvas(int width, int height, int frames, std::function<void(Painter&, int frame, int frames)> draw, float fps = 0);
View Spinner(int size = 48, float thickness = 5.f);

// ---- shapes / spacing
View Spacer(int minLength = 0);
View Rectangle();
View RoundedRectangle(float radius);
View Circle();
View Capsule();
View Divider();

// ---- controls
View Button(const std::string& label, std::function<void()> action);
View Button(View label, std::function<void()> action);
View Toggle(bool on, std::function<void(bool)> onChange = {});
View Slider(float min, float max, float value, std::function<void(float)> onChange = {});
View ProgressBar(float value01);  // give it .id("x"); update with fk::layer("x").reveal(0.7, Animation::easeOut(300))

}  // namespace fk
