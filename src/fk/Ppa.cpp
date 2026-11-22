#include "Ppa.h"
#include <algorithm>
#include "Platform.h"

#if defined(FK_USE_PPA) && defined(__has_include)
#if __has_include("driver/ppa.h")
#define FK_PPA_REAL 1
#endif
#endif

#if defined(FK_PPA_REAL)
#include "driver/ppa.h"
#include "esp_err.h"
#endif

namespace fk {
namespace ppa {

#if defined(FK_PPA_REAL)
// ============================================================ real ESP32-P4 PPA
namespace {
ppa_client_handle_t g_blend = nullptr, g_fill = nullptr;
constexpr size_t kAlign = 128;  // >= any cache line, so addresses and sizes pass the driver's alignment check
inline uint32_t up(uint32_t v) { return (v + kAlign - 1) & ~(uint32_t)(kAlign - 1); }
bool report(esp_err_t e, const char* what) {
  if (e == ESP_OK) return true;
  static int shown = 0;
  if (shown++ < 3) platform::log("PPA %s failed: %s", what, esp_err_to_name(e));  // e.g. MSPI alignment of PSRAM windows
  return false;
}
void setIn(ppa_in_pic_blk_config_t& in, const void* buf, int pw, int ph, int bx, int by, int bw, int bh, ppa_blend_color_mode_t cm) {
  in.buffer = buf; in.pic_w = pw; in.pic_h = ph; in.block_w = bw; in.block_h = bh;
  in.block_offset_x = bx; in.block_offset_y = by; in.blend_cm = cm;
}
void setOut(ppa_out_pic_blk_config_t& o, void* buf, int pw, int ph, int bx, int by) {
  o.buffer = buf; o.buffer_size = up((uint32_t)pw * ph * 2); o.pic_w = pw; o.pic_h = ph;
  o.block_offset_x = bx; o.block_offset_y = by; o.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
}
}  // namespace

bool compiledIn() { return true; }
size_t alignment() { return kAlign; }

bool init() {
  if (g_blend && g_fill) return true;
  ppa_client_config_t c = {};
  c.max_pending_trans_num = 1;
  c.oper_type = PPA_OPERATION_BLEND;
  if (ppa_register_client(&c, &g_blend) != ESP_OK) return false;
  c.oper_type = PPA_OPERATION_FILL;
  if (ppa_register_client(&c, &g_fill) != ESP_OK) { ppa_unregister_client(g_blend); g_blend = nullptr; return false; }
  return true;
}

bool fill565(uint16_t* tile, int tw, int th, int x, int y, int w, int h, uint16_t color) {
  ppa_fill_oper_config_t c = {};
  c.out.buffer = tile; c.out.buffer_size = up((uint32_t)tw * th * 2); c.out.pic_w = tw; c.out.pic_h = th;
  c.out.block_offset_x = x; c.out.block_offset_y = y; c.out.fill_cm = PPA_FILL_COLOR_MODE_RGB565;
  c.fill_block_w = w; c.fill_block_h = h;
  uint32_t r = ((color >> 11) & 31) << 3, g = ((color >> 5) & 63) << 2, b = (color & 31) << 3;
  c.fill_argb_color.val = 0xFF000000u | (r << 16) | (g << 8) | b;
  c.mode = PPA_TRANS_MODE_BLOCKING;
  return report(ppa_do_fill(g_fill, &c), "fill");
}

bool blend565(uint16_t* tile, int tw, int th, int x, int y, int w, int h,
              const uint16_t* src, int sw, int sh, int sx, int sy, uint32_t op) {
  ppa_blend_oper_config_t c = {};
  setIn(c.in_bg, tile, tw, th, x, y, w, h, PPA_BLEND_COLOR_MODE_RGB565);
  setIn(c.in_fg, src, sw, sh, sx, sy, w, h, PPA_BLEND_COLOR_MODE_RGB565);
  setOut(c.out, tile, tw, th, x, y);
  c.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;       // RGB565 has no alpha: treated as 255
  c.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;       // the layer's opacity
  c.fg_alpha_fix_val = std::min<uint32_t>(255, op);
  c.mode = PPA_TRANS_MODE_BLOCKING;
  return report(ppa_do_blend(g_blend, &c), "blend565");
}

bool blendArgb(uint16_t* tile, int tw, int th, int x, int y, int w, int h,
               const uint32_t* src, int sw, int sh, int sx, int sy, uint32_t op) {
  ppa_blend_oper_config_t c = {};
  setIn(c.in_bg, tile, tw, th, x, y, w, h, PPA_BLEND_COLOR_MODE_RGB565);
  setIn(c.in_fg, src, sw, sh, sx, sy, w, h, PPA_BLEND_COLOR_MODE_ARGB8888);
  setOut(c.out, tile, tw, th, x, y);
  c.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  if (op >= 255) {
    c.fg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  } else {
    c.fg_alpha_update_mode = PPA_ALPHA_SCALE;         // A' = A * ratio (ratio must be inside (0,1), resolution 1/256)
    c.fg_alpha_scale_ratio = std::min(255.f / 256.f, std::max(1.f / 256.f, op / 256.f));
  }
  c.mode = PPA_TRANS_MODE_BLOCKING;
  return report(ppa_do_blend(g_blend, &c), "blendArgb");
}

#elif defined(FK_MOCK_PPA)
// ============================================================ software stand-in (tests on a PC)
bool compiledIn() { return true; }
size_t alignment() { return 128; }
bool init() { return true; }

static inline void unpack(uint16_t p, int& r, int& g, int& b) {
  r = (p >> 11) & 31; g = (p >> 5) & 63; b = p & 31;
  r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
}
static inline uint16_t pack(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

bool fill565(uint16_t* tile, int tw, int th, int x, int y, int w, int h, uint16_t color) {
  if (x < 0 || y < 0 || x + w > tw || y + h > th) return false;
  for (int j = 0; j < h; j++) std::fill(tile + (size_t)(y + j) * tw + x, tile + (size_t)(y + j) * tw + x + w, color);
  return true;
}
bool blend565(uint16_t* tile, int tw, int th, int x, int y, int w, int h,
              const uint16_t* src, int sw, int sh, int sx, int sy, uint32_t op) {
  if (x < 0 || y < 0 || x + w > tw || y + h > th || sx < 0 || sy < 0 || sx + w > sw || sy + h > sh) return false;
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      uint16_t& d = tile[(size_t)(y + j) * tw + x + i];
      int dr, dg, db, sr, sg, sb;
      unpack(d, dr, dg, db); unpack(src[(size_t)(sy + j) * sw + sx + i], sr, sg, sb);
      d = pack((sr * (int)op + dr * (255 - (int)op) + 127) / 255, (sg * (int)op + dg * (255 - (int)op) + 127) / 255,
               (sb * (int)op + db * (255 - (int)op) + 127) / 255);
    }
  return true;
}
bool blendArgb(uint16_t* tile, int tw, int th, int x, int y, int w, int h,
               const uint32_t* src, int sw, int sh, int sx, int sy, uint32_t op) {
  if (x < 0 || y < 0 || x + w > tw || y + h > th || sx < 0 || sy < 0 || sx + w > sw || sy + h > sh) return false;
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      uint32_t p = src[(size_t)(sy + j) * sw + sx + i];
      int a = (int)(((p >> 24) * op + 127) / 255);
      uint16_t& d = tile[(size_t)(y + j) * tw + x + i];
      int dr, dg, db;
      unpack(d, dr, dg, db);
      int sr = (p >> 16) & 255, sg = (p >> 8) & 255, sb = p & 255;
      d = pack((sr * a + dr * (255 - a) + 127) / 255, (sg * a + dg * (255 - a) + 127) / 255, (sb * a + db * (255 - a) + 127) / 255);
    }
  return true;
}

#else
// ============================================================ not available: CPU compositor is used
bool compiledIn() { return false; }
size_t alignment() { return 0; }
bool init() { return false; }
bool fill565(uint16_t*, int, int, int, int, int, int, uint16_t) { return false; }
bool blend565(uint16_t*, int, int, int, int, int, int, const uint16_t*, int, int, int, int, uint32_t) { return false; }
bool blendArgb(uint16_t*, int, int, int, int, int, int, const uint32_t*, int, int, int, int, uint32_t) { return false; }
#endif

}  // namespace ppa
}  // namespace fk
