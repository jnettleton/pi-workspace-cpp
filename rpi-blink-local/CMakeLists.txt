cmake_minimum_required(VERSION 3.21)
project(rpi_blink_local CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libgpiod provides C++ bindings via the 'libgpiodcxx' pkg-config module.
# When cross-compiling, the toolchain file below points pkg-config at the
# sysroot copy of libgpiod-dev that was installed on the Pi.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED IMPORTED_TARGET libgpiodcxx)

add_executable(rpi_blink_local src/main.cpp)
target_link_libraries(rpi_blink_local PRIVATE PkgConfig::GPIOD)

# Warnings on; they catch a lot on embedded targets.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rpi_blink_local PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Put the binary somewhere predictable so VS's deploy step can find it.
set_target_properties(rpi_blink_local PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)