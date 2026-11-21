#include "Font.h"
#include "Platform.h"
#include "Raster.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

#define STBTT_malloc(x, u) ((void)(u), fk::platform::alloc(x))
#define STBTT_free(x, u) ((void)(u), fk::platform::release(x))
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"

// Embedded Montserrat (SIL OFL 1.1), generated into fonts/*.cpp
extern const unsigned char fk_montserrat_regular_ttf[]; extern const unsigned int fk_montserrat_regular_ttf_len;
extern const unsigned char fk_montserrat_medium_ttf[];  extern const unsigned int fk_montserrat_medium_ttf_len;
extern const unsigned char fk_montserrat_semibold_ttf[]; extern const unsigned int fk_montserrat_semibold_ttf_len;
extern const unsigned char fk_montserrat_bold_ttf[];    extern const unsigned int fk_montserrat_bold_ttf_len;

namespace fk {

struct FontFace::Impl {
  stbtt_fontinfo info;
  const uint8_t* data = nullptr;
  uint8_t* owned = nullptr;
  std::string name;
  bool ok = false;
  std::unordered_map<uint64_t, Glyph> glyphs;
};

FontFace::FontFace() : d_(new Impl) {}
FontFace::~FontFace() {
  for (auto& kv : d_->glyphs) platform::release(kv.second.bmp);
  if (d_->owned) platform::release(d_->owned);
}
bool FontFace::ok() const { return d_->ok; }
const std::string& FontFace::name() const { return d_->name; }

std::shared_ptr<FontFace> FontFace::fromMemory(const uint8_t* ttf, size_t, const std::string& name) {
  std::shared_ptr<FontFace> f(new FontFace());
  f->d_->data = ttf;
  f->d_->name = name;
  int off = stbtt_GetFontOffsetForIndex(ttf, 0);
  f->d_->ok = off >= 0 && stbtt_InitFont(&f->d_->info, ttf, off) != 0;
  if (!f->d_->ok) platform::log("font '%s': not a valid TTF/OTF", name.c_str());
  return f;
}

std::shared_ptr<FontFace> FontFace::fromFile(const std::string& path, const std::string& name) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) { platform::log("font file not found: %s", path.c_str()); return nullptr; }
  fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
  uint8_t* buf = (uint8_t*)platform::alloc((size_t)n);
  if (!buf || fread(buf, 1, (size_t)n, fp) != (size_t)n) { fclose(fp); platform::release(buf); return nullptr; }
  fclose(fp);
  auto f = fromMemory(buf, (size_t)n, name.empty() ? path : name);
  f->d_->owned = buf;
  return f;
}

const FontFace::Glyph& FontFace::glyph(uint32_t cp, int px) {
  uint64_t key = ((uint64_t)px << 32) | cp;
  auto it = d_->glyphs.find(key);
  if (it != d_->glyphs.end()) return it->second;
  Glyph g;
  auto& info = d_->info;
  float sc = stbtt_ScaleForMappingEmToPixels(&info, (float)px);
  int gi = stbtt_FindGlyphIndex(&info, (int)cp);
  int adv = 0, lsb = 0;
  stbtt_GetGlyphHMetrics(&info, gi, &adv, &lsb);
  g.adv = adv * sc;
  if (gi) {
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&info, gi, sc, sc, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    if (w > 0 && h > 0) {
      g.bmp = (uint8_t*)platform::alloc((size_t)w * h);
      if (g.bmp) stbtt_MakeGlyphBitmap(&info, g.bmp, w, h, w, sc, sc, gi);
      g.w = (uint16_t)w; g.h = (uint16_t)h;
    }
    g.bx = (int16_t)x0; g.by = (int16_t)y0;
  }
  return d_->glyphs.emplace(key, g).first->second;
}

float FontFace::ascent(int px) const {
  int A, D, G; stbtt_GetFontVMetrics(&d_->info, &A, &D, &G);
  return A * stbtt_ScaleForMappingEmToPixels(&d_->info, (float)px);
}
float FontFace::descent(int px) const {
  int A, D, G; stbtt_GetFontVMetrics(&d_->info, &A, &D, &G);
  return D * stbtt_ScaleForMappingEmToPixels(&d_->info, (float)px);
}
float FontFace::lineGap(int px) const {
  int A, D, G; stbtt_GetFontVMetrics(&d_->info, &A, &D, &G);
  return G * stbtt_ScaleForMappingEmToPixels(&d_->info, (float)px);
}

// ------------------------------------------------------------ registry
static std::shared_ptr<FontFace> g_builtin[4];
static std::shared_ptr<FontFace> g_family[4];
static std::map<std::string, std::shared_ptr<FontFace>> g_named;

