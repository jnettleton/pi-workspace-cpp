#include <vector>

#include "check.hpp"
#include "tft_display/color.hpp"
#include "tft_spi.hpp"

using namespace tft;
using V = std::vector<std::uint8_t>;

namespace {
V data(std::uint8_t cmd, const V& bytes) { return bus16::data(cmd, bytes); }
V pixels_of(std::initializer_list<Color> colors) {
    V rgb;
    for (const Color& c : colors) {
        const auto b = c.to_rgb666_bytes();
        rgb.insert(rgb.end(), b.begin(), b.end());
    }
    return bus16::pixels(rgb);
}
}  // namespace

TEST(bus16_commands_and_parameters_are_16_bit_words) {
    CHECK_EQ(bus16::command(0x36), (std::array<std::uint8_t, 2>{0x00, 0x36}));
    CHECK_EQ(data(0x36, {0x28}), (V{0x00, 0x28}));
    // CASET 0..479
    CHECK_EQ(data(0x2A, {0x00, 0x00, 0x01, 0xDF}), (V{0, 0x00, 0, 0x00, 0, 0x01, 0, 0xDF}));
    CHECK(data(0x2A, {}).empty());
}

TEST(bus16_pixel_format_becomes_16_bit) { CHECK_EQ(data(0x3A, {0x66}), (V{0x00, 0x55})); }

TEST(bus16_pixels_become_rgb565) {
    CHECK_EQ(pixels_of({Color::RED}), (V{0xF8, 0x00}));
    CHECK_EQ(pixels_of({Color::GREEN}), (V{0x07, 0xE0}));
    CHECK_EQ(pixels_of({Color::BLUE}), (V{0x00, 0x1F}));
    CHECK_EQ(pixels_of({Color::WHITE}), (V{0xFF, 0xFF}));
    CHECK_EQ(pixels_of({Color::BLACK}), (V{0x00, 0x00}));

    V row;
    for (const Color& c : {Color::RED, Color::BLUE}) {
        const auto b = c.to_rgb666_bytes();
        row.insert(row.end(), b.begin(), b.end());
    }
    CHECK_EQ(data(0x2C, row), (V{0xF8, 0x00, 0x00, 0x1F}));
}

TEST(bus16_every_transfer_is_whole_words) {
    const std::vector<std::pair<std::uint8_t, V>> cases{
        {0x36, V{0x28}}, {0xE0, V(15, 0x0F)}, {0x2C, V(3 * 7, 0xFC)}};
    for (const auto& [cmd, bytes] : cases) {
        CHECK_EQ(data(cmd, bytes).size() % 2, 0u, "cmd ", int(cmd));
    }
}
