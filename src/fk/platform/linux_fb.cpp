// Linux backend: Raspberry Pi Zero 2 W (and Pi 3/4/5, Orange Pi, any Linux SBC with a framebuffer).
//
//  * Output : /dev/fb0 (override: FLUXKIT_FB). On Raspberry Pi OS this works with the KMS driver through
//             fbdev emulation. 16/24/32 bpp are handled; frames wait for vsync when a large area changes.
//  * Input  : evdev touchscreen (auto-detected, override: FLUXKIT_TOUCH=/dev/input/eventN) or a mouse.
//             FLUXKIT_TOUCH_SWAP=1, FLUXKIT_TOUCH_FLIPX=1, FLUXKIT_TOUCH_FLIPY=1 fix rotated panels.
//  * No framebuffer found -> runs headless (FLUXKIT_W / FLUXKIT_H set the size), handy over SSH.
#include "../Backend.h"
#if defined(FK_IS_LINUX)
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../Platform.h"

namespace fk {
namespace platform {
namespace {
int g_fb = -1;
uint8_t* g_map = nullptr;
size_t g_len = 0;
int g_w = 800, g_h = 480, g_bpp = 32, g_pitch = 0;
unsigned g_ro = 16, g_go = 8, g_bo = 0;

int g_ev = -1;
bool g_abs = false, g_mouse = false;
int g_minX = 0, g_maxX = 0, g_minY = 0, g_maxY = 0;
int g_rawX = 0, g_rawY = 0;
bool g_down = false, g_swap = false, g_flipX = false, g_flipY = false;

bool envOn(const char* k) { const char* v = getenv(k); return v && *v && *v != '0'; }

#define TEST_BIT(b, arr) ((arr)[(b) / 8] & (1 << ((b) % 8)))

bool probe(const char* path) {
  int fd = open(path, O_RDONLY | O_NONBLOCK);
  if (fd < 0) return false;
  uint8_t ev[(EV_MAX + 7) / 8] = {0}, abs[(ABS_MAX + 7) / 8] = {0}, key[(KEY_MAX + 7) / 8] = {0}, rel[(REL_MAX + 7) / 8] = {0};
  ioctl(fd, EVIOCGBIT(0, sizeof ev), ev);
  ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs);
  ioctl(fd, EVIOCGBIT(EV_KEY, sizeof key), key);
  ioctl(fd, EVIOCGBIT(EV_REL, sizeof rel), rel);
  bool hasAbs = TEST_BIT(EV_ABS, ev) && (TEST_BIT(ABS_X, abs) || TEST_BIT(ABS_MT_POSITION_X, abs)) &&
                (TEST_BIT(BTN_TOUCH, key) || TEST_BIT(BTN_LEFT, key) || TEST_BIT(ABS_MT_POSITION_X, abs));
  bool hasMouse = TEST_BIT(EV_REL, ev) && TEST_BIT(REL_X, rel) && TEST_BIT(BTN_LEFT, key);
  if (!hasAbs && !hasMouse) { close(fd); return false; }
  if (hasAbs) {
    input_absinfo ax{}, ay{};
    bool mt = TEST_BIT(ABS_MT_POSITION_X, abs);
    ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_X : ABS_X), &ax);
    ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_Y : ABS_Y), &ay);
    g_minX = ax.minimum; g_maxX = ax.maximum; g_minY = ay.minimum; g_maxY = ay.maximum;
    g_abs = true;
  } else g_mouse = true;
  g_ev = fd;
  return true;
}

void openInput() {
  g_swap = envOn("FLUXKIT_TOUCH_SWAP"); g_flipX = envOn("FLUXKIT_TOUCH_FLIPX"); g_flipY = envOn("FLUXKIT_TOUCH_FLIPY");
  if (const char* p = getenv("FLUXKIT_TOUCH")) { if (probe(p)) return; }
  char path[64];
  int mouseFd = -1;
  for (int i = 0; i < 32; i++) {  // prefer a touchscreen; remember a mouse as fallback
    snprintf(path, sizeof path, "/dev/input/event%d", i);
    if (probe(path)) {
      if (g_abs) return;
      mouseFd = g_ev; g_ev = -1; g_mouse = false;
    }
  }
  if (mouseFd >= 0) { g_ev = mouseFd; g_mouse = true; }
}
}  // namespace

void* alloc(size_t n) { return calloc(1, n ? n : 1); }
void* allocAligned(size_t n, size_t a) {
  n = (n + a - 1) / a * a;
  void* p = aligned_alloc(a, n ? n : a);
  if (p) memset(p, 0, n);
  return p;
}
void release(void* p) { free(p); }
uint64_t micros() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000ULL + t.tv_nsec / 1000; }
uint32_t millis() { return (uint32_t)(micros() / 1000ULL); }
void sleepMs(uint32_t ms) { usleep(ms * 1000); }
void log(const char* fmt, ...) {
  va_list a; va_start(a, fmt); printf("[FluxKit] "); vprintf(fmt, a); va_end(a); printf("\n"); fflush(stdout);
}

