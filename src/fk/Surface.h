// FluxKit - pixel buffers (premultiplied ARGB8888, stored in PSRAM on device)
#pragma once
#include "Types.h"

namespace fk {

class Surface;
using SurfacePtr = std::shared_ptr<Surface>;

// Opaque RGB565 pixels (half the memory of Surface, and the compositor can memcpy / SWAR-blend them).
class Surface565 {
 public:
  Surface565(int w, int h);
  ~Surface565();
  Surface565(const Surface565&) = delete;
  Surface565& operator=(const Surface565&) = delete;
  int w = 0, h = 0;
  uint16_t* data = nullptr;
};

class Surface {
 public:
  Surface(int w, int h);
  ~Surface();
  Surface(const Surface&) = delete;
  Surface& operator=(const Surface&) = delete;

  int w = 0, h = 0;
  uint32_t* data = nullptr;
  bool straight = false;  // pixels are straight (non-premultiplied) alpha: set when offloaded to the PPA

  uint32_t* row(int y) { return data + (size_t)y * w; }
  const uint32_t* row(int y) const { return data + (size_t)y * w; }
  size_t bytes() const { return (size_t)w * h * 4; }
  bool isOpaque() const;

  static size_t totalBytes();  // all live surfaces
  static size_t failedAllocs();  // allocations that failed (out of memory): the UI is incomplete if > 0
  // Build from user data (e.g. an exported image array)
  static SurfacePtr fromRGB565(const uint16_t* px, int w, int h);
  static SurfacePtr fromARGB(const uint32_t* px, int w, int h, bool alreadyPremultiplied = false);
};

}  // namespace fk
