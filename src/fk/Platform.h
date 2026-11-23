// FluxKit - platform abstraction. Exactly one backend is compiled (see Backend.h):
//   M5Unified/M5GFX (M5Stack Tab5 and other M5 boards) | ESP-IDF esp_lcd (any ESP32-P4/S3 board)
//   Linux framebuffer + evdev (Raspberry Pi Zero 2 W and other Linux SBCs) | host stub (tests)
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace fk {
namespace platform {

void* alloc(size_t bytes);                       // zero-filled; PSRAM preferred on ESP32
void* allocAligned(size_t bytes, size_t align);  // zero-filled, aligned (DMA / PPA buffers)
void release(void* p);
uint32_t millis();
uint64_t micros();
void sleepMs(uint32_t ms);
void log(const char* fmt, ...);

void init(int rotation);  // rotation < 0 keeps the current one
int width();
int height();
void present(int x, int y, int w, int h, const uint16_t* rgb565);  // contiguous w*h block
bool touch(int& x, int& y);  // true while a finger / mouse button is down
void pump();                 // poll input (M5.update() on M5)
void powerOff();             // power down (see each backend)

void yield();                // let the idle task run (feeds the watchdog); cheap, throttled
size_t freeMemory();         // free PSRAM / RAM in bytes, or 0 if unknown
// Run fn on a thread with a large stack (ESP32: the Arduino loop task has only 8 KB; baking recurses through the view tree)
void runWithStack(size_t stackBytes, const std::function<void()>& fn);

int cpuCount();
int defaultRenderThreads();  // 1 on ESP32 (opt in with Config::renderThreads), all cores (max 4) on Linux
void parallelFor(int count, const std::function<void(int)>& fn);  // fn(0..count-1) spread over worker threads

#ifdef FLUXKIT_HOST
const std::vector<uint16_t>& hostFramebuffer();
void hostSetTime(uint32_t ms);
void hostSetTouch(bool down, int x, int y);
void hostSetSize(int w, int h);
#endif

// ---- generic ESP32-P4 / S3 boards (esp_lcd backend)
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
struct EspLcdPanel;                                                   // = esp_lcd_panel_handle_t
void useEspLcd(void* esp_lcd_panel_handle, int width, int height);    // call before fk::begin()
void setTouchReader(bool (*read)(int* x, int* y));                    // optional: return true while touched
void setPowerOffHandler(void (*fn)());                                // optional: your board's power-down
#endif

}  // namespace platform
}  // namespace fk
