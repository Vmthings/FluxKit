// FluxKit demo for Linux (Raspberry Pi Zero 2 W etc.). Draws to /dev/fb0, input from evdev.
//   FLUXKIT_FB=/dev/fb1  FLUXKIT_TOUCH=/dev/input/event2  FLUXKIT_TOUCH_SWAP=1  (see platform/linux_fb.cpp)
#include <ctime>
#include <cstdio>
#include "FluxKit.h"
#include "fk/Platform.h"
using namespace fk;

int main() {
  Config cfg;
  cfg.bootText = "Starting";
  cfg.renderThreads = 0;   // 0 = auto: all cores up to 4 (the Zero 2 W has 4 x Cortex-A53)
  fk::begin(cfg);

  screen("home", ZStack({
    Rectangle().fill(LinearGradient(0x141E30, 0x35577D, 60)),
    VStack({
      Text("FluxKit").font(Font::bold(56)),
      Text("00:00:00").id("clock").reserve("00:00:00").font(Font::medium(40)),   // live text
      Spacer(),
      TextField("Type here").returnKey("Done"),
      Button("Power off", [] { powerOff(); }).background(Color(0xFF453A)).fillWidth(),
    }).spacing(18).padding(40),
  }).alignment(Align::Center, Align::Top));
  bake();

  // live text: update the clock once a second, without ever re-laying-out
  uint32_t last = 0;
  for (;;) {
    update();
    uint32_t now = platform::millis();
    if (now - last >= 1000) {
      last = now;
      time_t t = time(nullptr); tm lt; localtime_r(&t, &lt);
      char buf[16]; snprintf(buf, sizeof buf, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
      setText("clock", buf);
    }
    platform::sleepMs(8);
  }
}
