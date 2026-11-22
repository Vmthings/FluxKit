// FluxKit - picks the platform backend at compile time. Override with one of:
//   -DFLUXKIT_HOST (tests) | -DFK_BACKEND_M5 | -DFK_BACKEND_ESP_LCD | -DFK_BACKEND_LINUX
#pragma once
#if defined(FLUXKIT_HOST)
#define FK_IS_HOST 1
#elif defined(FK_BACKEND_M5)
#define FK_IS_M5 1
#elif defined(FK_BACKEND_ESP_LCD)
#define FK_IS_ESP_LCD 1
#elif defined(FK_BACKEND_LINUX)
#define FK_IS_LINUX 1
#elif defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#if defined(__has_include)
#if __has_include(<M5Unified.h>)
#define FK_IS_M5 1
#else
#define FK_IS_ESP_LCD 1
#endif
#else
#define FK_IS_M5 1
#endif
#elif defined(__linux__)
#define FK_IS_LINUX 1
#else
#define FK_IS_HOST 1
#endif
