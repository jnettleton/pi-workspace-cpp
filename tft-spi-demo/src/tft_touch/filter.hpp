#pragma once

//! Turning a burst of noisy XPT2046 readings into one touch point.

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

#include "tft_touch/touch_input.hpp"

namespace tft {

/// Combines raw readings taken during one touch: readings stuck at either end
/// of the range (0 or 4095, common while the finger is landing or lifting)
/// are dropped, then each axis takes its median, which ignores the odd wild
/// reading. Nothing if no usable readings are left.
inline std::optional<Point> median_reading(std::span<const Point> readings) {
    const auto usable = [](std::uint16_t v) { return v >= 1 && v < 4095; };
    std::vector<std::uint16_t> xs, ys;
    for (const auto& [x, y] : readings) {
        if (usable(x) && usable(y)) {
            xs.push_back(x);
            ys.push_back(y);
        }
    }
    if (xs.empty()) {
        return std::nullopt;
    }

    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    return Point{xs[xs.size() / 2], ys[ys.size() / 2]};
}

}  // namespace tft
