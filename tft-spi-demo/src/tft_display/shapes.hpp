#pragma once

//! Rasterizes lines and circles into horizontal/vertical runs of pixels.
//!
//! Each run is passed to `emit` as a rectangle `{x, y, w, h}` with `w == 1` or
//! `h == 1` (filled circles also produce wider blocks). Sending runs instead of
//! single pixels means far fewer SPI transfers. Coordinates are signed and may
//! be off-screen; the caller clips.
//!
//! The math is done in `int64_t`, so no `int32_t` coordinate or `uint16_t`
//! size can overflow. Note that the work is proportional to the shape's size,
//! not the visible part: a line millions of pixels long takes millions of steps.

#include <algorithm>
#include <cstdint>
#include <ostream>

namespace tft::shapes {

/// `{x, y, width, height}`
struct Rect {
    std::int64_t x;
    std::int64_t y;
    std::int64_t w;
    std::int64_t h;

    friend bool operator==(const Rect&, const Rect&) = default;
};

inline std::ostream& operator<<(std::ostream& os, const Rect& r) {
    return os << '(' << r.x << ", " << r.y << ", " << r.w << ", " << r.h << ')';
}

namespace detail {

constexpr std::int64_t abs64(std::int64_t v) { return v < 0 ? -v : v; }

/// Rectangle covering a run between two points on the same row or column.
constexpr Rect run(std::int64_t ax, std::int64_t ay, std::int64_t bx, std::int64_t by) {
    return {std::min(ax, bx), std::min(ay, by), abs64(bx - ax) + 1, abs64(by - ay) + 1};
}

/// Walks the octant from 12 o'clock to 45° (x from 0 up to y) and reports each
/// run of consecutive x values that share the same y as `(x_start, x_end, y)`.
template <class Run>
void octant_runs(std::uint16_t radius, Run&& run) {
    const std::int64_t r = radius;
    std::int64_t x = 0;
    std::int64_t y = r;
    std::int64_t d = 1 - r;
    std::int64_t run_start = 0;

    while (x <= y) {
        const std::int64_t cur_x = x;
        const std::int64_t cur_y = y;
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y -= 1;
        }
        x += 1;

        if (y != cur_y || x > y) {
            run(run_start, cur_x, cur_y);
            run_start = x;
        }
    }
}

template <class Emit>
void round_rect(std::int64_t x, std::int64_t y, std::int64_t w, std::int64_t h, std::int64_t radius, Emit&& emit) {
    if (w <= 0 || h <= 0) {
        return;
    }
    const std::int64_t r = std::min({radius, (w - 1) / 2, (h - 1) / 2});
    if (r == 0) {
        emit(Rect{x, y, w, h});
        return;
    }

    // Centers of the corner circles
    const std::int64_t left = x + r;
    const std::int64_t right = x + w - 1 - r;
    const std::int64_t top = y + r;
    const std::int64_t bottom = y + h - 1 - r;
    const std::int64_t stretch = right - left;  // extra width between the left and right corners

    // r <= radius, which came from a uint16_t
    octant_runs(static_cast<std::uint16_t>(r), [&](std::int64_t x_start, std::int64_t x_end, std::int64_t dy) {
        const std::int64_t len = x_end - x_start + 1;
        // Rows top - dy / bottom + dy: from the left corner's edge to the right corner's
        emit(Rect{left - x_end, top - dy, stretch + 2 * x_end + 1, 1});
        emit(Rect{left - x_end, bottom + dy, stretch + 2 * x_end + 1, 1});
        // Rows top - x / bottom + x for x in x_start..=x_end: span left - dy ..= right + dy
        emit(Rect{left - dy, top - x_end, stretch + 2 * dy + 1, len});
        emit(Rect{left - dy, bottom + x_start, stretch + 2 * dy + 1, len});
    });

    // Full-width block between the top and bottom corner rows
    if (bottom - top > 1) {
        emit(Rect{x, top + 1, w, bottom - top - 1});
    }
}

}  // namespace detail

