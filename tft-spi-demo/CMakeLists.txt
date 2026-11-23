cmake_minimum_required(VERSION 3.21)
project(tft_spi_demo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TFT_BUILD_TESTS "Build the tests (tft_tests)" ON)

# ---- Portable core: display driver, shapes, font, touch calibration, pictures.
# Builds on any platform so the tests run on a PC.
add_library(tft_core STATIC
    src/tft_display/font.cpp
    src/tft_display/tft_display.cpp
    src/tft_spi.cpp
    src/tft_touch/calibration.cpp
    src/calibrate.cpp
    src/picture.cpp
    src/ui.cpp
    src/stb_image_impl.cpp
)
target_include_directories(tft_core PUBLIC src)
target_include_directories(tft_core SYSTEM PUBLIC third_party/stb)

# Warnings on; they catch a lot on embedded targets.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(TFT_WARNINGS -Wall -Wextra -Wpedantic)
elseif (MSVC)
    set(TFT_WARNINGS /W4 /utf-8)
endif()
target_compile_options(tft_core PRIVATE ${TFT_WARNINGS})
if (MSVC)
    target_compile_options(tft_core PUBLIC /utf-8)
endif()

# ---- The demo. The Raspberry Pi parts (GPIO via /dev/gpiomem, spidev) are
# Linux only; elsewhere the program just says it needs a Pi.
add_executable(tft_spi_demo src/main.cpp)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tft_spi_demo PRIVATE
        src/app.cpp
        src/rpi/gpio.cpp
        src/rpi/spidev.cpp
        src/rpi/tft_spi_impl.cpp
        src/rpi/tft_touch.cpp
    )
endif()
target_link_libraries(tft_spi_demo PRIVATE tft_core)
target_compile_options(tft_spi_demo PRIVATE ${TFT_WARNINGS})

# Put the binaries somewhere predictable so VS's deploy step can find them.
set_target_properties(tft_spi_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---- Tests: plain executable, no framework to download. `ctest` or run
# bin/tft_tests directly (optionally with a name filter).
if (TFT_BUILD_TESTS)
    enable_testing()
    add_executable(tft_tests
        tests/test_main.cpp
        tests/mock_spi.cpp
        tests/stb_image_write_impl.cpp
        tests/test_bus16.cpp
        tests/test_calibrate.cpp
        tests/test_color_font.cpp
        tests/test_display.cpp
        tests/test_picture.cpp
        tests/test_shapes.cpp
        tests/test_touch.cpp
        tests/test_ui.cpp
    )
    target_link_libraries(tft_tests PRIVATE tft_core)
    target_include_directories(tft_tests PRIVATE tests)
    target_compile_definitions(tft_tests PRIVATE TFT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(tft_tests PRIVATE ${TFT_WARNINGS})
    set_target_properties(tft_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_test(NAME tft_tests COMMAND tft_tests)
endif()
