// FluxKit - text input (internal): pre-rendered glyph sets, text fields, on-screen keyboard.
//
// Text input is dynamic by nature, so it follows the same rule as the rest of the library: all pixels are
// pre-rendered at bake. Every glyph a field can show is rendered once (GlyphSet); a typed character is just
// a layer switching to that glyph's frame. The keyboard keys, labels, pressed states and the blurred glass
// behind the keyboard are baked too.
#pragma once
#include <unordered_map>
#include "Node.h"

namespace fk {

Layer* currentScreenLayer();    // defined in App.cpp
void detachLayerInternal(Layer*);  // defined in App.cpp: safely removes a layer subtree

namespace input {

struct GlyphSet {
  Font font;
  Color color;
  std::unordered_map<uint32_t, int> index;
  std::vector<SurfacePtr> cells;  // same-size cells, glyph drawn at (pad, baseline)
  std::vector<float> adv;
  int cellW = 0, cellH = 0, pad = 0, baseline = 0;
  int find(uint32_t cp) const { auto it = index.find(cp); return it == index.end() ? -1 : it->second; }
};
std::shared_ptr<GlyphSet> glyphSet(const Font& f, Color c, const std::vector<uint32_t>& codepoints);
std::vector<uint32_t> defaultCharset(const std::string& extraUtf8);

struct FieldState {
  std::string id;
  Layer *container = nullptr, *ph = nullptr, *row = nullptr, *cursor = nullptr, *ring = nullptr;
  std::vector<Layer*> chars;
  std::shared_ptr<GlyphSet> glyphs;
  std::vector<uint32_t> text;
  int maxLen = 48;
  bool secure = false;
  std::string returnLabel = "Done";
  int innerW = 0;
  std::function<void(const std::string&)> onChange, onSubmit;
};

std::vector<uint32_t> decode(const std::string& utf8);
std::string encode(const std::vector<uint32_t>& cps);

FieldState* newField(const std::string& id);
FieldState* findField(const std::string& id);
size_t fieldCount();
size_t fieldBakeCount();  // how many times a TextField was baked (counts re-bakes too)
void registerReturnLabel(const std::string& label);
void refresh(FieldState* f, bool animate, size_t prevLen);
std::string getText(FieldState* f);
void setText(FieldState* f, const std::string& s);

void focus(FieldState* f);
void hideKeyboard();
bool keyboardVisible();
FieldState* focused();
bool isKeyboardOrField(Layer* l);  // layer belongs to the keyboard or the focused field

// app glue
void init(Layer* kbRoot, int W, int H, int blur);
Layer* bakeKeyboardBackdrop(Layer* screenLayer, const Surface& screenSnapshot);
void bakeKeyboard();
void onScreenChanged();
bool keyCenter(uint32_t cp, int page, int& x, int& y);  // for tests
bool specialKeyCenter(int kind, int& x, int& y);        // 1 shift 2 back 3 mode 4 space 5 return

}  // namespace input
}  // namespace fk
