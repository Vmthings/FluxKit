// FluxKit - ESP32-P4 Pixel-Processing Accelerator (PPA) backend for the compositor.
//
// Build with -DFK_USE_PPA (ESP-IDF 5.3+ with the esp_driver_ppa component) and call fk::offloadToPPA().
// Without it these functions are stubs that return false and the CPU compositor is used.
// (-DFK_MOCK_PPA emulates the hardware in software so the offload path can be tested on a PC.)
#pragma once
#include <cstddef>
#include <cstdint>

namespace fk {
namespace ppa {

bool compiledIn();   // PPA code is present in this build
bool init();         // register the driver clients; false if the hardware/driver is unavailable
size_t alignment();  // alignment (bytes) FluxKit gives the PPA output buffer: over-aligned to cover any cache line

// All rectangles are in tile / picture pixels. `tile` is the RGB565 output (also the background) of size tw x th.
bool fill565(uint16_t* tile, int tw, int th, int x, int y, int w, int h, uint16_t color);
// Opaque RGB565 source blended with constant opacity op (0..255); op == 255 is a plain copy.
bool blend565(uint16_t* tile, int tw, int th, int x, int y, int w, int h,
              const uint16_t* src, int sw, int sh, int sx, int sy, uint32_t op);
// ARGB8888 source with STRAIGHT (non-premultiplied) alpha, optionally faded by op (0..255).
bool blendArgb(uint16_t* tile, int tw, int th, int x, int y, int w, int h,
               const uint32_t* src, int sw, int sh, int sx, int sy, uint32_t op);

}  // namespace ppa
}  // namespace fk