std::shared_ptr<FontFace> montserrat(Weight w) {
  int i = (int)w;
  if (!g_builtin[i]) {
    switch (w) {
      case Weight::Regular:  g_builtin[i] = FontFace::fromMemory(fk_montserrat_regular_ttf, fk_montserrat_regular_ttf_len, "Montserrat"); break;
      case Weight::Medium:   g_builtin[i] = FontFace::fromMemory(fk_montserrat_medium_ttf, fk_montserrat_medium_ttf_len, "Montserrat Medium"); break;
      case Weight::SemiBold: g_builtin[i] = FontFace::fromMemory(fk_montserrat_semibold_ttf, fk_montserrat_semibold_ttf_len, "Montserrat SemiBold"); break;
      case Weight::Bold:     g_builtin[i] = FontFace::fromMemory(fk_montserrat_bold_ttf, fk_montserrat_bold_ttf_len, "Montserrat Bold"); break;
    }
  }
  return g_builtin[i];
}
void registerFont(const std::string& n, std::shared_ptr<FontFace> f) { g_named[n] = std::move(f); }
std::shared_ptr<FontFace> findFont(const std::string& n) {
  auto it = g_named.find(n);
  return it == g_named.end() ? nullptr : it->second;
}
void setFontFamily(Weight w, std::shared_ptr<FontFace> f) { g_family[(int)w] = std::move(f); }
void setDefaultFont(std::shared_ptr<FontFace> f) { for (auto& s : g_family) s = f; }

Font Font::system(int size, Weight w) {
  auto& f = g_family[(int)w];
  return Font(f ? f : montserrat(w), size);
}
Font Font::named(const std::string& name, int size) {
  auto f = findFont(name);
  if (!f) { platform::log("font '%s' not registered, using Montserrat", name.c_str()); return regular(size); }
  return Font(f, size);
}
Font resolveFont(const Font& f) { return f.face ? f : Font(Font::system(f.size).face, f.size); }

// ------------------------------------------------------------ text
namespace text {

static uint32_t nextCp(const char*& p) {
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
  uint32_t cp = c & (0x3F >> n);
  while (n-- > 0 && (*p & 0xC0) == 0x80) cp = (cp << 6) | ((uint8_t)*p++ & 0x3F);
  return cp;
}

int ascent(const Font& f0) { Font f = resolveFont(f0); return (int)std::lround(f.face->ascent(f.size)); }
int lineHeight(const Font& f0) {
  Font f = resolveFont(f0);
  return (int)std::lround(f.face->ascent(f.size) - f.face->descent(f.size) + f.face->lineGap(f.size));
}

float measure(const Font& f0, const std::string& s) {
  Font f = resolveFont(f0);
  float w = 0;
  for (const char* p = s.c_str(); *p;) w += std::round(f.face->glyph(nextCp(p), f.size).adv);
  return w;
}

std::vector<std::string> layout(const Font& f, const std::string& s, int maxW) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t nl = s.find('\n', start);
    std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    if (maxW >= Unbounded || measure(f, para) <= maxW) {
      out.push_back(para);
    } else {
      std::string line, word;
      auto flushWord = [&]() {
        if (word.empty()) return;
        std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(f, trial) > maxW) { out.push_back(line); line = word; }
        else line = trial;
        word.clear();
      };
      for (char ch : para) { if (ch == ' ') flushWord(); else word += ch; }
      flushWord();
      out.push_back(line);
    }
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return out;
}

void draw(Surface& s, const Font& f0, const std::string& str, float x, float base, Color c) {
  Font f = resolveFont(f0);
  float pen = x;
  for (const char* p = str.c_str(); *p;) {
    const auto& g = f.face->glyph(nextCp(p), f.size);
    if (g.bmp) {
      int gx = (int)std::lround(pen) + g.bx, gy = (int)std::lround(base) + g.by;
      for (int j = 0; j < g.h; j++) {
        int ty = gy + j; if (ty < 0 || ty >= s.h) continue;
        for (int i = 0; i < g.w; i++) {
          int tx = gx + i; if (tx < 0 || tx >= s.w) continue;
          uint32_t cov = g.bmp[j * g.w + i];
          if (!cov) continue;
          uint32_t a = (cov * c.a + 127) / 255;
          uint32_t src = (a << 24) | (((c.r * a + 127) / 255) << 16) | (((c.g * a + 127) / 255) << 8) | ((c.b * a + 127) / 255);
          s.row(ty)[tx] = raster::over(src, s.row(ty)[tx]);
        }
      }
    }
    pen += std::round(g.adv);
  }
}

}  // namespace text

void Painter::text(const std::string& str, const Font& f, float x, float y, Color c) {
  text::draw(s_, f, str, x, y + text::ascent(f), c);
}

}  // namespace fk
