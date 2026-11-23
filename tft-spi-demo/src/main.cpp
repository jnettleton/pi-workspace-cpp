// Driver and demo for a Waveshare 3.5" RPi LCD (A) touch display on a
// Raspberry Pi 4: shapes, text, touch input with calibration, and a JPEG/PNG
// viewer.
//
// Usage:
//   tft_spi_demo [--calibrate]      demo: shapes, text, touch drawing
//   tft_spi_demo [--fill] <image>   show a JPEG or PNG and exit
//   tft_spi_demo --colors           color gradients and exit
//
// The display, shape, font, color and picture code builds on any platform so
// the tests run on a PC; only the Pi hardware parts need Linux.

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifdef __linux__
#include "app.hpp"
#endif

int main(int argc, char** argv) {
#ifdef __linux__
    try {
        app::run(std::vector<std::string>(argv + 1, argv + argc));
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
#else
    (void)argc;
    (void)argv;
    std::cerr << "tft_spi_demo needs a Raspberry Pi (Linux). On this platform run the tests (tft_tests).\n";
    return 1;
#endif
}
