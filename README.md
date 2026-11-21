# FluxKit

SwiftUI-style declarative UI for the **M5Stack Tab5** (ESP32-P4, ST7123, 720×1280), in C++.
**Everything is rendered once, at boot** (`fk::bake()`): layouts, text, shadows, frosted glass, popup blur,
button pressed-states, spinner frames. At runtime the device only *composites* pre-rendered layers.

```cpp
#include <M5Unified.h>
#include <FluxKit.h>
using namespace fk;

void setup() {
  M5.begin(M5.config());
  fk::begin();
  screen("home", VStack({
      Text("Hello").font(Font::bold(48)),
      Button("Open", [] { present("about"); }).fillWidth(),
  }).padding(40).background(LinearGradient(0x141E30, 0x35577D)));
  popup("about", VStack({Text("About"), Button("Close", [] { dismiss(); })})
                     .padding(32).background(0x1C1C1E).cornerRadius(28));
  bake();               // all rasterization happens here
}
void loop() { fk::update(); }
```

## What's in it
| | |
|---|---|
| Layout | `VStack` `HStack` `ZStack` `Spacer` `ScrollView` `ForEach`; `.padding .margin .frame .fillWidth .spacing .align` |
| Content | `Text` `TextStates` `Image` `Canvas` `Spinner` `Rectangle` `RoundedRectangle` `Circle` `Capsule` `Divider` |
| Input | `TextField` (tap → blurred on-screen keyboard springs up; `.secure() .maxLength() .returnKey("Go") .charset("äöü")`); read with `fieldText("id")`, set with `setFieldText`, `focusField`, `hideKeyboard()` |
| Boot / power | boot splash (`Config::bootText`, shown instantly by `begin()`, fades out after `bake()`); `powerOff()` fades to black with a cubic-bezier ease-in-out, then powers down |
| Controls | `Button` `Toggle` `Slider` `ProgressBar` (+ any view with `.onTap`); `.disabled()` greys a button out, and tapping it fades it red, wiggles it, then fades back to grey (`layer("id").enabled(true)` re-enables it) |
| Look | `.background` (solid/`LinearGradient`) `.cornerRadius .border .shadow .material` (frosted glass) `.opacity` |
| Motion | `.appear(Appear::slideUp())` (staggerable), screen transitions (`Fade/Push/Cover`), popup spring animations, `layer("id").offset/scale/opacity/reveal/frame/play` |
| Navigation | `screen() show() push() pop()`; popups: `popup() present() dismiss()` (`Alert`, `Sheet`, `Fullscreen`) |

## Fonts
Montserrat Regular / Medium / SemiBold / Bold (SIL OFL, Latin subset, ~78 KB each) is built in: `Font::regular|medium|semibold|bold(size)`.

Import your own, any of three ways:
```bash
python tools/ttf2h.py Inter-Bold.ttf --subset latin          # -> Inter_Bold.h  (needs: pip install fonttools)
python tools/ttf2h.py Noto.ttf --subset "U+0020-007E,U+0400-04FF"   # custom Unicode ranges
```
```cpp
#include "Inter_Bold.h"
auto inter = FontFace::fromMemory(Inter_Bold_ttf, Inter_Bold_ttf_len, "Inter");  // from flash
auto inter = FontFace::fromFile("/sdcard/Inter-Bold.ttf", "Inter");               // or SD / LittleFS
registerFont("Inter", inter);          Text("Hi").font("Inter", 32)
setFontFamily(Weight::Bold, inter);    // replace one Montserrat weight
setDefaultFont(inter);                 // swap the whole UI's font
```
Glyphs are rasterized during `bake()` for exactly the strings you use, so any language your font covers works.

## Boot splash and power off
```cpp
Config cfg; cfg.bootText = "Rendering";      // centred text; the gradient is light blue -> dark blue
fk::begin(cfg);                               // splash is on screen immediately, before the slow bake
...
bake();                                       // when it finishes the splash fades into your first screen

powerOff();                                   // cubic-bezier(0.42, 0, 0.58, 1) fade to black, then power down
powerOff(Animation::cubicBezier(0.65f, 0, 0.35f, 1, 1200), [] { /* your own shutdown */ });
```
The splash also shows a grey, semi-transparent "Made with FluxKit" and the repo URL at the bottom (`Config::bootCredit = false` hides it). The power-off overlay is a flat-colour layer, so it costs no memory. `platform::powerOff()` calls `M5.Power.powerOff()` and falls back to deep sleep: **untested on the Tab5**.

## Text input
`TextField("placeholder")` is a normal view. Tapping it focuses it, a keyboard springs up (rows rise in one after another) on frosted glass baked from the current screen, and the screen lifts so the field stays visible. Shift (double-tap = caps lock), `123` / `#+=` symbol pages, space, hold-to-repeat backspace, and a return key whose label you choose per field (`.returnKey("Go")`). Tapping outside the keyboard dismisses it (`Config::dismissKeyboardOnTapOutside`).