void init(int rotation) {
  if (rotation >= 0) log("rotation is fixed by the display driver on Linux (set display_rotate in config.txt / use kms)");
  const char* dev = getenv("FLUXKIT_FB");
  g_fb = open(dev ? dev : "/dev/fb0", O_RDWR);
  if (g_fb >= 0) {
    fb_var_screeninfo v{}; fb_fix_screeninfo f{};
    if (ioctl(g_fb, FBIOGET_VSCREENINFO, &v) == 0 && ioctl(g_fb, FBIOGET_FSCREENINFO, &f) == 0) {
      g_w = (int)v.xres; g_h = (int)v.yres; g_bpp = (int)v.bits_per_pixel; g_pitch = (int)f.line_length;
      g_ro = v.red.offset; g_go = v.green.offset; g_bo = v.blue.offset;
      g_len = (size_t)f.line_length * v.yres_virtual;
      g_map = (uint8_t*)mmap(nullptr, g_len, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb, 0);
      if (g_map == MAP_FAILED) g_map = nullptr;
    }
  }
  if (!g_map) {
    if (g_fb >= 0) { close(g_fb); g_fb = -1; }
    if (const char* w = getenv("FLUXKIT_W")) g_w = atoi(w);
    if (const char* h = getenv("FLUXKIT_H")) g_h = atoi(h);
    log("no framebuffer: running headless at %dx%d (set FLUXKIT_FB to a framebuffer device)", g_w, g_h);
  } else {
    log("framebuffer %dx%d %dbpp", g_w, g_h, g_bpp);
  }
  openInput();
  if (g_ev < 0) log("no touchscreen/mouse found under /dev/input (are you in the 'input' group?)");
}

int width() { return g_w; }
int height() { return g_h; }

void present(int x, int y, int w, int h, const uint16_t* px) {
  if (!g_map) return;
  if (w * h * 2 >= g_w * g_h) { unsigned zero = 0; ioctl(g_fb, FBIO_WAITFORVSYNC, &zero); }  // big update: avoid tearing
  for (int j = 0; j < h; j++) {
    const uint16_t* s = px + (size_t)j * w;
    uint8_t* d = g_map + (size_t)(y + j) * g_pitch + (size_t)x * (g_bpp / 8);
    if (g_bpp == 16 && g_ro == 11) { memcpy(d, s, (size_t)w * 2); continue; }
    for (int i = 0; i < w; i++) {
      uint32_t p = s[i], r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
      r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
      if (g_bpp == 32) ((uint32_t*)d)[i] = (r << g_ro) | (g << g_go) | (b << g_bo);
      else if (g_bpp == 24) { d[i * 3 + g_ro / 8] = (uint8_t)r; d[i * 3 + g_go / 8] = (uint8_t)g; d[i * 3 + g_bo / 8] = (uint8_t)b; }
      else ((uint16_t*)d)[i] = (uint16_t)(((r >> 3) << g_ro) | ((g >> 2) << g_go) | ((b >> 3) << g_bo));
    }
  }
}

void pump() {
  if (g_ev < 0) return;
  input_event e;
  while (read(g_ev, &e, sizeof e) == (ssize_t)sizeof e) {
    if (e.type == EV_ABS) {
      if (e.code == ABS_X || e.code == ABS_MT_POSITION_X) g_rawX = e.value;
      else if (e.code == ABS_Y || e.code == ABS_MT_POSITION_Y) g_rawY = e.value;
      else if (e.code == ABS_MT_TRACKING_ID) g_down = e.value >= 0;
    } else if (e.type == EV_REL && g_mouse) {
      if (e.code == REL_X) g_rawX = std::max(0, std::min(g_w - 1, g_rawX + e.value));
      else if (e.code == REL_Y) g_rawY = std::max(0, std::min(g_h - 1, g_rawY + e.value));
    } else if (e.type == EV_KEY && (e.code == BTN_TOUCH || e.code == BTN_LEFT)) {
      g_down = e.value != 0;
    }
  }
}

bool touch(int& x, int& y) {
  int rx = g_rawX, ry = g_rawY;
  if (g_abs) {
    if (g_swap) std::swap(rx, ry);
    int mnx = g_swap ? g_minY : g_minX, mxx = g_swap ? g_maxY : g_maxX, mny = g_swap ? g_minX : g_minY, mxy = g_swap ? g_maxX : g_maxY;
    rx = mxx > mnx ? (int)((long long)(rx - mnx) * (g_w - 1) / (mxx - mnx)) : rx;
    ry = mxy > mny ? (int)((long long)(ry - mny) * (g_h - 1) / (mxy - mny)) : ry;
    if (g_flipX) rx = g_w - 1 - rx;
    if (g_flipY) ry = g_h - 1 - ry;
  }
  x = std::max(0, std::min(g_w - 1, rx)); y = std::max(0, std::min(g_h - 1, ry));
  return g_down;
}

void powerOff() {
  sync();
  if (system("systemctl poweroff") != 0) (void)!system("poweroff");
}

}  // namespace platform
}  // namespace fk
#endif
