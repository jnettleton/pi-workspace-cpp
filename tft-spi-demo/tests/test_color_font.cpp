#include <array>
#include <vector>

#include "check.hpp"
#include "tft_display/color.hpp"
#include "tft_display/font.hpp"

using namespace tft;

namespace {
std::vector<std::uint8_t> bytes(std::span<const std::uint8_t> s) { return {s.begin(), s.end()}; }
}  // namespace

TEST(rgb666_conversion) {
    CHECK_EQ(Color::WHITE.to_rgb666_bytes(), (Rgb666{0xFC, 0xFC, 0xFC}));
    CHECK_EQ(Color::BLACK.to_rgb666_bytes(), (Rgb666{0x00, 0x00, 0x00}));
    CHECK_EQ(Color::RED.to_rgb666_bytes(), (Rgb666{0xFC, 0x00, 0x00}));
    CHECK_EQ(Color::BLUE.to_rgb666_bytes(), (Rgb666{0x00, 0x00, 0xFC}));
    CHECK_EQ(Color::rgb(0x13, 0x26, 0x11).to_rgb666_bytes(), (Rgb666{0x10, 0x24, 0x10}));
}

TEST(color_from_24_bit) {
    CHECK_EQ(Color::from_24_bit(0xFF8000), std::optional<Color>(Color::rgb(0xFF, 0x80, 0x00)));
    CHECK(!Color::from_24_bit(0x1'000'000));
}

TEST(glyph_lookup_spans_both_tables) {
    using V = std::vector<std::uint8_t>;
    CHECK_EQ(bytes(font::glyph(U' ')), (V{0x00, 0x00, 0x00, 0x00, 0x00}));
    CHECK_EQ(bytes(font::glyph(U'R')), (V{0x7F, 0x09, 0x19, 0x29, 0x46}));
    CHECK_EQ(bytes(font::glyph(U'S')), (V{0x26, 0x49, 0x49, 0x49, 0x32}));
    CHECK_EQ(bytes(font::glyph(U'~')), (V{0x02, 0x01, 0x02, 0x04, 0x02}));
    CHECK_EQ(bytes(font::glyph(U'é')), bytes(font::glyph(U'?')));
    CHECK_EQ(bytes(font::glyph(U'\n')), bytes(font::glyph(U'?')));
}
