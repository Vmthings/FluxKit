#include <cstdio>
#include "../src/FluxKit.h"
#include "../src/fk/Input.h"
#include "../src/fk/Platform.h"
using namespace fk;

static uint32_t T = 0;
static void dump(const char* path) {
  auto& fb = platform::hostFramebuffer(); int W = platform::width(), H = platform::height();
  FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) { uint16_t p = fb[i]; fputc(((p >> 11) & 31) << 3, f); fputc(((p >> 5) & 63) << 2, f); fputc((p & 31) << 3, f); }
  fclose(f);
}
static double meanLuma() {
  auto& fb = platform::hostFramebuffer(); double s = 0;
  for (uint16_t p : fb) s += (((p >> 11) & 31) << 3) * 0.3 + (((p >> 5) & 63) << 2) * 0.59 + ((p & 31) << 3) * 0.11;
  return s / fb.size();
}
static void run(uint32_t ms) { for (uint32_t e = T + ms; T < e; T += 16) { platform::hostSetTime(T); update(); } }
static void tap(int x, int y) { platform::hostSetTouch(true, x, y); run(48); platform::hostSetTouch(false, x, y); run(48); }
static bool tapKey(uint32_t cp, int page) { int x, y; if (!input::keyCenter(cp, page, x, y)) return false; tap(x, y); return true; }
static void tapSpecial(int kind) { int x, y; if (input::specialKeyCenter(kind, x, y)) tap(x, y); }

int main() {
  platform::hostSetSize(720, 1280);
  Config cfg; cfg.bootText = "Rendering";
  begin(cfg);
  dump("/home/claude/work/i_splash.ppm");                       // splash visible before bake
  screen("home", ZStack({
    Rectangle().fill(LinearGradient(0x141E30, 0x35577D, 60)),
    VStack({
      Text("Sign in").font(Font::bold(52)),
      Spacer(),
      Text("Name").font(Font::medium(20)).foreground(Color(0xB0C4DE)),
      TextField("Your name", nullptr, [](const std::string& s) { printf("submitted: %s\n", s.c_str()); }).id("name").returnKey("Go"),
      Text("Password").font(Font::medium(20)).foreground(Color(0xB0C4DE)),
      TextField("Password").id("pw").secure(),
      Button("Power off", [] { powerOff(); }).fillWidth().background(Color(0xFF453A)),
    }).spacing(14).padding(60, 36).fillWidth().fillHeight(),
  }).alignment(Align::Center, Align::Top));
  platform::hostSetTime(0);
  bake();
  run(60); dump("/home/claude/work/i_fade_mid.ppm");            // splash fading out
  run(1400); dump("/home/claude/work/i_home.ppm");

  auto* f = input::findField("name");
  tap(f->container->absX + 100, f->container->absY + 30);        // focus -> keyboard springs up
  run(70); dump("/home/claude/work/i_kb_mid.ppm");
  run(900);
  tapSpecial(1);                                                 // shift
  const char* word = "Hello";
  tapKey('H', 1); for (const char* c = word + 1; *c; c++) tapKey(*c, 0);
  tapSpecial(3);                                                 // 123 page
  tapKey('4', 2); tapKey('2', 2);
  tapSpecial(3);
  tapSpecial(4);                                                 // space
  tapKey('w', 0);
  run(300); dump("/home/claude/work/i_typed.ppm");
  printf("text = '%s' (expect 'Hello42 w')\n", fieldText("name").c_str());
  // hold backspace
  { int x, y; input::specialKeyCenter(2, x, y); platform::hostSetTouch(true, x, y); run(450 + 55 * 3 + 16); platform::hostSetTouch(false, x, y); run(100); }
  printf("after hold-backspace = '%s'\n", fieldText("name").c_str());
  tapKey('x', 0);
  tapSpecial(5);                                                 // return -> submit + hide
  run(100); dump("/home/claude/work/i_hide_mid.ppm");
  run(700);
  printf("keyboardVisible after return = %d\n", keyboardVisible());

  // second field, secure + tap outside dismisses
  auto* p = input::findField("pw");
  tap(p->container->absX + 100, p->container->absY + 30); run(900);
  tapKey('s', 0); tapKey('e', 0); tapKey('c', 0); run(300);
  dump("/home/claude/work/i_secure.ppm");
  tap(360, 200); run(700);                                       // tap empty area
  printf("keyboardVisible after tap-outside = %d, pw='%s'\n", keyboardVisible(), fieldText("pw").c_str());

  // power off: brightness at 0 / 25 / 50 / 75 / 100% of the fade
  run(100); double l0 = meanLuma();
  powerOff(Animation::bezierInOut(1000), [] { printf("screen is black: power down here\n"); });
  double prev = l0; printf("luma: %.1f", l0);
  for (int i = 1; i <= 4; i++) { run(250); double l = meanLuma(); printf(" -> %.1f", l); prev = l; if (i == 2) dump("/home/claude/work/i_off_mid.ppm"); }
  run(100); printf(" -> %.1f (final)\n", meanLuma());
}
