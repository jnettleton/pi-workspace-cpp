#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "check.hpp"
#include "tft_display/shapes.hpp"

using namespace tft::shapes;

namespace {

using Pixels = std::set<std::pair<std::int32_t, std::int32_t>>;

struct Raster {
    Pixels pixels;
    std::size_t rects = 0;
};

/// Runs `draw(emit)` and collects every pixel of every rectangle emitted.
template <class Draw>
Raster rasterize(Draw&& draw) {
    Raster r;
    draw([&](const Rect& rect) {
        if (rect.w <= 0 || rect.h <= 0) throw std::logic_error("empty rect");
        ++r.rects;
        for (std::int64_t px = rect.x; px < rect.x + rect.w; ++px) {
            for (std::int64_t py = rect.y; py < rect.y + rect.h; ++py) {
                r.pixels.insert({std::int32_t(px), std::int32_t(py)});
            }
        }
    });
    return r;
}

Raster line_pixels(int x0, int y0, int x1, int y1) {
    return rasterize([&](auto emit) { line(x0, y0, x1, y1, emit); });
}

/// Plain per-pixel Bresenham to compare against.
Pixels reference_line(int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int x = x0, y = y0, err = dx + dy;
    Pixels pixels;
    for (;;) {
        pixels.insert({x, y});
        if (x == x1 && y == y1) return pixels;
        const int e2 = 2 * err;
        if (e2 >= dy) err += dy, x += sx;
        if (e2 <= dx) err += dx, y += sy;
    }
}

/// Plain per-pixel midpoint circle (8-way symmetry) to compare against.
Pixels reference_circle(int cx, int cy, int r) {
    int x = 0, y = r, d = 1 - r;
    Pixels pixels;
    while (x <= y) {
        for (const auto& [px, py] : {std::pair{x, y}, std::pair{y, x}}) {
            for (const auto& [sx, sy] : {std::pair{1, 1}, std::pair{1, -1}, std::pair{-1, 1}, std::pair{-1, -1}}) {
                pixels.insert({cx + sx * px, cy + sy * py});
            }
        }
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y -= 1;
        }
        x += 1;
    }
    return pixels;
}

std::vector<int> row_of(const Pixels& pixels, int y) {
    std::vector<int> xs;
    for (const auto& [px, py] : pixels) {
        if (py == y) xs.push_back(px);
    }
    std::sort(xs.begin(), xs.end());
    return xs;
}

/// Expected rounded rectangle: a plus-shaped cross of two rectangles, and a
/// filled quarter of the reference circle in each corner.
Pixels reference_round_rectangle(int x, int y, int w, int h, int r) {
    const int left = x + r, right = x + w - 1 - r, top = y + r, bottom = y + h - 1 - r;
    Pixels pixels;
    for (int px = x; px < x + w; ++px) {
        for (int py = y; py < y + h; ++py) {
            if ((px >= left && px <= right) || (py >= top && py <= bottom)) pixels.insert({px, py});
        }
    }
    const Pixels outline = reference_circle(0, 0, r);
    for (const auto& [cx, cy, sx, sy] : {std::tuple{left, top, -1, -1}, std::tuple{right, top, 1, -1},
                                         std::tuple{left, bottom, -1, 1}, std::tuple{right, bottom, 1, 1}}) {
        for (int dy = 0; dy <= r; ++dy) {
            // Widest outline pixel on this row of the circle
            const int reach = row_of(outline, dy).back();
            for (int dx = 0; dx <= reach; ++dx) pixels.insert({cx + sx * dx, cy + sy * dy});
        }
    }
    return pixels;
}

}  // namespace

TEST(lines_match_per_pixel_bresenham) {
    const std::vector<std::array<int, 4>> ends{
        {0, 0, 10, 0}, {0, 0, 0, 10}, {0, 0, 10, 10}, {0, 0, 10, 3},     {0, 0, 3, 10},
        {10, 3, 0, 0}, {5, 5, -7, 2}, {5, 5, 2, -7},  {-20, 15, 479, 300}, {3, 3, 3, 3},
    };
    for (const auto& [x0, y0, x1, y1] : ends) {
        CHECK(line_pixels(x0, y0, x1, y1).pixels == reference_line(x0, y0, x1, y1), "line ", x0, ",", y0, " ", x1,
              ",", y1);
    }
}

TEST(straight_lines_are_one_rect) {
    CHECK_EQ(line_pixels(0, 5, 100, 5).rects, 1u);
    CHECK_EQ(line_pixels(7, 100, 7, 0).rects, 1u);
    // A shallow line is a handful of runs, not one rect per pixel
    const Raster r = line_pixels(0, 0, 200, 10);
    CHECK_EQ(r.pixels.size(), 201u);
    CHECK_EQ(r.rects, 11u);
}