Like everything else it is pre-rendered: each glyph the field can show is rendered once at bake, a typed character is a layer switching to that glyph's frame, and the cursor blink is a 2-frame loop. Typed text must use characters in the field's glyph set (ASCII + `£ ¥ € •` by default; add more with `.charset("äöü")` *and* make sure the font contains them). The keyboard is a QWERTY layout; the cursor always sits at the end (no selection, no cursor movement, no IME). Budget roughly 2 MB of PSRAM for the keyboard, plus ~0.7 MB of blurred glass per screen that has a text field.

## How "all rendered at boot" works
* Each view that can move/fade/change (anything with `.onTap`, `.id`, `.appear`, `.opacity`, controls, scroll content) becomes a **layer**: a pre-rendered ARGB surface in PSRAM. Static views are flattened into their parent's layer.
* Animation only changes layer properties (offset, scale, opacity, frame, reveal), never pixels. A toggle slides its pre-rendered knob; a slider moves the knob and crops the pre-rendered fill; a button swaps to its pre-rendered pressed frame; a `.disabled()` button has three pre-rendered looks (normal, grey, red) and the error animation is a fade plus a damped wiggle of those layers; `TextStates` / `Canvas` / `Spinner` flip between pre-rendered frames.
* Popup blur: at bake each screen is snapshotted, blurred, upscaled and dithered into full-resolution RGB565 levels (default 2). Opening a popup cross-fades them with an integer blend, so the blur appears to grow with no per-frame blur or scaling.
* The compositor tracks dirty rects, culls anything hidden behind an opaque layer, memcpy's opaque RGB565 layers, and dithers RGBA content to RGB565.

## Limits / things to know
* **Dynamic content must be a pre-rendered state.** Use `TextStates({...})`, `Canvas(..., frames)`, or `reveal` for progress. There is no per-frame text re-render by design.
* **Memory is the trade-off.** Layers with transparency (rounded cards, text) are ARGB8888; fully opaque layers (screen backgrounds, blur backdrops) are stored as dithered RGB565 at 1.8 MB per full screen. The bundled example uses ~19 MB of the 32 MB PSRAM (two screens, a popup, a 24-row scroll list). Each blur level costs 1.8 MB per screen that has popups: lower `Config::blurSteps` (default 2) to save memory. `fk::memoryUsed()` reports it. Each tappable view stores a second (pressed) frame.
* `.material()` blurs what is painted behind it *in the same layer* (and, for popup content, the first screen). Static glass panels work; glass over a different layer does not.
* Lifted layers draw above static siblings in the same parent.
* No PNG/JPG decoding (pass pixels via `Surface::fromRGB565/fromARGB`), no kerning (stb_truetype's `kern` table only), vertical `ScrollView` only.
* You may experience several crashes on the system if you have too much elements. We are working hard to fix this.

## Status: read this
* **Display driver call** in `src/fk/Platform.cpp`: `pushImage(..., rgb565_t*)`. If colors look byte-swapped, build with `-DFK_SWAP565`. If the screen comes up 1280×720, pass `Config{.rotation = N}`; layouts adapt to whatever size M5GFX reports.
* **Finding your bottleneck:** `fk::begin(Config{.profile = true})` prints compose ms, present ms and screen coverage per frame once a second over serial. If `present` dominates, the display transfer (not FluxKit) is the limit; DMA/double-buffering in `Platform.cpp` is the next step.
* **Performance numbers are unmeasured on hardware.** The compositor is software (no PPA yet). Small/partial animations (toggles, sliders, popups) should be fine; full-screen transitions blend ~0.9 MP per frame on a 400 MHz core, so expect somewhere around 20–30 fps there. The natural next step is moving layer blits to the P4's PPA behind `Compositor::drawLayer`. Measure `bake()` time and a full-screen `Push` transition first.
* Touch uses `M5.Touch.getDetail(0)`.
* Built for **high-performance chips only.** You must have at least 32 MB of PSRAM (or other types, like DDR RAM), and a 360 MHz processor (like the ESP32-P4 or you can also use a RPi Zero 2W.)

## Layout
```
src/FluxKit.h          umbrella header
src/fk/View.h          public declarative API        src/fk/App.h   runtime (screens, popups, input)
src/fk/Font.*          fonts + import               src/fk/Raster.*  boot-time rasterizer
src/fk/Layer.*         layers + compositor          src/fk/Anim.*    curves, springs, Appear
src/fk/fonts/          embedded Montserrat + OFL     tools/ttf2h.py   font converter
test/host_test.cpp     host render test (g++ -DFLUXKIT_HOST)
test/bench_popup.cpp   host benchmark of popup-open frame cost
test/disabled_test.cpp host test of disabled-button behaviour
test/input_test.cpp    host test: splash, text field + keyboard, power off
```
Licenses: FluxKit code is yours to license; `stb_truetype.h` is public domain/MIT; Montserrat is SIL OFL 1.1 (`src/fk/fonts/OFL.txt`).
