#include <Arduino.h>
#include <M5Unified.h>
#include <FluxKit.h>
// #include "Inter_Bold.h"   // <- generated with: python tools/ttf2h.py Inter-Bold.ttf --subset latin

using namespace fk;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // begin() shows the boot splash immediately (light-blue -> dark-blue gradient, your text in the middle,
  // "Made with FluxKit" at the bottom). When bake() finishes it fades into the UI below.
  Config fk_cfg;
  fk_cfg.bootText = "Rendering";   // change it: "Starting up", "Hello", ...
  fk::begin(fk_cfg);

  // ---- fonts -------------------------------------------------------------
  // Montserrat is built in: Font::regular / medium / semibold / bold(size).
  // Import your own (any of these):
  //   auto inter = FontFace::fromMemory(Inter_Bold_ttf, Inter_Bold_ttf_len, "Inter");  // C array in flash
  //   auto inter = FontFace::fromFile("/sdcard/Inter-Bold.ttf", "Inter");               // SD card / LittleFS
  //   registerFont("Inter", inter);        // then: Text("Hi").font("Inter", 32)
  //   setFontFamily(Weight::Bold, inter);  // or replace one Montserrat weight
  //   setDefaultFont(inter);               // or swap the whole UI's font

  theme().accent = Color(0x0A84FF);

  screen("home",
    ZStack({
      Rectangle().fill(LinearGradient(0x141E30, 0x35577D, 60)),
      VStack({
        Text("FluxKit").font(Font::bold(64)).appear(Appear::slideUp(60)),
        Text("Everything rendered at boot").font(Font::medium(24)).foreground(Color(0xB0C4DE))
            .appear(Appear::slideUp(60).delay(80)),
        Spacer(),
        VStack({
          HStack({Text("Wi-Fi").font(Font::semibold(22)), Spacer(),
                  Toggle(false, [](bool on) { Serial.println(on); layer("connect").enabled(on); })}).fillWidth(),
          Divider(),
          HStack({Text("Volume"), Spacer(), TextStates({"0%", "50%", "100%"}).id("vol")}).fillWidth(),
          Slider(0, 100, 50, [](float v) { layer("vol").frame(v < 33 ? 0 : v < 66 ? 1 : 2); }),
        }).spacing(18).padding(28).fillWidth().cornerRadius(28)
          .material(30, Color(0xFFFFFF, 30)).border(Color(0xFFFFFF, 50)).shadow(24, 12),
        // Greyed out until Wi-Fi is on. Tapping it while disabled: fades red, wiggles, fades back to grey.
        Button("Connect", [] { Serial.println("connecting"); }).disabled().id("connect").fillWidth(),
        // Tap the box: a blurred keyboard springs up and the screen lifts so the field stays visible.
        TextField("Device name", nullptr, [](const std::string& s) { Serial.println(s.c_str()); })
            .id("device").returnKey("Save").fillWidth(),
        Button("Settings", [] { push("settings"); }).fillWidth(),
        Button("About", [] { present("about"); }).background(Color(0xFFFFFF, 30)).fillWidth(),
      }).spacing(20).padding(48, 36),
    }));

  screen("settings",
    ZStack({
      Rectangle().fill(Color(0x0B0B0E)),
      VStack({
        HStack({Button("‹ Back", [] { pop(); }).background(Color(0xFFFFFF, 24)), Spacer(),
                // Fades the whole screen to black (cubic-bezier ease-in-out), then powers down.
                Button("Power off", [] { powerOff(); }).background(Color(0xFF453A))}).fillWidth(),
        ScrollView(VStack(ForEach(24, [](int i) {
          return HStack({Text("Option " + std::to_string(i + 1)), Spacer(), Toggle(i % 3 == 0)})
              .fillWidth().padding(16, 20).background(Color(0x1C1C1E)).cornerRadius(18);
        })).spacing(10)).fillHeight().fillWidth(),
      }).spacing(16).padding(40, 28),
    }));

  popup("about",
    VStack({
      Text("About").font(Font::bold(30)),
      Text("Layouts, text, blur and animation frames are all baked at boot.")
          .font(Font::regular(20)).foreground(Color(0xC0C0C8)).frame(480).multilineAlign(Align::Center),
      Spinner(44),
      Button("Close", [] { dismiss(); }).fillWidth(),
    }).spacing(18).padding(32).background(Color(0x1C1C1E)).cornerRadius(32).shadow(40, 20));

  bake();  // <- all rasterization happens here, once
}

void loop() { fk::update(); }
