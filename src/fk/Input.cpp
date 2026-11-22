#include "Input.h"
#include <cstring>
#include <map>
#include "App.h"
#include "Platform.h"

namespace fk {
namespace input {
namespace {

enum Kind { KChar = 0, KShift = 1, KBack = 2, KMode = 3, KSpace = 4, KReturn = 5 };

struct KeyRec {
  Layer* bg = nullptr;
  Layer* label = nullptr;
  int kind = KChar;
  uint32_t cp[4] = {0, 0, 0, 0};
  int ax = 0, ay = 0, w = 0, h = 0;  // rest position in screen space
};

struct K {
  Layer* root = nullptr;
  int W = 0, H = 0, kbH = 0, keyH = 0, gap = 0, padTop = 0, padBottom = 0, blur = 34;
  std::vector<Layer*> rows;
  std::vector<KeyRec> keys;
  std::map<Layer*, Layer*> backdropOf;
  std::vector<std::string> returnLabels;
  Layer* returnLabel = nullptr;
  int page = 0;
  bool lock = false;
  uint32_t lastShift = 0;
  bool visible = false, built = false;
  uint32_t gen = 0;
  FieldState* focused = nullptr;
  Layer* liftedScreen = nullptr;
  std::vector<std::unique_ptr<FieldState>> fields;
  std::unordered_map<std::string, FieldState*> byId;
  std::map<std::string, std::shared_ptr<GlyphSet>> glyphCache;
};
K& K_() { static K x; return x; }
#define KB K_()

}  // namespace

// ------------------------------------------------------------------ utf-8
std::vector<uint32_t> decode(const std::string& s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    uint8_t c = (uint8_t)s[i++];
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = n ? (c & (0x3F >> n)) : c;
    while (n-- > 0 && i < s.size() && ((uint8_t)s[i] & 0xC0) == 0x80) cp = (cp << 6) | ((uint8_t)s[i++] & 0x3F);
    out.push_back(cp);
  }
  return out;
}
static std::string enc1(uint32_t cp) {
  std::string o;
  if (cp < 0x80) o += (char)cp;
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  return o;
}
std::string encode(const std::vector<uint32_t>& cps) { std::string o; for (auto c : cps) o += enc1(c); return o; }

// ------------------------------------------------------------------ glyph sets
std::vector<uint32_t> defaultCharset(const std::string& extra) {
  std::vector<uint32_t> v;
  for (uint32_t c = 32; c < 127; c++) v.push_back(c);
  for (uint32_t c : {0xA3u, 0xA5u, 0x20ACu, 0x2022u}) v.push_back(c);
  for (uint32_t c : decode(extra)) if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
  return v;
}

std::shared_ptr<GlyphSet> glyphSet(const Font& f0, Color c, const std::vector<uint32_t>& cps) {
  Font f = resolveFont(f0);
  uint32_t h = 2166136261u;
  for (uint32_t cp : cps) h = (h ^ cp) * 16777619u;
  std::string key = std::to_string((uintptr_t)f.face.get()) + ":" + std::to_string(f.size) + ":" + std::to_string(c.r) + "," +
                    std::to_string(c.g) + "," + std::to_string(c.b) + "," + std::to_string(c.a) + ":" + std::to_string(h);
  auto it = KB.glyphCache.find(key);
  if (it != KB.glyphCache.end()) return it->second;

  auto gs = std::make_shared<GlyphSet>();
  gs->font = f; gs->color = c;
  int minBx = 0, maxRight = 0;
  for (uint32_t cp : cps) {
    const auto& g = f.face->glyph(cp, f.size);
    minBx = std::min(minBx, (int)g.bx);
    maxRight = std::max(maxRight, std::max(g.bx + (int)g.w, (int)std::ceil(g.adv)));
  }
  gs->pad = 2 - minBx;
  gs->cellW = gs->pad + maxRight + 2;
  gs->cellH = text::lineHeight(f) + 4;
  gs->baseline = text::ascent(f) + 2;
  for (uint32_t cp : cps) {
    if (gs->index.count(cp)) continue;
    auto s = std::make_shared<Surface>(gs->cellW, gs->cellH);
    text::draw(*s, f, enc1(cp), (float)gs->pad, (float)gs->baseline, c);
    gs->index[cp] = (int)gs->cells.size();
    gs->cells.push_back(s);
    gs->adv.push_back(std::round(f.face->glyph(cp, f.size).adv));
  }
  KB.glyphCache[key] = gs;
  return gs;
}

// ------------------------------------------------------------------ fields
static size_t g_fieldBakes = 0;
size_t fieldBakeCount() { return g_fieldBakes; }
FieldState* newField(const std::string& id) {
  g_fieldBakes++;
  auto old = KB.byId.find(id);
  if (old != KB.byId.end()) {  // re-bake: keep the typed text, drop the dead layer pointers
    FieldState* f = old->second;
    f->container = f->ph = f->row = f->cursor = f->ring = nullptr;
    f->chars.clear();
    f->glyphs.reset();
    return f;
  }
  KB.fields.push_back(std::make_unique<FieldState>());
  FieldState* f = KB.fields.back().get();
  f->id = id;
  KB.byId[id] = f;
  return f;
}
FieldState* findField(const std::string& id) {
  auto it = KB.byId.find(id);
  return it == KB.byId.end() ? nullptr : it->second;
}
size_t fieldCount() { return KB.fields.size(); }
void registerReturnLabel(const std::string& l) {
  if (std::find(KB.returnLabels.begin(), KB.returnLabels.end(), l) == KB.returnLabels.end()) KB.returnLabels.push_back(l);
}
std::string getText(FieldState* f) { return f ? encode(f->text) : std::string(); }

void refresh(FieldState* f, bool animate, size_t prevLen) {
  if (!f || !f->glyphs) return;
  auto& an = animator();
  float pen = 0;
  size_t n = f->text.size();
  for (size_t i = 0; i < f->chars.size(); i++) {
    Layer* c = f->chars[i];
    if (i < n) {
      uint32_t cp = f->secure ? 0x2022u : f->text[i];
      int gi = f->glyphs->find(cp);
      if (gi < 0) gi = std::max(0, f->glyphs->find('?'));
      c->frame = gi;
      c->tx = pen - f->glyphs->pad;
      pen += f->glyphs->adv[gi];
      c->visible = true;
    } else c->visible = false;
  }
  f->cursor->tx = pen + 1.f;
  float over = pen + 6.f - (float)f->innerW;
  float target = over > 0 ? -over : 0.f;
  float phTarget = n ? 0.f : 1.f;
  if (animate) {
    an.animate(&f->row->tx, target, Animation::easeOut(140));
    an.animate(&f->ph->opacity, phTarget, Animation::easeOut(160));
    if (n > prevLen && n > 0) {  // newest character pops in
      Layer* c = f->chars[n - 1];
      c->scale = 0.55f; c->opacity = 0.f;
      an.animate(&c->scale, 1.f, Animation::spring(320, 0.45f));
      an.animate(&c->opacity, 1.f, Animation::easeOut(110));
    }
  } else {
    f->row->tx = target;
    f->ph->opacity = phTarget;
  }
}

void setText(FieldState* f, const std::string& s) {
  if (!f) return;
  size_t prev = f->text.size();
  f->text.clear();
  for (uint32_t cp : decode(s)) {
    if ((int)f->text.size() >= f->maxLen) break;
    if (f->glyphs && f->glyphs->find(cp) < 0) continue;
    f->text.push_back(cp);
  }
  refresh(f, f->container && f->container->wvis, prev);
}

// ------------------------------------------------------------------ keyboard state
static void liftScreen(FieldState* f) {
  Layer* sl = currentScreenLayer();
  if (!sl || !f->container) return;
  float fieldBottom = f->container->wy + f->container->h * f->container->ws - sl->ty;
  float kbTop = (float)(KB.H - KB.kbH);
  float shift = std::max(0.f, fieldBottom + 28.f - kbTop);
  KB.liftedScreen = sl;
  animator().animate(&sl->ty, -shift, Animation::spring(500, 0.08f));
}

static void setPage(int page, bool lock) {
  KB.page = page; KB.lock = lock;
  for (auto& k : KB.keys) {
    if (!k.label) continue;
    if (k.kind == KChar) k.label->frame = page;
    else if (k.kind == KShift) k.label->frame = page <= 1 ? (lock ? 4 : (page == 1 ? 1 : 0)) : (page == 2 ? 2 : 3);
    else if (k.kind == KMode) k.label->frame = page <= 1 ? 0 : 1;
  }
}

bool keyboardVisible() { return KB.visible; }
FieldState* focused() { return KB.focused; }

bool isKeyboardOrField(Layer* l) {
  for (; l; l = l->parent) {
    if (l == KB.root) return true;
    if (KB.focused && l == KB.focused->container) return true;
  }
  return false;
}

void hideKeyboard() {
  if (!KB.visible) return;
  auto& an = animator();
  if (KB.focused) {
    an.animate(&KB.focused->cursor->opacity, 0.f, Animation::easeOut(80));
    KB.focused->cursor->visible = false;
    an.animate(&KB.focused->ring->opacity, 0.f, Animation::easeOut(180));
  }
  KB.focused = nullptr;
  KB.visible = false;
  uint32_t gen = ++KB.gen;
  an.animate(&KB.root->ty, (float)KB.kbH + 10.f, Animation::easeIn(260), [gen]() {
    if (gen == KB.gen) KB.root->visible = false;
  });
  if (KB.liftedScreen) an.animate(&KB.liftedScreen->ty, 0.f, Animation::easeOut(300));
  KB.liftedScreen = nullptr;
}

void focus(FieldState* f) {
  if (!f || !KB.built || KB.focused == f) return;
  auto& an = animator();
  if (KB.focused) {  // moving between fields
    KB.focused->cursor->visible = false;
    an.animate(&KB.focused->ring->opacity, 0.f, Animation::easeOut(160));
  }
  KB.focused = f;
  f->cursor->visible = true;
  f->cursor->opacity = 1.f;
  f->cursor->playStart = platform::millis();
  f->cursor->frame = 0;
  an.animate(&f->ring->opacity, 1.f, Animation::easeOut(180));

  Layer* sl = currentScreenLayer();
  for (auto& kv : KB.backdropOf) kv.second->visible = (kv.first == sl);

  if (KB.returnLabel) {
    auto it = std::find(KB.returnLabels.begin(), KB.returnLabels.end(), f->returnLabel);
    KB.returnLabel->frame = it == KB.returnLabels.end() ? 0 : (int)(it - KB.returnLabels.begin());
  }
  if (!KB.visible) {
    KB.visible = true;
    ++KB.gen;
    setPage(0, false);
    KB.root->visible = true;
    KB.root->ty = (float)KB.kbH + 10.f;
    an.animate(&KB.root->ty, 0.f, Animation::spring(560, 0.12f));
    for (size_t i = 0; i < KB.rows.size(); i++) {  // rows rise in one after another
      KB.rows[i]->opacity = 0.f; KB.rows[i]->ty = 18.f;
      uint32_t d = 70 + (uint32_t)i * 45;
      an.animate(&KB.rows[i]->opacity, 1.f, Animation::easeOut(240).delay(d));
      an.animate(&KB.rows[i]->ty, 0.f, Animation::easeOut(320).delay(d));
    }
  }
  liftScreen(f);
}

void onScreenChanged() { hideKeyboard(); }

// ------------------------------------------------------------------ key actions
static void insertCp(uint32_t cp) {
  FieldState* f = KB.focused;
  if (!f || (int)f->text.size() >= f->maxLen || f->glyphs->find(cp) < 0) return;
  size_t prev = f->text.size();
  f->text.push_back(cp);
  refresh(f, true, prev);
  f->cursor->playStart = platform::millis();  // cursor stays solid while typing
  if (f->onChange) f->onChange(encode(f->text));
}
static void backspace() {
  FieldState* f = KB.focused;
  if (!f || f->text.empty()) return;
  size_t prev = f->text.size();
  f->text.pop_back();
  refresh(f, true, prev);
  f->cursor->playStart = platform::millis();
  if (f->onChange) f->onChange(encode(f->text));
}
static void submit() {
  FieldState* f = KB.focused;
  if (!f) return;
  auto cb = f->onSubmit;
  std::string t = encode(f->text);
  hideKeyboard();
  if (cb) cb(t);
}
static void onShift() {
  uint32_t now = platform::millis();
  if (KB.page >= 2) { setPage(KB.page == 2 ? 3 : 2, false); return; }
  if (KB.lock) { setPage(0, false); return; }
  if (KB.page == 1 && now - KB.lastShift < 350) { setPage(1, true); KB.lastShift = now; return; }  // double tap = caps lock
  setPage(KB.page == 0 ? 1 : 0, false);
  KB.lastShift = now;
}
static void onMode() { setPage(KB.page <= 1 ? 2 : 0, false); }

// ------------------------------------------------------------------ baking
void init(Layer* root, int W, int H, int blur) {
  KB.root = root; KB.W = W; KB.H = H; KB.blur = blur;
  float u = W / 720.f;
  KB.keyH = std::max(40, (int)std::lround(76 * u));
  KB.gap = std::max(4, (int)std::lround(10 * u));
  KB.padTop = (int)std::lround(14 * u);
  KB.padBottom = (int)std::lround(28 * u);
  KB.kbH = KB.padTop + 4 * KB.keyH + 3 * KB.gap + KB.padBottom;
  root->x = 0; root->y = H - KB.kbH; root->w = W; root->h = KB.kbH;
  root->absX = 0; root->absY = H - KB.kbH;
  root->visible = false; root->hit = true;
}

Layer* bakeKeyboardBackdrop(Layer* screenLayer, const Surface& snap) {
  Surface dst(KB.W, KB.kbH);
  Material m; m.blur = (float)KB.blur; m.tint = Color(0x101018, 175);
  raster::material(dst, 0, 0, KB.W, KB.kbH, 0.f, m, snap, 0, 0, 0, KB.H - KB.kbH);
  raster::line(dst, 0, 0.5f, (float)KB.W, 0.5f, 1.f, Color(0xFFFFFF, 46));  // top hairline
  auto old = KB.backdropOf.find(screenLayer);
  if (old != KB.backdropOf.end()) detachLayerInternal(old->second);  // screen was re-baked
  Layer* L = addLayer(KB.root, nullptr, 0, 0, KB.W, KB.kbH);
  L->p565 = raster::toRGB565(dst);
  L->opaque = true;
  L->visible = false;
  KB.backdropOf[screenLayer] = L;
  return L;
}

static SurfacePtr labelSurface(const std::string& s, const Font& f, Color c, int w, int h) {
  auto sf = std::make_shared<Surface>(w, h);
  float tw = text::measure(f, s);
  text::draw(*sf, f, s, std::round((w - tw) * 0.5f), (float)((h - text::lineHeight(f)) / 2 + text::ascent(f)), c);
  return sf;
}

static void iconShift(Painter& p, float cx, float cy, float s, Color c, bool filled, bool lock) {
  float t = 2.6f * s;
  float tipY = cy - 13 * s, baseY = cy + 2 * s, hw = 13 * s, sw = 6 * s, botY = cy + 13 * s;
  if (filled) {
    for (float y = tipY; y <= baseY; y += 0.5f) { float w = hw * (y - tipY) / (baseY - tipY); p.line(cx - w, y, cx + w, y, 1.5f, c); }
    p.fillRect(cx - sw, baseY - 1, 2 * sw, botY - baseY + 1, Paint(c));
  } else {
    p.line(cx - hw, baseY, cx, tipY, t, c); p.line(cx + hw, baseY, cx, tipY, t, c);
    p.line(cx - hw, baseY, cx - sw, baseY, t, c); p.line(cx + hw, baseY, cx + sw, baseY, t, c);
    p.line(cx - sw, baseY, cx - sw, botY, t, c); p.line(cx + sw, baseY, cx + sw, botY, t, c);
    p.line(cx - sw, botY, cx + sw, botY, t, c);
  }
  if (lock) p.line(cx - sw, botY + 6 * s, cx + sw, botY + 6 * s, t, c);
}
static void iconBack(Painter& p, float cx, float cy, float s, Color c) {
  float t = 2.4f * s;
  p.line(cx - 16 * s, cy, cx - 6 * s, cy - 11 * s, t, c); p.line(cx - 6 * s, cy - 11 * s, cx + 16 * s, cy - 11 * s, t, c);
  p.line(cx + 16 * s, cy - 11 * s, cx + 16 * s, cy + 11 * s, t, c); p.line(cx + 16 * s, cy + 11 * s, cx - 6 * s, cy + 11 * s, t, c);
  p.line(cx - 6 * s, cy + 11 * s, cx - 16 * s, cy, t, c);
  p.line(cx - 1 * s, cy - 5 * s, cx + 9 * s, cy + 5 * s, t, c); p.line(cx + 9 * s, cy - 5 * s, cx - 1 * s, cy + 5 * s, t, c);
}

void bakeKeyboard() {
  if (KB.built || KB.fields.empty()) return;
  KB.built = true;
  const int W = KB.W, keyH = KB.keyH, gap = KB.gap;
  const float u = W / 720.f;
  Font kf = Font::medium(std::max(14, (int)std::lround(30 * u)));
  Font sf = Font::medium(std::max(12, (int)std::lround(21 * u)));
  Color ink = theme().text;
  int margin = (int)std::lround(6 * u);
  int kw = (W - 2 * margin - 9 * gap) / 10;
  float rad = 11.f * u;

  auto bgPair = [&](int w, Color n, Color p) {
    auto a = std::make_shared<Surface>(w, keyH), b = std::make_shared<Surface>(w, keyH);
    raster::fillRoundRect(*a, 0, 0, (float)w, (float)keyH, rad, Paint(n));
    raster::fillRoundRect(*b, 0, 0, (float)w, (float)keyH, rad, Paint(p));
    return std::vector<SurfacePtr>{a, b};
  };
  Color letterN(0xFFFFFF, 44), letterP(0xFFFFFF, 96), specN(0xFFFFFF, 24), specP(0xFFFFFF, 70);
  Color retN = theme().accent, retP = Color::mix(theme().accent, Color(0x000000), 0.28f);

  for (int i = 0; i < 4; i++) {
    int y = KB.padTop + i * (keyH + gap);
    KB.rows.push_back(addLayer(KB.root, nullptr, 0, y, W, keyH));
  }

  auto addKey = [&](int row, int x, int w, std::vector<SurfacePtr> bgFrames, std::vector<SurfacePtr> labelFrames, int lw, int lh,
                    int kind, const std::u32string* chars4, std::function<void()> action, bool repeat = false) {
    Layer* r = KB.rows[row];
    Layer* bg = r->add(std::make_unique<Layer>());
    bg->x = x; bg->y = 0; bg->w = w; bg->h = keyH; bg->absX = r->absX + x; bg->absY = r->absY;
    bg->frames = bgFrames;
    bg->hit = true;
    bg->onPress = [bg](bool d) {
      bg->frame = d ? 1 : 0;
      animator().animate(&bg->scale, d ? 0.94f : 1.f, d ? Animation::easeOut(70) : Animation::spring(260, 0.3f));
    };
    bg->onTap = std::move(action);
    if (repeat) bg->repeatMs = 55;
    Layer* lab = addLayer(bg, nullptr, (w - lw) / 2, (keyH - lh) / 2, lw, lh);
    lab->frames = labelFrames;
    KeyRec k;
    k.bg = bg; k.label = lab; k.kind = kind; k.ax = bg->absX; k.ay = bg->absY; k.w = w; k.h = keyH;
    if (chars4) for (int p = 0; p < 4; p++) k.cp[p] = (*chars4)[p];
    KB.keys.push_back(k);
    return lab;
  };

  // ---- letter / symbol keys (4 pages: abc, ABC, 123, #+=)
  static const std::u32string R1[4] = {U"qwertyuiop", U"QWERTYUIOP", U"1234567890", U"[]{}#%^*+="};
  static const std::u32string R2[4] = {U"asdfghjkl", U"ASDFGHJKL", U"-/:;()$&@", U"_\\|~<>\u20AC\u00A3\u00A5"};
  static const std::u32string R3[4] = {U"zxcvbnm", U"ZXCVBNM", U".,?!'\"_", U".,?!'\"\u2022"};
  int lw = (int)std::ceil(kf.size * 1.2f), lh = text::lineHeight(kf) + 4;
  auto bgLetter = bgPair(kw, letterN, letterP);
  auto rowKeys = [&](int row, const std::u32string* src, int count, int startX) {
    for (int i = 0; i < count; i++) {
      std::u32string c4;
      std::vector<SurfacePtr> labels;
      for (int p = 0; p < 4; p++) {
        c4 += src[p][i];
        labels.push_back(labelSurface(enc1(src[p][i]), kf, ink, lw, lh));
      }
      uint32_t cps[4] = {c4[0], c4[1], c4[2], c4[3]};
      addKey(row, startX + i * (kw + gap), kw, bgLetter, labels, lw, lh, KChar, &c4, [cps]() {
        insertCp(cps[KB.page]);
        if (KB.page == 1 && !KB.lock) setPage(0, false);  // shift is one-shot unless locked
      });
    }
  };
  rowKeys(0, R1, 10, margin);
  rowKeys(1, R2, 9, margin + (kw + gap) / 2);

  // ---- row 3: shift | 7 keys | backspace
  int sw = (W - 2 * margin - 7 * kw - 8 * gap) / 2;
  {
    auto bg = bgPair(sw, specN, specP);
    std::vector<SurfacePtr> fr;
    for (int f = 0; f < 5; f++) {
      auto s = std::make_shared<Surface>(sw, keyH);
      Painter p(*s);
      if (f == 2 || f == 3) { auto t = labelSurface(f == 2 ? "#+=" : "123", sf, ink, sw, keyH); raster::blend(*s, *t, 0, 0); }
      else iconShift(p, sw * 0.5f, keyH * 0.5f, u, ink, f == 1 || f == 4, f == 4);
      fr.push_back(s);
    }
    addKey(2, margin, sw, bg, fr, sw, keyH, KShift, nullptr, []() { onShift(); });
  }
  rowKeys(2, R3, 7, margin + sw + gap);
  {
    auto bg = bgPair(sw, specN, specP);
    auto s = std::make_shared<Surface>(sw, keyH);
    Painter p(*s);
    iconBack(p, sw * 0.5f, keyH * 0.5f, u, ink);
    addKey(2, W - margin - sw, sw, bg, {s}, sw, keyH, KBack, nullptr, []() { backspace(); }, true);
  }

  // ---- row 4: 123 | space | return
  int mw = (int)std::lround(kw * 1.7f), rw = (int)std::lround(kw * 2.5f);
  int spw = W - 2 * margin - mw - rw - 2 * gap;
  {
    auto bg = bgPair(mw, specN, specP);
    addKey(3, margin, mw, bg, {labelSurface("123", sf, ink, mw, keyH), labelSurface("ABC", sf, ink, mw, keyH)}, mw, keyH, KMode, nullptr, []() { onMode(); });
  }
  {
    auto bg = bgPair(spw, letterN, letterP);
    addKey(3, margin + mw + gap, spw, bg, {labelSurface("space", sf, Color(0xFFFFFF, 170), spw, keyH)}, spw, keyH, KSpace, nullptr, []() { insertCp(' '); });
  }
  {
    auto bg = bgPair(rw, retN, retP);
    std::vector<SurfacePtr> labels;
    for (auto& s : KB.returnLabels) labels.push_back(labelSurface(s, sf, Color::White, rw, keyH));
    if (labels.empty()) labels.push_back(labelSurface("Done", sf, Color::White, rw, keyH));
    KB.returnLabel = addKey(3, W - margin - rw, rw, bg, labels, rw, keyH, KReturn, nullptr, []() { submit(); });
  }
  KB.root->visible = false;
}

bool keyCenter(uint32_t cp, int page, int& x, int& y) {
  for (auto& k : KB.keys)
    if (k.kind == KChar && k.cp[page] == cp) { x = k.ax + k.w / 2; y = k.ay + k.h / 2; return true; }
  return false;
}
bool specialKeyCenter(int kind, int& x, int& y) {
  for (auto& k : KB.keys)
    if (k.kind == kind) { x = k.ax + k.w / 2; y = k.ay + k.h / 2; return true; }
  return false;
}

}  // namespace input
}  // namespace fk
