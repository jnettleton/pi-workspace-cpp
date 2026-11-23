#pragma once

#include <cstdint>
#include <ostream>
#include <utility>

namespace tft {

/// A raw touch reading (0..=4095 per axis) or a screen point, as (x, y).
using Point = std::pair<std::uint16_t, std::uint16_t>;

/// Prints `(x, y)`.
inline std::ostream& operator<<(std::ostream& os, const Point& p) {
    return os << '(' << p.first << ", " << p.second << ')';
}

/// Blocking touch input: what the calibration screens need. `rpi::TftTouch`
/// implements it on the Pi; tests use scripted touches.
class TouchInput {
public:
    virtual ~TouchInput() = default;
    /// Waits for a touch and returns a filtered raw reading (0..=4095 per axis).
    virtual Point wait_for_touch() = 0;
    /// Waits until the panel has been released.
    virtual void wait_for_release() = 0;
};

}  // namespace tft
