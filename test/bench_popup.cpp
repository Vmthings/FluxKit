// Host benchmark: wall-clock cost of each frame while a popup opens (relative numbers only; x86 != P4).
#include <chrono>
#include <cstdio>
#include "../src/FluxKit.h"
#include "../src/fk/Platform.h"
using namespace fk;
int main() {
  platform::hostSetSize(720, 1280);
  begin();
  screen("home", ZStack({
    Rectangle().fill(LinearGradient(0x141E30, 0x35577D, 60)),
    VStack({Text("Bench").font(Font::bold(64)), Spacer(),
            VStack({Text("Panel"), Slider(0, 1, .5f), Toggle(true)}).padding(28).fillWidth().cornerRadius(28).background(0x2A3345).shadow(24, 12),
            Button("Open", [] { present("p"); }).fillWidth()}).spacing(20).padding(48, 36)}));
  popup("p", VStack({Text("Popup").font(Font::bold(30)), Button("Close", [] { dismiss(); })}).spacing(18).padding(32).background(0x1C1C1E).cornerRadius(32).shadow(40, 20));
  bake();
  platform::hostSetTime(0); update(); for (int t = 16; t < 500; t += 16) { platform::hostSetTime(t); update(); }
  present("p");
  double tot = 0, mx = 0; int n = 0;
  for (int t = 600; t < 1200; t += 16) {
    platform::hostSetTime(t);
    auto a = std::chrono::steady_clock::now(); update();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
    tot += ms; mx = std::max(mx, ms); n++;
  }
  printf("popup open: %d frames, avg %.2f ms, worst %.2f ms, surfaces %zu KB\n", n, tot / n, mx, memoryUsed() / 1024);
}
