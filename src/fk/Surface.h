// FluxKit - pixel buffers (premultiplied ARGB8888, stored in PSRAM on device)
#pragma once
#include "Types.h"

namespace fk {

class Surface;
using SurfacePtr = std::shared_ptr<Surface>;

class Surface {
 public:
  Surface(int w, int h);
  ~Surface();
  Surface(const Surface&) = delete;
  Surface& operator=(const Surface&) = delete;

  int w = 0, h = 0;
  uint32_t* data = nullptr;

  uint32_t* row(int y) { return data + (size_t)y * w; }
  const uint32_t* row(int y) const { return data + (size_t)y * w; }
  size_t bytes() const { return (size_t)w * h * 4; }
  bool isOpaque() const;

  static size_t totalBytes();  // all live surfaces
  // Build from user data (e.g. an exported image array)
  static SurfacePtr fromRGB565(const uint16_t* px, int w, int h);
  static SurfacePtr fromARGB(const uint32_t* px, int w, int h, bool alreadyPremultiplied = false);
};

}  // namespace fk
