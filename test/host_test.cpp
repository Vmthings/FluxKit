// Host-side smoke test: bakes a demo UI, renders frames to PPM files.
#include <cstdio>
#include "../src/FluxKit.h"
#include "../src/fk/Platform.h"
using namespace fk;

static void dump(const char* path) {
  auto& fb = platform::hostFramebuffer();
  int W = platform::width(), H = platform::height();
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    uint16_t p = fb[i];
    uint8_t r = ((p >> 11) & 31) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
    fputc(r | (r >> 5), f); fputc(g | (g >> 6), f); fputc(b | (b >> 5), f);
  }
  fclose(f);
}
static void run(uint32_t from, uint32_t to) {
  for (uint32_t t = from; t <= to; t += 16) { platform::hostSetTime(t); update(); }
}

int main() {
  platform::hostSetSize(720, 1280);
  begin();
  screen("home",
    ZStack({
      Rectangle().fill(LinearGradient(0x141E30, 0x35577D, 60)),
      VStack({
        Text("FluxKit").font(Font::bold(64)).appear(Appear::slideUp(60)),
        Text("SwiftUI-style UI for Tab5").font(Font::medium(24)).foreground(Color(0xB0C4DE)).appear(Appear::slideUp(60).delay(80)),
        Spacer(),
        VStack({
          HStack({Text("Dark mode").font(Font::semibold(22)), Spacer(), Toggle(true)}).fillWidth(),
          Divider(),
          HStack({Text("Brightness").font(Font::regular(20)), Spacer(), Text("72%").foreground(Color::Gray)}).fillWidth(),
          Slider(0, 100, 72),
          ProgressBar(0.6f),
          HStack({Spinner(40), Text("Loading assets…").font(Font::regular(20)).foreground(Color(0xD0D8E8))}).spacing(14),
        }).spacing(18).padding(28).fillWidth().cornerRadius(28)
          .material(30, Color(0xFFFFFF, 30)).border(Color(0xFFFFFF, 50)).shadow(24, 12),
        Button("Open popup", [] { present("about"); }).fillWidth().shadow(14, 6, Color(0x0A84FF, 110)),
        Button("Ghost", [] {}).background(Color(0xFFFFFF, 30)).fillWidth(),
      }).spacing(20).padding(48, 36),
    }));
  popup("about",
    VStack({
      Text("About FluxKit").font(Font::bold(30)),
      Text("Every view, blur and animation was rendered at boot. Now the P4 only composites.").font(Font::regular(20)).foreground(Color(0xC0C0C8)).frame(480).multilineAlign(Align::Center),
      Button("Got it", [] { dismiss(); }).fillWidth(),
    }).spacing(18).padding(32).background(Color(0x1C1C1E)).cornerRadius(32).shadow(40, 20, Color(0x000000, 160)));
  bake();
  platform::hostSetTime(0); update();
  run(16, 900);
  dump("/home/claude/work/home.ppm");
  // open popup mid-animation and settled
  platform::hostSetTouch(true, 360, 1065); run(920, 960);
  platform::hostSetTouch(false, 360, 1065); run(980, 1060);
  dump("/home/claude/work/popup_mid.ppm");
  run(1080, 1700);
  dump("/home/claude/work/popup.ppm");
  printf("popupOpen=%d mem=%zuKB\n", popupOpen(), memoryUsed() / 1024);
  return 0;
}
