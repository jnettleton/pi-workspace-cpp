#include <cmath>
#include <limits>
#include <vector>

#include "check.hpp"
#include "helpers.hpp"
#include "ui.hpp"

using namespace tft;
using namespace helpers;
using namespace ui;

namespace {

using XY = std::pair<std::uint16_t, std::uint16_t>;

/// Pixels of `color` inside button `index`. Only the band between the
/// rounded corners is searched, so the black gap and corners don't count
/// (some labels are black).
std::vector<XY> pixels_in_button(const mock::MockSpi& spi, std::size_t index, Color color) {
    const unsigned x0 = unsigned(index) * BUTTON_W, y0 = 320 - BUTTON_H;
    const unsigned inset = BUTTON_GAP + BUTTON_RADIUS;
    std::vector<XY> found;
    for (unsigned y = y0 + inset; y < y0 + BUTTON_H - inset; ++y) {
        for (unsigned x = x0 + BUTTON_GAP; x < x0 + BUTTON_W - BUTTON_GAP; ++x) {
            if (spi.pixel(x, y) == color.to_rgb666_bytes()) found.push_back({std::uint16_t(x), std::uint16_t(y)});
        }
    }
    return found;
}

std::vector<Rgb666> button_area(const mock::MockSpi& spi, std::size_t index) {
    const unsigned x0 = unsigned(index) * BUTTON_W;
    std::vector<Rgb666> out;
    for (unsigned y = 320 - BUTTON_H; y < 320; ++y) {
        for (unsigned x = x0; x < x0 + BUTTON_W; ++x) out.push_back(spi.pixel(x, y));
    }
    return out;
}

}  // namespace

TEST(ui_buttons_along_the_bottom) {
    Landscape s;
    draw_ui(s.display);

    for (std::size_t index = 0; index < BUTTONS.size(); ++index) {
        const Button& b = BUTTONS[index];
        const unsigned bx = unsigned(index) * BUTTON_W, by = 320 - BUTTON_H;
        const std::string name = "button " + std::to_string(index) + " (" + std::string(b.label) + ")";

        // Filled with its color inside, black gap at the edges, rounded corners
        CHECK_EQ(s.spi.pixel(bx + 4, by + 20), b.button.to_rgb666_bytes(), name, ": fill");
        CHECK_EQ(s.spi.pixel(bx, by + 20), BLACK, name, ": left gap");
        CHECK_EQ(s.spi.pixel(bx + BUTTON_W - 1, by + 20), BLACK, name, ": right gap");
        CHECK_EQ(s.spi.pixel(bx + 4, by), BLACK, name, ": top gap");
        CHECK_EQ(s.spi.pixel(bx + BUTTON_GAP, by + BUTTON_GAP), BLACK, name, ": rounded corner");

        // Label in the text color, centered
        const auto label = pixels_in_button(s.spi, index, b.text);
        CHECK(!label.empty(), name, ": label drawn");
        unsigned min_x = std::numeric_limits<unsigned>::max(), max_x = 0, min_y = min_x, max_y = 0;
        for (const auto& [x, y] : label) {
            min_x = std::min<unsigned>(min_x, x), max_x = std::max<unsigned>(max_x, x);
            min_y = std::min<unsigned>(min_y, y), max_y = std::max<unsigned>(max_y, y);
        }
        const double center_x = (min_x + max_x) / 2.0;
        const double button_center_x = bx + (BUTTON_W - 1) / 2.0;
        CHECK(std::abs(center_x - button_center_x) <= 0.5, name, ": label centered at ", center_x);
        // Size-2 text is 16 rows tall, centered in the 40-row button: rows 12..28
        CHECK(min_y >= by + 12 && max_y < by + 28, name, ": label rows ", min_y, "..=", max_y);
    }
}

TEST(ui_pressed_button_swaps_colors_and_restores) {
    Landscape s;
    draw_ui(s.display);
    std::vector<std::vector<Rgb666>> before;
    for (std::size_t i = 0; i < BUTTONS.size(); ++i) before.push_back(button_area(s.spi, i));

    for (std::size_t index = 0; index < BUTTONS.size(); ++index) {
        const Button& b = BUTTONS[index];
        const unsigned bx = unsigned(index) * BUTTON_W, by = 320 - BUTTON_H;
        draw_button(s.display, index, true);
        CHECK_EQ(s.spi.pixel(bx + 4, by + 20), b.text.to_rgb666_bytes(), "button ", index, ": pressed fill");
        CHECK(!pixels_in_button(s.spi, index, b.button).empty(), "button ", index, ": pressed label");
        // Only this button changed
        for (std::size_t other = 0; other < BUTTONS.size(); ++other) {
            if (other != index) CHECK(button_area(s.spi, other) == before[other], "button ", other, " changed");
        }

        draw_button(s.display, index, false);
        CHECK(button_area(s.spi, index) == before[index], "button ", index, " not restored");
    }
}

TEST(ui_button_hit_testing) {
    using O = std::optional<std::size_t>;
    const std::uint16_t h = 320;
    CHECK_EQ(button_at(0, 279, h), O(), "just above the buttons");
    CHECK_EQ(button_at(0, 280, h), O(0));
    CHECK_EQ(button_at(119, 319, h), O(0));
    CHECK_EQ(button_at(120, 300, h), O(1));
    CHECK_EQ(button_at(240, 300, h), O(2));
    CHECK_EQ(button_at(359, 300, h), O(2));
    CHECK_EQ(button_at(360, 300, h), O(3));
    CHECK_EQ(button_at(479, 319, h), O(3));
    CHECK_EQ(button_at(600, 300, h), O(3), "past the right edge counts as the last button");
    CHECK_EQ(button_at(10, 320, h), O(), "below the screen");
    CHECK_EQ(button_at(10, 10, 20), O(0), "short screen: buttons fill it");
}
