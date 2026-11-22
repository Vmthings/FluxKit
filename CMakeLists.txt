# FluxKit on Linux: Raspberry Pi Zero 2 W (and Pi 3/4/5, Orange Pi, any SBC with a framebuffer).
#   sudo apt install cmake g++
#   cmake -B build && cmake --build build -j4 && sudo ./build/fluxkit_demo
cmake_minimum_required(VERSION 3.13)
project(FluxKit CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

option(FLUXKIT_HOST "Headless RAM-framebuffer stub (tests, screenshots) instead of /dev/fb0" OFF)

file(GLOB_RECURSE FK_SOURCES CONFIGURE_DEPENDS src/fk/*.cpp)   # backends self-select in Backend.h
add_library(fluxkit STATIC ${FK_SOURCES})
target_include_directories(fluxkit PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(fluxkit PUBLIC Threads::Threads)
if(FLUXKIT_HOST)
  target_compile_definitions(fluxkit PUBLIC FLUXKIT_HOST)
endif()

add_executable(fluxkit_demo examples/Linux/main.cpp)
target_link_libraries(fluxkit_demo PRIVATE fluxkit)
