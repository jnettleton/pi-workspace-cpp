#pragma once

//! Fixtures shared by the tests.

#include <cstdlib>
#include <filesystem>
#include <string>

#include "mock_spi.hpp"
#include "tft_display/tft_display.hpp"

namespace helpers {

inline constexpr tft::Rgb666 BLACK{0, 0, 0};

/// A display on a mock panel. The panel is sized for the rotation's logical
/// size unless given explicitly.
struct Screen {
    mock::MockSpi spi;
    tft::TftDisplay display{spi};

    explicit Screen(tft::Rotate rotation)
        : spi(rotation == tft::Rotate::Degrees0 || rotation == tft::Rotate::Degrees180 ? 320 : 480,
              rotation == tft::Rotate::Degrees0 || rotation == tft::Rotate::Degrees180 ? 480 : 320) {
        display.set_rotation(rotation);
    }
    /// An unrotated display (the default Degrees0, no command sent yet) on a
    /// `w` x `h` panel.
    Screen(std::size_t w, std::size_t h) : spi(w, h) {}

    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;
};

/// A 480x320 landscape display on a mock panel.
struct Landscape : Screen {
    Landscape() : Screen(tft::Rotate::Degrees90) {}
};

/// A temp directory for one test, removed when destroyed.
struct TempDir {
    std::filesystem::path path;

    explicit TempDir(const std::string& name) {
        path = std::filesystem::temp_directory_path() /
               ("tft-test-" + name + "-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

}  // namespace helpers
