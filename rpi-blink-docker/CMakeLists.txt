cmake_minimum_required(VERSION 3.21)
project(rpi_blink CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libgpiod's C++ bindings, found via pkg-config.
# In the Docker image PKG_CONFIG_PATH already points at the arm64 .pc files.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED IMPORTED_TARGET libgpiodcxx)

add_executable(rpi_blink src/main.cpp)
target_link_libraries(rpi_blink PRIVATE PkgConfig::GPIOD)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rpi_blink PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(rpi_blink PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
