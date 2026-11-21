#include <cstdio>
#include "../src/FluxKit.h"
#include "../src/fk/Platform.h"
using namespace fk;
static int taps = 0;
static void dump(const char* path) {
  auto& fb = platform::hostFramebuffer(); int W = platform::width(), H = platform::height();
  FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) { uint16_t p = fb[i]; fputc(((p >> 11) & 31) << 3, f); fputc(((p >> 5) & 63) << 2, f); fputc((p & 31) << 3, f); }
  fclose(f);
}
static void run(uint32_t a, uint32_t b) { for (uint32_t t = a; t <= b; t += 16) { platform::hostSetTime(t); update(); } }
int main() {
  platform::hostSetSize(720, 1280); begin();
  screen("s", ZStack({Rectangle().fill(0x0B0B0E), VStack({
      Button("Enabled", [] { taps++; }).fillWidth(),
      Button("Disabled", [] { taps += 100; }).disabled().id("save").fillWidth(),
      Button("Disabled (ghost)", [] {}).background(Color(0xFFFFFF, 30)).disabled().fillWidth(),
  }).spacing(24).padding(60, 40)}).alignment(Align::Center, Align::Top));
  bake(); platform::hostSetTime(0); update(); run(16, 300);
  dump("/home/claude/work/d0.ppm");                                   // initial: 1 blue, 2 grey
  platform::hostSetTouch(true, 360, 162); run(320, 340); platform::hostSetTouch(false, 360, 162); run(356, 372);  // tap disabled (2nd button)
  uint32_t t = 372; bool shot1 = false, shot2 = false;
  for (; t < 1200; t += 16) { platform::hostSetTime(t); update();
    if (!shot1 && t >= 372 + 160) { dump("/home/claude/work/d1.ppm"); shot1 = true; }          // red + wiggling
    if (!shot2 && t >= 372 + 560) { dump("/home/claude/work/d2.ppm"); shot2 = true; } }          // fading back
  run(1216, 1500); dump("/home/claude/work/d3.ppm");                  // back to grey
  printf("taps after disabled tap = %d (expect 0)\n", taps);
  layer("save").enabled(true); run(1516, 1800); dump("/home/claude/work/d4.ppm");
  platform::hostSetTouch(true, 360, 162); run(1816, 1840); platform::hostSetTouch(false, 360, 162); run(1856, 1900);
  printf("taps after enabling + tap = %d (expect 100)\n", taps);
}