/// Bresenham line from (x0, y0) to (x1, y1), both ends included.
template <class Emit>
void line(std::int32_t x0_in, std::int32_t y0_in, std::int32_t x1_in, std::int32_t y1_in, Emit&& emit) {
    const std::int64_t x0 = x0_in, y0 = y0_in, x1 = x1_in, y1 = y1_in;
    const std::int64_t dx = detail::abs64(x1 - x0);
    const std::int64_t dy = -detail::abs64(y1 - y0);
    const std::int64_t sx = x0 < x1 ? 1 : -1;
    const std::int64_t sy = y0 < y1 ? 1 : -1;
    // Mostly-horizontal lines are emitted as horizontal runs, steep ones as vertical runs.
    const bool horizontal = dx >= -dy;

    std::int64_t err = dx + dy;
    std::int64_t x = x0, y = y0;
    std::int64_t run_x = x0, run_y = y0;

    for (;;) {
        if (x == x1 && y == y1) {
            emit(detail::run(run_x, run_y, x, y));
            return;
        }

        std::int64_t next_x = x, next_y = y;
        const std::int64_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            next_x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            next_y += sy;
        }

        const bool leaves_run = horizontal ? next_y != y : next_x != x;
        if (leaves_run) {
            emit(detail::run(run_x, run_y, x, y));
            run_x = next_x;
            run_y = next_y;
        }
        x = next_x;
        y = next_y;
    }
}

/// 1-pixel rectangle outline with its top-left corner at (x, y). Each pixel is
/// drawn once: top and bottom edges span the full width, the sides fill in
/// between them.
template <class Emit>
void rectangle(std::int32_t x_in, std::int32_t y_in, std::uint16_t w_in, std::uint16_t h_in, Emit&& emit) {
    const std::int64_t x = x_in, y = y_in, w = w_in, h = h_in;
    if (w == 0 || h == 0) {
        return;
    }
    if (w <= 2 || h <= 2) {
        // No inside to leave empty: it's a solid block
        emit(Rect{x, y, w, h});
        return;
    }

    emit(Rect{x, y, w, 1});              // top
    emit(Rect{x, y + h - 1, w, 1});      // bottom
    emit(Rect{x, y + 1, 1, h - 2});      // left
    emit(Rect{x + w - 1, y + 1, 1, h - 2});  // right
}

/// Circle outline centered on (cx, cy) (midpoint algorithm).
template <class Emit>
void circle(std::int32_t cx_in, std::int32_t cy_in, std::uint16_t radius, Emit&& emit) {
    const std::int64_t cx = cx_in, cy = cy_in;
    if (radius == 0) {
        emit(Rect{cx, cy, 1, 1});
        return;
    }
    detail::octant_runs(radius, [&](std::int64_t x_start, std::int64_t x_end, std::int64_t y) {
        const std::int64_t len = x_end - x_start + 1;
        if (x_start == 0) {
            // Runs touching the axes: one span across the axis instead of two.
            const std::int64_t width = 2 * x_end + 1;
            emit(Rect{cx - x_end, cy - y, width, 1});  // top
            emit(Rect{cx - x_end, cy + y, width, 1});  // bottom
            emit(Rect{cx - y, cy - x_end, 1, width});  // left
            emit(Rect{cx + y, cy - x_end, 1, width});  // right
        } else {
            for (const std::int64_t s : {-1, 1}) {
                // top / bottom, both sides
                emit(Rect{cx + x_start, cy + s * y, len, 1});
                emit(Rect{cx - x_end, cy + s * y, len, 1});
            }
            for (const std::int64_t s : {-1, 1}) {
                // left / right, both halves
                emit(Rect{cx + s * y, cy + x_start, 1, len});
                emit(Rect{cx + s * y, cy - x_end, 1, len});
            }
        }
    });
}

/// Filled circle centered on (cx, cy), covering the same pixels as `circle`
/// plus everything inside it.
template <class Emit>
void filled_circle(std::int32_t cx, std::int32_t cy, std::uint16_t radius, Emit&& emit) {
    const std::int64_t r = radius;
    // The diameter can exceed uint16_t, so this goes straight to the int64 version.
    detail::round_rect(std::int64_t{cx} - r, std::int64_t{cy} - r, 2 * r + 1, 2 * r + 1, r, emit);
}

/// Filled rectangle with its top-left corner at (x, y) and corners rounded
/// with `radius`. The radius is reduced to fit if it's more than half the
/// shorter side, so a large radius gives a pill or circle shape.
///
/// Each corner is a quarter of a `filled_circle`, so the curves match
/// `circle` pixel for pixel.
template <class Emit>
void filled_round_rectangle(std::int32_t x, std::int32_t y, std::uint16_t w, std::uint16_t h, std::uint16_t radius,
                            Emit&& emit) {
    detail::round_rect(x, y, w, h, radius, emit);
}

}  // namespace tft::shapes
