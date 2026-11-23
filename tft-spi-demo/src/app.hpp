#pragma once

//! The demo app: shapes, text and touch drawing, or a picture viewer.
//! Raspberry Pi (Linux) only.

#include <string>
#include <vector>

namespace app {

/// Usage:
///   tft_spi_demo [--calibrate]      demo: shapes, text, touch drawing
///   tft_spi_demo [--fill] <image>   show a JPEG or PNG and exit
///   tft_spi_demo --colors           color gradients and exit
///
/// `TFT_GAMMA=full|analog|none` picks the gamma tables sent at startup.
/// Throws on errors.
void run(const std::vector<std::string>& args);

}  // namespace app