TEST(rectangle_outlines) {
    const auto outline = [](int x, int y, std::uint16_t w, std::uint16_t h) {
        return rasterize([&](auto emit) {
            Pixels seen;
            rectangle(x, y, w, h, [&](const Rect& r) {
                // No pixel is drawn twice
                for (std::int64_t px = r.x; px < r.x + r.w; ++px) {
                    for (std::int64_t py = r.y; py < r.y + r.h; ++py) {
                        if (!seen.insert({int(px), int(py)}).second) throw std::logic_error("pixel drawn twice");
                    }
                }
                emit(r);
            });
        });
    };
    const auto border = [](int x, int y, int w, int h) {
        Pixels p;
        for (int px = x; px < x + w; ++px) {
            for (int py = y; py < y + h; ++py) {
                if (px == x || px == x + w - 1 || py == y || py == y + h - 1) p.insert({px, py});
            }
        }
        return p;
    };

    const std::vector<std::array<int, 4>> cases{{0, 0, 10, 5}, {-3, 7, 4, 3}, {5, 5, 3, 3},
                                                {2, 2, 1, 1},  {0, 0, 2, 9},  {0, 0, 9, 1}};
    for (const auto& [x, y, w, h] : cases) {
        const Raster r = outline(x, y, std::uint16_t(w), std::uint16_t(h));
        CHECK(r.pixels == border(x, y, w, h), x, ",", y, " ", w, "x", h);
        CHECK(r.rects <= 4);
    }
    CHECK(outline(5, 5, 0, 10).pixels.empty());
    CHECK(outline(5, 5, 10, 0).pixels.empty());
}

TEST(circles_match_per_pixel_midpoint) {
    for (const std::uint16_t r : {0, 1, 2, 3, 5, 10, 37, 100, 159}) {
        const Raster got = rasterize([&](auto emit) { circle(50, 40, r, emit); });
        const Pixels expected = reference_circle(50, 40, r);
        CHECK(got.pixels == expected, "radius ", r);
        CHECK(got.rects <= expected.size(), "radius ", r, ": ", got.rects, " rects");
        if (r >= 10) {
            // Runs should cut the number of transfers well below one per pixel
            CHECK(got.rects * 2 <= expected.size(), "radius ", r, ": ", got.rects, " rects for ", expected.size(),
                  " px");
        }
    }
}

TEST(filled_circle_covers_outline_and_inside) {
    for (const std::uint16_t radius : {0, 1, 2, 5, 10, 37, 100}) {
        const Pixels filled = rasterize([&](auto emit) { filled_circle(0, 0, radius, emit); }).pixels;
        const Pixels outline = reference_circle(0, 0, radius);
        CHECK(std::includes(filled.begin(), filled.end(), outline.begin(), outline.end()), "radius ", radius,
              ": outline not covered");

        // Each row is one solid span bounded by the outline
        const int r = radius;
        for (int y = -r; y <= r; ++y) {
            const auto row = row_of(filled, y);
            const auto outline_row = row_of(outline, y);
            CHECK_EQ(int(row.size()), row.back() - row.front() + 1, "radius ", r, " row ", y, " has gaps");
            CHECK_EQ(row.front(), outline_row.front(), "radius ", r, " row ", y);
            CHECK_EQ(row.back(), outline_row.back(), "radius ", r, " row ", y);
        }
        for (const auto& p : filled) CHECK(std::abs(p.second) <= r, "radius ", r, ": rows outside circle");
    }
}

TEST(filled_round_rectangles) {
    // x, y, w, h, radius, effective radius
    const std::vector<std::array<int, 6>> cases{
        {0, 0, 100, 40, 10, 10},
        {5, -3, 37, 22, 7, 7},
        {0, 0, 60, 20, 50, 9},   // radius reduced to fit: (20 - 1) / 2
        {0, 0, 21, 21, 10, 10},  // radius = half: a circle
        {0, 0, 50, 21, 10, 10},  // pill shape
        {0, 0, 20, 30, 3, 3},
        {0, 0, 2, 2, 5, 0},
        {-10, -10, 15, 8, 0, 0},
    };
    for (const auto& [x, y, w, h, radius, effective] : cases) {
        const Raster got = rasterize([&](auto emit) {
            filled_round_rectangle(x, y, std::uint16_t(w), std::uint16_t(h), std::uint16_t(radius), emit);
        });
        CHECK(got.pixels == reference_round_rectangle(x, y, w, h, effective), x, ",", y, " ", w, "x", h, " r",
              radius);
        CHECK(got.rects <= std::size_t(h), got.rects, " rects for ", h, " rows");
    }
    CHECK(rasterize([](auto emit) { filled_round_rectangle(0, 0, 0, 10, 3, emit); }).pixels.empty());
    CHECK(rasterize([](auto emit) { filled_round_rectangle(0, 0, 10, 0, 3, emit); }).pixels.empty());
}
