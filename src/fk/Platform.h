// FluxKit - platform abstraction (M5Unified/M5GFX on device, stub on host for tests)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fk {
namespace platform {

void* alloc(size_t bytes);  // zero-filled; PSRAM preferred on ESP32
void release(void* p);
uint32_t millis();
void log(const char* fmt, ...);

void init(int rotation);  // rotation < 0 keeps the current one
int width();
int height();
void present(int x, int y, int w, int h, const uint16_t* rgb565);  // contiguous w*h block
bool touch(int& x, int& y);  // true while a finger is down
void pump();                 // M5.update()

#ifdef FLUXKIT_HOST
const std::vector<uint16_t>& hostFramebuffer();
void hostSetTime(uint32_t ms);
void hostSetTouch(bool down, int x, int y);
void hostSetSize(int w, int h);
#endif

}  // namespace platform
}  // namespace fk
