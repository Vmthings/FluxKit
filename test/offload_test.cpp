// usage: offload_test <cpu1|cpu4|ppa> <out-prefix>   (build ppa mode with -DFK_MOCK_PPA)
#include <cstdio>
#include <cstring>
#include "../src/FluxKit.h"
#include "../src/fk/Input.h"
#include "../src/fk/Platform.h"
using namespace fk;
static uint32_t T = 0;
static const char* prefix;
static int shot = 0;
static void runTo(uint32_t t) { for (; T < t; T += 16) { platform::hostSetTime(T); update(); } }
static void snap() {
  auto& fb = platform::hostFramebuffer();
  char path[256]; snprintf(path, sizeof path, "%s_%02d.bin", prefix, shot++);
  FILE* f = fopen(path, "wb"); fwrite(fb.data(), 2, fb.size(), f); fclose(f);
}
int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "cpu1"; prefix = argc > 2 ? argv[2] : "/tmp/ot";
  platform::hostSetSize(720, 1280);
  Config cfg; cfg.log = false;
  cfg.renderThreads = !strcmp(mode, "cpu4") ? 4 : 1;
  cfg.ppa = !strcmp(mode, "ppa"); cfg.ppaMinPixels = 512;
  begin(cfg);
  screen("home", ZStack({
    Rectangle().fill(LinearGradient(0x141E30, 0x35577D, 60)),
    VStack({
      Text("Offload").font(Font::bold(64)).appear(Appear::slideUp(60)),
      Spacer(),
      VStack({HStack({Text("Wi-Fi").font(Font::semibold(22)), Spacer(), Toggle(true)}).fillWidth(), Divider(), Slider(0, 100, 60),
              TextField("Name").id("n")}).spacing(16).padding(28).fillWidth().cornerRadius(28).background(Color(0xFFFFFF, 34))
          .border(Color(0xFFFFFF, 50)).shadow(24, 12),
      Button("Open", [] { present("p"); }).fillWidth(),
      Button("Disabled", [] {}).disabled().fillWidth(),
    }).spacing(20).padding(48, 36),
  }));
  popup("p", VStack({Text("Popup").font(Font::bold(30)), Button("Close", [] { dismiss(); })}).spacing(18).padding(32)
                 .background(Color(0x1C1C1E)).cornerRadius(32).shadow(40, 20));
  bake();
  platform::hostSetTime(0); update();
  runTo(200); snap();            // splash fading out (opaque RGB565 layer at partial opacity)
  runTo(1500); snap();           // settled home screen
  present("p"); runTo(1500 + 100); snap();   // popup opening: scaled alert + blur backdrop crossfade
  runTo(1500 + 700); snap();
  dismiss(); runTo(2300 + 130); snap();
  runTo(3200);
  auto* f = input::findField("n");
  platform::hostSetTouch(true, f->container->absX + 100, f->container->absY + 30); runTo(3300); platform::hostSetTouch(false, 0, 0);
  runTo(3300 + 120); snap();     // keyboard rising, screen lifting
  runTo(4600); snap();
  powerOff(Animation::bezierInOut(1000), [] {}); runTo(4600 + 450); snap();   // fade to black, mid-way
  runTo(4600 + 1100); snap();
  printf("%s: %d frames, ppaActive=%d\n", mode, shot, (int)ppaActive());
}
