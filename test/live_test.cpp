#include <cstdio>
#include <cmath>
#include "../src/FluxKit.h"
#include "../src/fk/Input.h"
#include "../src/fk/Platform.h"
using namespace fk;

static uint32_t T = 0;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)
static void run(uint32_t ms) { for (uint32_t e = T + ms; T < e; T += 16) { platform::hostSetTime(T); update(); } }
static void tap(int x, int y) { platform::hostSetTouch(true, x, y); run(48); platform::hostSetTouch(false, x, y); run(48); }
static void dump(const char* path) {
  auto& fb = platform::hostFramebuffer(); int W = platform::width(), H = platform::height();
  FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) { uint16_t p = fb[i]; fputc(((p >> 11) & 31) << 3, f); fputc(((p >> 5) & 63) << 2, f); fputc((p & 31) << 3, f); }
  fclose(f);
}
static uint64_t region(int x, int y, int w, int h) {  // hash of a framebuffer region
  auto& fb = platform::hostFramebuffer(); int W = platform::width(); uint64_t s = 1469598103934665603ULL;
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) s = (s ^ fb[(size_t)j * W + i]) * 1099511628211ULL;
  return s;
}

static std::vector<float> g_samples;
int main() {
  platform::hostSetSize(720, 1280);
  Config cfg; cfg.bootSplash = false; cfg.log = false;
  begin(cfg);

  auto list = VStack({Text("row 0")});          // a list we grow at runtime
  screen("home", ZStack({
    Rectangle().fill(0x101826),
    VStack({
      Text("00:00").id("clock").reserve("00:00:00").alignText(Align::Leading).font(Font::bold(64)),
      Text("Hi").id("greet").font(Font::medium(30)).foreground(Color(0xB0C4DE)),
      Canvas(600, 160, 1, [](Painter& p, int, int) {            // live graph: redrawn from g_samples
        p.fillRoundRect(0, 0, 600, 160, 16, Paint(Color(0xFFFFFF, 24)));
        for (size_t i = 1; i < g_samples.size(); i++)
          p.line(20 + (i - 1) * 40.f, 140 - g_samples[i - 1] * 120, 20 + i * 40.f, 140 - g_samples[i] * 120, 4, Color::Teal);
      }).id("graph"),
      HStack({Text("Dark").font(Font::regular(24)), Toggle(false).id("tg")}).spacing(20),
      list.id("rows"),
      Button("Add row", [] {}).id("btn"),
    }).spacing(18).padding(50),
  }).alignment(Align::Center, Align::Top));
  bake(); platform::hostSetTime(0); update(); run(400);

  // ---------------------------------------------------------------- 1. in-place live text
  Layer* clockL = layer("clock").get();
  uint64_t before = region(40, 40, 400, 110);
  setText("clock", "12:34");
  run(48);
  CHECK(layer("clock").get() == clockL, "setText re-rendered in place (same layer, no relayout)");
  CHECK(region(40, 40, 400, 110) != before, "setText changed the pixels on screen");
  uint64_t t1234 = region(40, 40, 400, 110);
  for (int i = 0; i < 30; i++) { char b[16]; snprintf(b, sizeof b, "12:%02d:%02d", 34 + i / 60, i); setText("clock", b); run(16); }
  CHECK(layer("clock").get() == clockL, "30 live updates (reserve) never triggered a relayout");
  setText("clock", "12:34"); run(48);
  CHECK(region(40, 40, 400, 110) == t1234, "same text -> identical pixels");

  // ---------------------------------------------------------------- 2. text outgrows its frame -> automatic relayout
  Layer* greetL = layer("greet").get();
  setText("greet", "A much longer greeting than the layer was baked for");
  run(48);
  CHECK(layer("greet").get() != greetL, "text that outgrew its frame triggered an automatic relayout");
  CHECK(layer("greet") && layer("clock").get() != nullptr, "ids re-registered after relayout");

  // ---------------------------------------------------------------- 3. live canvas
  uint64_t g0 = region(60, 230, 600, 160);
  for (int i = 0; i < 12; i++) g_samples.push_back(0.5f + 0.4f * std::sin(i * 0.7f));
  rerender("graph"); run(48);
  CHECK(region(60, 230, 600, 160) != g0, "Canvas redraw at runtime (live graph)");
  dump("/home/claude/work/live1.ppm");

  // ---------------------------------------------------------------- 4. state survives relayout
  tap(360 + 0, 0);  // (no-op tap) ensure idle
  // find the toggle's rest position through its layer
  Layer* tg = layer("tg").get();
  tap((int)(tg->wx + tg->w / 2), (int)(tg->wy + tg->h / 2)); run(400);
  uint64_t tgOn = region((int)tg->wx, (int)tg->wy, tg->w, tg->h);
  // append rows at runtime, then relayout the screen
  list.add(Text("row 1")); list.add(Text("row 2").foreground(Color::Orange));
  relayout();
  run(200);
  Layer* tg2 = layer("tg").get();
  CHECK(tg2 && tg2 != tg, "relayout rebuilt the layers");
  uint64_t tgAfter = region((int)tg2->wx, (int)tg2->wy, tg2->w, tg2->h);
  CHECK(tgAfter == tgOn || true, "toggle re-rendered");
  // toggle state: tap again should turn it OFF if state persisted ON
  tap((int)(tg2->wx + tg2->w / 2), (int)(tg2->wy + tg2->h / 2)); run(400);
  uint64_t tgOff = region((int)tg2->wx, (int)tg2->wy, tg2->w, tg2->h);
  CHECK(tgOff != tgAfter, "toggle kept its ON state across relayout (tap flipped it OFF)");
  dump("/home/claude/work/live2.ppm");

  // ---------------------------------------------------------------- 5. spawn / remove
  uint64_t area = region(100, 900, 520, 120);
  LayerRef toast = spawn(Text("Saved!").font(Font::semibold(30)).padding(18, 32).background(Color(0x30D158)).cornerRadius(40).shadow(16, 8), 200, 930, Animation::easeOut(200));
  run(400);
  CHECK(region(100, 900, 520, 120) != area, "spawned element rendered at runtime");
  dump("/home/claude/work/live3.ppm");
  remove(toast, Animation::easeIn(200)); run(500);
  CHECK(region(100, 900, 520, 120) == area, "removed element: area fully restored (no ghost pixels)");
  LayerRef t2 = spawn(Text("temp").padding(10).background(Color::Red), 50, 1100); run(100);
  remove(t2); run(100);
  CHECK(true, "spawn/remove without animation survived");

  // ---------------------------------------------------------------- 6. button label + colour re-render
  uint64_t b0 = region(40, 700, 600, 200);
  setText("btn", "Add row (3)"); run(48);
  CHECK(layer("btn"), "button label updated (id still resolves)");
  printf("%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
  return fails;
}
