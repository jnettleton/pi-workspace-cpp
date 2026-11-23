#pragma once

//! Interactive touch calibration, and saving/loading the result.
//!
//! The calibration file holds one line: the display rotation in degrees
//! followed by the six calibration coefficients, e.g.
//! `90 0.0012 -0.1301 509.6 0.0861 0.0004 -16.2`.

#include <chrono>
#include <filesystem>

#include "tft_display/tft_display.hpp"
#include "tft_touch/calibration.hpp"
#include "tft_touch/touch_input.hpp"

namespace calibrate {

/// How long "Calibrated" / "try again" messages stay up
inline constexpr std::chrono::milliseconds MESSAGE_TIME{1500};

/// Loads the calibration from `path` if it exists and matches the current
/// rotation; otherwise (or if `force` is set) runs `run` and saves the result.
tft::Calibration load_or_calibrate(tft::TftDisplay& display, tft::TouchInput& touch,
                                   const std::filesystem::path& path, bool force,
                                   std::chrono::milliseconds message_time = MESSAGE_TIME);

/// Shows 5 targets one at a time (4 corners, then center) and fits a
/// calibration to where they were touched. Repeats until the fit is good.
tft::Calibration run(tft::TftDisplay& display, tft::TouchInput& touch,
                     std::chrono::milliseconds message_time = MESSAGE_TIME);

}  // namespace calibrate
