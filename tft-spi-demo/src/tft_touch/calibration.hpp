#pragma once

//! Maps raw XPT2046 readings (0..=4095) to screen coordinates.
//!
//! Uses an affine transform fitted by least squares from 3 or more
//! (raw, screen) point pairs:
//!
//!     screen_x = x[0] * raw_x + x[1] * raw_y + x[2]
//!     screen_y = y[0] * raw_x + y[1] * raw_y + y[2]
//!
//! This handles swapped axes, mirroring, scale, offset and slight skew in one
//! step, so it doesn't matter how the touch panel is wired relative to the LCD.
//! A calibration is only valid for the display rotation it was taken in.

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "tft_touch/touch_input.hpp"

namespace tft {

/// A calibration point: the raw touch reading and the screen point it should map to.
struct CalibrationPoint {
    Point raw;
    Point screen;

    friend bool operator==(const CalibrationPoint&, const CalibrationPoint&) = default;
};

struct Calibration {
    /// Coefficients for screen x: `x[0] * raw_x + x[1] * raw_y + x[2]`
    std::array<double, 3> x{};
    /// Coefficients for screen y: `y[0] * raw_x + y[1] * raw_y + y[2]`
    std::array<double, 3> y{};

    friend bool operator==(const Calibration&, const Calibration&) = default;

    /// Least-squares fit from 3 or more points. Nothing if there are fewer
    /// than 3 points or they are (nearly) in a straight line.
    static std::optional<Calibration> fit(std::span<const CalibrationPoint> points);

    /// Maps a raw reading to (unclamped) screen coordinates.
    std::pair<double, double> map(Point raw) const;

    /// Maps a raw reading to screen coordinates, clamped to a `width` x `height` screen.
    Point to_screen(Point raw, std::uint16_t width, std::uint16_t height) const;

    /// Largest distance in pixels between where a point should be and where it maps.
    double max_error(std::span<const CalibrationPoint> points) const;

    /// Text form: six space-separated coefficients (shortest exact form).
    std::string to_text() const;

    /// Parses the output of `to_text`.
    static std::optional<Calibration> from_text(std::string_view text);
};

}  // namespace tft
