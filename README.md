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
| Controls | `Button` `Toggle` `Slider` `ProgressBar` (+ any view with `.onTap`) |
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

## How "all rendered at boot" works
* Each view that can move/fade/change (anything with `.onTap`, `.id`, `.appear`, `.opacity`, controls, scroll content) becomes a **layer**: a pre-rendered ARGB surface in PSRAM. Static views are flattened into their parent's layer.
* Animation only changes layer properties (offset, scale, opacity, frame, reveal), never pixels. A toggle slides its pre-rendered knob; a slider moves the knob and crops the pre-rendered fill; a button swaps to its pre-rendered pressed frame; `TextStates` / `Canvas` / `Spinner` flip between pre-rendered frames.
* Popup blur: at bake each screen is snapshotted and blurred into a ladder (default 3 levels at ¼ resolution). Opening a popup cross-fades the ladder, so the blur appears to grow with no per-frame blur.
* The compositor tracks dirty rects, culls anything hidden behind an opaque layer, and ordered-dithers to RGB565.

## Limits / things to know
* **Dynamic content must be a pre-rendered state.** Use `TextStates({...})`, `Canvas(..., frames)`, or `reveal` for progress. There is no per-frame text re-render by design.
* **Memory is the trade-off.** Full-screen layers are 3.5 MB each (ARGB8888). The bundled example uses ~17 MB of the 32 MB PSRAM (two screens, a popup, a 24-row scroll list). `fk::memoryUsed()` reports it. Each tappable view stores a second (pressed) frame.
* `.material()` blurs what is painted behind it *in the same layer* (and, for popup content, the first screen). Static glass panels work; glass over a different layer does not.
* Lifted layers draw above static siblings in the same parent.
* No text input / keyboard, no PNG/JPG decoding (pass pixels via `Surface::fromRGB565/fromARGB`), no kerning (stb_truetype's `kern` table only), vertical `ScrollView` only.

## Status: read this
Built and verified **on a host build only** (full pipeline: bake, layout, text, blur, popups, navigation, scroll, touch; output inspected as images). It has **not been run on a Tab5**. Expect to touch up:
* **Display driver call** in `src/fk/Platform.cpp`: `pushImage(..., rgb565_t*)`. If colors look byte-swapped, build with `-DFK_SWAP565`. If the screen comes up 1280×720, pass `Config{.rotation = N}`; layouts adapt to whatever size M5GFX reports.
* **Performance numbers are unmeasured.** The compositor is software (no PPA yet). Small/partial animations (toggles, sliders, popups) should be fine; full-screen transitions blend ~0.9 MP per frame on a 400 MHz core, so expect somewhere around 20–30 fps there. The natural next step is moving layer blits to the P4's PPA behind `Compositor::drawLayer`. Measure `bake()` time and a full-screen `Push` transition first.
* Touch uses `M5.Touch.getDetail(0)`.

## Layout
```
src/FluxKit.h          umbrella header
src/fk/View.h          public declarative API        src/fk/App.h   runtime (screens, popups, input)
src/fk/Font.*          fonts + import               src/fk/Raster.*  boot-time rasterizer
src/fk/Layer.*         layers + compositor          src/fk/Anim.*    curves, springs, Appear
src/fk/fonts/          embedded Montserrat + OFL     tools/ttf2h.py   font converter
test/host_test.cpp     host render test (g++ -DFLUXKIT_HOST)
```
Licenses: FluxKit code is yours to license; `stb_truetype.h` is public domain/MIT; Montserrat is SIL OFL 1.1 (`src/fk/fonts/OFL.txt`).
