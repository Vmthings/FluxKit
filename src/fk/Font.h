// FluxKit - fonts. Montserrat (Regular/Medium/SemiBold/Bold) is built in; import any TTF/OTF.
#pragma once
#include "Types.h"
#include "Surface.h"

namespace fk {

// A loaded TrueType/OpenType file. Glyphs are rasterized once (at boot) and cached as 8-bit alpha.
class FontFace {
 public:
  struct Glyph { int16_t bx = 0, by = 0; uint16_t w = 0, h = 0; float adv = 0; uint8_t* bmp = nullptr; };

  // From a C array (see tools/ttf2h.py). The array must stay alive (const arrays live in flash).
  static std::shared_ptr<FontFace> fromMemory(const uint8_t* ttf, size_t len, const std::string& name = "");
  // From a file on SD / LittleFS / SPIFFS (any path fopen() understands, e.g. "/sdcard/Inter.ttf").
  static std::shared_ptr<FontFace> fromFile(const std::string& path, const std::string& name = "");

  ~FontFace();
  bool ok() const;
  const std::string& name() const;
  const Glyph& glyph(uint32_t codepoint, int px);
  float ascent(int px) const;   // px above baseline
  float descent(int px) const;  // negative
  float lineGap(int px) const;

 private:
  FontFace();
  struct Impl;
  std::unique_ptr<Impl> d_;
};

enum class Weight { Regular, Medium, SemiBold, Bold };

struct Font {
  std::shared_ptr<FontFace> face;  // null = built-in Montserrat Regular
  int size = 17;
  Font() = default;
  Font(std::shared_ptr<FontFace> f, int s) : face(std::move(f)), size(s) {}

  // Family-aware constructors (Montserrat unless you changed the family)
  static Font system(int size, Weight w = Weight::Regular);
  static Font regular(int size) { return system(size, Weight::Regular); }
  static Font medium(int size) { return system(size, Weight::Medium); }
  static Font semibold(int size) { return system(size, Weight::SemiBold); }
  static Font bold(int size) { return system(size, Weight::Bold); }
  // A font you imported and registered with registerFont()
  static Font named(const std::string& name, int size);
};

// ---- font import / registry -------------------------------------------------
std::shared_ptr<FontFace> montserrat(Weight w);                         // built-in
void registerFont(const std::string& name, std::shared_ptr<FontFace>);  // lookup by name later
std::shared_ptr<FontFace> findFont(const std::string& name);
void setFontFamily(Weight w, std::shared_ptr<FontFace> face);           // replace one weight
void setDefaultFont(std::shared_ptr<FontFace> face);                    // replace ALL weights (UI-wide font swap)
Font resolveFont(const Font& f);                                        // fills a null face

namespace text {
int ascent(const Font& f);
int lineHeight(const Font& f);
float measure(const Font& f, const std::string& utf8);                        // one line
std::vector<std::string> layout(const Font& f, const std::string& s, int maxW);  // \n + word wrap
void draw(Surface& s, const Font& f, const std::string& utf8, float x, float baselineY, Color c);
}  // namespace text

}  // namespace fk
