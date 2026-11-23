//! Tests for `TftDisplay` against the mock ILI9486 in `mock_spi.cpp`, which
//! paints what the display sends into a framebuffer so tests can check the screen.

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "helpers.hpp"
#include "tft_display/font.hpp"
#include "util/utf8.hpp"

using namespace tft;
using namespace helpers;
using mock::GRAM_H;
using mock::GRAM_W;
using mock::MADCTL;
using mock::MockSpi;

namespace {

/// Test image with (nearly) every pixel different: R from x, G from y, B from both.
std::vector<std::uint8_t> gradient(std::uint16_t w, std::uint16_t h) {
    std::vector<std::uint8_t> rgb;
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) {
            rgb.push_back(std::uint8_t(x * 4));
            rgb.push_back(std::uint8_t(y * 4));
            rgb.push_back(std::uint8_t((x ^ y) * 4));
        }
    }
    return rgb;
}

/// Source pixel as the panel should receive it (RGB666: low 2 bits cleared).
Rgb666 src_pixel(const std::vector<std::uint8_t>& rgb, std::uint16_t w, std::size_t x, std::size_t y) {
    const std::size_t i = (y * w + x) * 3;
    return {std::uint8_t(rgb[i] & 0xFC), std::uint8_t(rgb[i + 1] & 0xFC), std::uint8_t(rgb[i + 2] & 0xFC)};
}

/// Checks the panel against an image drawn at (ox, oy): every on-screen
/// image pixel matches, the rest is untouched (black), and nothing was sent
/// past the window. Returns the number of visible pixels.
std::size_t check_placement(const MockSpi& p, const std::vector<std::uint8_t>& rgb, std::uint16_t w, std::uint16_t h,
                            int ox, int oy) {
    std::size_t visible = 0;
    for (std::size_t y = 0; y < p.height; ++y) {
        for (std::size_t x = 0; x < p.width; ++x) {
            const int ix = int(x) - ox, iy = int(y) - oy;
            Rgb666 expected = BLACK;
            if (ix >= 0 && iy >= 0 && ix < w && iy < h) {
                ++visible;
                expected = src_pixel(rgb, w, std::size_t(ix), std::size_t(iy));
            }
            CHECK_EQ(p.pixel(x, y), expected, "screen (", x, ", ", y, "), image at (", ox, ", ", oy, ")");
        }
    }
    CHECK_EQ(p.overflow, 0u, "pixels sent past the window");
    CHECK_EQ(p.pixels_written, visible, "pixels sent vs visible");
    return visible;
}

/// Checks every panel pixel against `expected(x, y)` and that nothing was
/// sent past an address window. Returns the number of pixels sent.
std::size_t check_frame(const MockSpi& p, const std::function<Rgb666(std::size_t, std::size_t)>& expected) {
    for (std::size_t y = 0; y < p.height; ++y) {
        for (std::size_t x = 0; x < p.width; ++x) {
            const Rgb666 want = expected(x, y);
            if (p.pixel(x, y) != want) {
                CHECK_EQ(p.pixel(x, y), want, "pixel (", x, ", ", y, ")");
            }
        }
    }
    CHECK_EQ(p.overflow, 0u, "pixels sent past the window");
    return p.pixels_written;
}

/// Checks `total` pixels were sent in as few transfers as possible: every
/// transfer holds MAX_TRANSFER_PIXELS except the last, which holds the rest.
void check_full_transfers(const MockSpi& spi, std::size_t total) {
    constexpr std::size_t max = TftDisplay::MAX_TRANSFER_PIXELS;
    const std::size_t count = (total + max - 1) / max;
    const auto& transfers = spi.pixel_transfers;
    CHECK_EQ(transfers.size(), count, "transfers for ", total, " pixels");
    for (std::size_t i = 0; i + 1 < transfers.size(); ++i) {
        CHECK_EQ(transfers[i], max * 3, "transfer ", i);
    }
    CHECK_EQ(transfers.back(), (total - (count - 1) * max) * 3, "last transfer");
}

/// Whether (x, y) is inside the w x h rectangle at (rx, ry).
bool inside(std::size_t x, std::size_t y, std::size_t rx, std::size_t ry, std::size_t w, std::size_t h) {
    return x >= rx && x < rx + w && y >= ry && y < ry + h;
}

}  // namespace

TEST(draw_image_on_screen_and_clipped) {
    const std::uint16_t w = 50, h = 40;
    const auto rgb = gradient(w, h);
    // On screen, clipped left/top, clipped right/bottom, clipped by one pixel
    const std::vector<std::array<int, 3>> cases{
        {100, 50, 50 * 40}, {-10, -5, 40 * 35}, {460, 300, 20 * 20}, {-1, -1, 49 * 39}};
    for (const auto& [ox, oy, visible] : cases) {
        Landscape s;
        s.display.draw_image(ox, oy, w, h, rgb);
        CHECK_EQ(check_placement(s.spi, rgb, w, h, ox, oy), std::size_t(visible), "image at (", ox, ", ", oy, ")");
    }

    // Bigger than the screen on every side
    const auto big = gradient(600, 400);
    Landscape s;
    s.display.draw_image(-60, -40, 600, 400, big);
    CHECK_EQ(check_placement(s.spi, big, 600, 400, -60, -40), 480u * 320u);
}

TEST(draw_image_off_screen_sends_nothing) {
    const auto rgb = gradient(50, 40);
    for (const auto& [ox, oy] : {std::pair{480, 0}, {0, 320}, {-50, 0}, {0, -40}, {1000, 1000}}) {
        Landscape s;
        s.display.draw_image(ox, oy, 50, 40, rgb);
        CHECK(s.spi.pixel_transfers.empty(), "image at (", ox, ", ", oy, ")");
    }
}

TEST(draw_image_rejects_wrong_buffer_size) {
    Landscape s;
    const std::vector<std::uint8_t> short_buffer(50 * 40 * 3 - 1);
    const auto err = CHECK_THROWS(ImageSizeError, s.display.draw_image(0, 0, 50, 40, short_buffer));
    CHECK_EQ(err.given, 5999u);
    CHECK_EQ(err.expected, 6000u);
    CHECK(s.spi.pixel_transfers.empty());
}

TEST(full_screen_image_transfers) {
    const auto rgb = gradient(480, 320);
    Landscape s;
    s.display.draw_image(0, 0, 480, 320, rgb);
    check_placement(s.spi, rgb, 480, 320, 0, 0);

    // 113 transfers of up to 1365 pixels, not one per row
    check_full_transfers(s.spi, 480 * 320);
}

/// A clipped image still goes out in full transfers, which run across rows.
TEST(clipped_image_transfers_span_rows) {
    const auto rgb = gradient(300, 200);
    Landscape s;
    s.display.draw_image(-50, 250, 300, 200, rgb);
    // 250 x 70 visible
    check_placement(s.spi, rgb, 300, 200, -50, 250);
    check_full_transfers(s.spi, 250 * 70);
}

// ---- fill_rectangle ----

TEST(fill_rectangle_on_screen) {
    Landscape s;
    s.display.fill_rectangle(10, 20, 30, 15, Color::RED);
    const Rgb666 red = Color::RED.to_rgb666_bytes();
    const auto sent = check_frame(s.spi, [&](auto x, auto y) { return inside(x, y, 10, 20, 30, 15) ? red : BLACK; });
    CHECK_EQ(sent, 30u * 15u);
}

TEST(fill_rectangle_clipped_at_right_and_bottom) {
    Landscape s;
    s.display.fill_rectangle(470, 310, 50, 50, Color::GREEN);
    const Rgb666 green = Color::GREEN.to_rgb666_bytes();
    const auto sent =
        check_frame(s.spi, [&](auto x, auto y) { return inside(x, y, 470, 310, 10, 10) ? green : BLACK; });
    CHECK_EQ(sent, 10u * 10u);
}

TEST(fill_rectangle_empty_or_off_screen_sends_nothing) {
    const std::vector<std::array<std::uint16_t, 4>> cases{
        {10, 10, 0, 5}, {10, 10, 5, 0}, {480, 0, 10, 10}, {0, 320, 10, 10}, {0xFFFF, 5, 1, 1}};
    for (const auto& [x, y, w, h] : cases) {
        Landscape s;
        s.display.fill_rectangle(x, y, w, h, Color::WHITE);
        CHECK(s.spi.pixel_transfers.empty(), "rect ", x, ",", y, " ", w, "x", h);
    }
}

TEST(fill_screen_in_each_rotation) {
    for (const Rotate rotation : {Rotate::Degrees0, Rotate::Degrees90, Rotate::Degrees180, Rotate::Degrees270}) {
        // The mock's `pixels` ignore MADCTL, so it just needs to match the logical size
        Screen s(rotation);
        const std::size_t w = s.spi.width, h = s.spi.height;
        CHECK_EQ(std::size_t(s.display.width()), w);
        CHECK_EQ(std::size_t(s.display.height()), h);

        s.display.fill_screen(Color::BLUE);
        const Rgb666 blue = Color::BLUE.to_rgb666_bytes();
        CHECK_EQ(check_frame(s.spi, [&](auto, auto) { return blue; }), w * h, rotation);

        // Full transfers, like draw_image
        check_full_transfers(s.spi, w * h);
    }
}

/// Each character cell goes out in one transfer, not one per row.
TEST(draw_text_sends_one_transfer_per_character) {
    {
        Landscape s;
        s.display.draw_text(0, 0, "Hi!", Color::WHITE, Color::BLUE, 2);
        CHECK_EQ(s.spi.pixel_transfers, (std::vector<std::size_t>(3, 12 * 16 * 3)));
    }
    {
        // A character too big for one transfer: size 10 is 60 x 80 = 4800 pixels
        Landscape s;
        s.display.draw_text(0, 0, "W", Color::WHITE, Color::BLUE, 10);
        check_full_transfers(s.spi, 60 * 80);
    }
}

TEST(fill_rectangle_overwrites) {
    Landscape s;
    s.display.fill_rectangle(0, 0, 100, 100, Color::RED);
    s.display.fill_rectangle(50, 50, 100, 100, Color::BLUE);
    const Rgb666 red = Color::RED.to_rgb666_bytes(), blue = Color::BLUE.to_rgb666_bytes();
    check_frame(s.spi, [&](auto x, auto y) {
        if (inside(x, y, 50, 50, 100, 100)) return blue;
        if (inside(x, y, 0, 0, 100, 100)) return red;
        return BLACK;
    });
}

// ---- draw_text ----

namespace {

/// How a piece of text was drawn, for working out the expected pixels.
struct TextAt {
    std::string text;
    std::size_t x, y, size;
    Color fg, bg;

    /// Expected color of pixel (x, y), built from the font's column bytes
    /// (bit 0 = top row). Black outside the text's character cells.
    Rgb666 pixel(std::size_t px, std::size_t py) const {
        const std::size_t cell_w = 6 * size, cell_h = 8 * size;
        std::size_t line_index = 0, char_index = 0;
        for (const char32_t c : util::decode_utf8(text + "\n")) {
            if (c == U'\n') {
                ++line_index;
                char_index = 0;
                continue;
            }
            const std::size_t cell_x = x + char_index * cell_w, line_y = y + line_index * cell_h;
            ++char_index;
            if (inside(px, py, cell_x, line_y, cell_w, cell_h)) {
                const std::size_t col = (px - cell_x) / size, row = (py - line_y) / size;
                // Column 5 is the 1-pixel gap after the glyph
                const bool on = col < 5 && ((font::glyph(c)[col] >> row) & 1) == 1;
                return (on ? fg : bg).to_rgb666_bytes();
            }
        }
        return BLACK;
    }

    /// Checks the whole panel shows just this text. Returns pixels sent.
    std::size_t check(const MockSpi& spi) const {
        return check_frame(spi, [&](auto px, auto py) { return pixel(px, py); });
    }
};

}  // namespace

TEST(draw_text_single_character) {
    Landscape s;
    s.display.draw_text(5, 7, "A", Color::WHITE, Color::BLUE, 1);
    CHECK_EQ((TextAt{"A", 5, 7, 1, Color::WHITE, Color::BLUE}.check(s.spi)), 6u * 8u, "one 6x8 cell");

    // Spot check 'A' (0x7C, 0x12, 0x11, 0x12, 0x7C): its top row is only the middle column
    const Rgb666 white = Color::WHITE.to_rgb666_bytes(), blue = Color::BLUE.to_rgb666_bytes();
    const std::vector<Rgb666> top_row{s.spi.pixel(5, 7), s.spi.pixel(6, 7), s.spi.pixel(7, 7), s.spi.pixel(8, 7),
                                      s.spi.pixel(9, 7)};
    CHECK_EQ(top_row, (std::vector<Rgb666>{blue, blue, white, blue, blue}));
    CHECK_EQ(s.spi.pixel(10, 7), blue, "gap column");
}

TEST(draw_text_scaled) {
    for (const std::uint8_t size : {2, 3, 4}) {
        Landscape s;
        s.display.draw_text(20, 30, "Rust!", Color::YELLOW, Color::BLACK, size);
        const std::size_t sz = size;
        CHECK_EQ((TextAt{"Rust!", 20, 30, sz, Color::YELLOW, Color::BLACK}.check(s.spi)), 5 * (6 * sz) * (8 * sz),
                 "size ", sz);
    }
}

TEST(draw_text_newlines_and_descenders) {
    const std::string text = "gyp\nAB\n\nq";
    Landscape s;
    s.display.draw_text(3, 4, text, Color::GREEN, Color::GREY, 2);
    TextAt{text, 3, 4, 2, Color::GREEN, Color::GREY}.check(s.spi);
}

TEST(draw_text_size_zero_is_size_one) {
    Landscape s;
    s.display.draw_text(0, 0, "x", Color::WHITE, Color::RED, 0);
    TextAt{"x", 0, 0, 1, Color::WHITE, Color::RED}.check(s.spi);
}

TEST(draw_text_unknown_characters_draw_question_mark) {
    Landscape s;
    s.display.draw_text(0, 0, "\xC3\xA9", Color::WHITE, Color::RED, 1);  // é in UTF-8: one character
    TextAt{"?", 0, 0, 1, Color::WHITE, Color::RED}.check(s.spi);
}

TEST(draw_text_clipped_at_edges) {
    {
        // Runs off the right edge: "WWW" at x = 470 leaves 10 visible columns
        Landscape s;
        s.display.draw_text(470, 0, "WWW", Color::WHITE, Color::BLUE, 1);
        CHECK_EQ((TextAt{"WWW", 470, 0, 1, Color::WHITE, Color::BLUE}.check(s.spi)), 10u * 8u);
    }
    {
        // Runs off the bottom: size 2 at y = 310 leaves 10 visible rows
        Landscape s;
        s.display.draw_text(0, 310, "Hi", Color::WHITE, Color::BLUE, 2);
        CHECK_EQ((TextAt{"Hi", 0, 310, 2, Color::WHITE, Color::BLUE}.check(s.spi)), 2u * 12u * 10u);
    }
    {
        // Entirely off screen
        Landscape s;
        s.display.draw_text(480, 0, "off", Color::WHITE, Color::BLUE, 1);
        s.display.draw_text(0, 320, "off", Color::WHITE, Color::BLUE, 1);
        CHECK(s.spi.pixel_transfers.empty());
    }
}

TEST(text_width) {
    CHECK_EQ(TftDisplay::text_width("", 1), 0);
    CHECK_EQ(TftDisplay::text_width("abc", 1), 18);
    CHECK_EQ(TftDisplay::text_width("abc", 2), 36);
    CHECK_EQ(TftDisplay::text_width("abc", 0), 18, "size 0 counts as 1");
    CHECK_EQ(TftDisplay::text_width("ab\nlonger\nc", 1), 36, "longest line");
    // Very long text saturates instead of wrapping around
    CHECK_EQ(TftDisplay::text_width(std::string(20'000, 'x'), 1), 0xFFFF);
    CHECK_EQ(TftDisplay::text_width(std::string(65'537, 'x'), 1), 0xFFFF, "more chars than uint16_t");
}

/// Windows line endings ("\r\n") draw the same as "\n".
TEST(draw_text_ignores_carriage_returns) {
    Landscape s;
    s.display.draw_text(2, 3, "one\r\ntwo\r\n\r\nfour", Color::WHITE, Color::RED, 2);
    TextAt{"one\ntwo\n\nfour", 2, 3, 2, Color::WHITE, Color::RED}.check(s.spi);
}

/// text_width agrees with what draw_text actually covers (character cells
/// drawn in the background color), including odd line endings.
TEST(text_width_matches_drawn_width) {
    for (const char* text :
         {"abc", "ab\nlonger\nc", "ab\r\ncd", "trailing\n", "\n\nx", "tab\there", "\xC3\xA9t\xC3\xA9"}) {
        Landscape s;
        s.display.draw_text(0, 0, text, Color::WHITE, Color::BLUE, 1);
        std::size_t drawn_width = 0;
        for (std::size_t x = 0; x < s.spi.width; ++x) {
            for (std::size_t y = 0; y < s.spi.height; ++y) {
                if (s.spi.pixel(x, y) != BLACK) {
                    drawn_width = x + 1;
                    break;
                }
            }
        }
        CHECK_EQ(std::size_t(TftDisplay::text_width(text, 1)), drawn_width, check::show(std::string_view(text)));
    }
}

// ---- rotations ----

namespace {

constexpr std::array<Rotate, 4> ROTATIONS{Rotate::Degrees0, Rotate::Degrees90, Rotate::Degrees180,
                                          Rotate::Degrees270};

using Vec = std::pair<int, int>;

/// Where a pixel of `color` ended up in GRAM. Fails unless exactly one did.
Vec find_in_gram(const MockSpi& spi, Color color) {
    const Rgb666 target = color.to_rgb666_bytes();
    std::vector<std::size_t> found;
    for (std::size_t i = 0; i < spi.gram.size(); ++i) {
        if (spi.gram[i] == target) found.push_back(i);
    }
    CHECK_EQ(found.size(), 1u, "expected one ", color, " pixel in GRAM");
    return {int(found[0] % GRAM_W), int(found[0] / GRAM_W)};
}

/// How a rotation lays the logical screen onto GRAM: where logical (0, 0)
/// lands, and the GRAM step for +1 in logical x and in logical y.
struct Orientation {
    Vec origin, x_step, y_step;
};

Orientation orientation(Rotate rotation) {
    Screen s(rotation);
    s.display.draw_pixel(0, 0, Color::RED);
    s.display.draw_pixel(1, 0, Color::GREEN);
    s.display.draw_pixel(0, 1, Color::BLUE);
    const Vec origin = find_in_gram(s.spi, Color::RED);
    const Vec g = find_in_gram(s.spi, Color::GREEN);
    const Vec b = find_in_gram(s.spi, Color::BLUE);
    return {origin, {g.first - origin.first, g.second - origin.second},
            {b.first - origin.first, b.second - origin.second}};
}

/// Quarter turn of a GRAM step vector: (dx, dy) -> (-dy, dx).
Vec quarter_turn(Vec v) { return {-v.second, v.first}; }

}  // namespace

/// The MADCTL values were checked on the actual panel with tft-spi-demo's
/// rotation test (green block in the bottom-left corner in every rotation).
/// This pins them so a refactor can't silently change the orientation.
TEST(rotation_madctl_values_match_hardware) {
    const std::array<std::pair<Rotate, std::uint8_t>, 4> cases{
        {{Rotate::Degrees0, 0x88}, {Rotate::Degrees90, 0x28}, {Rotate::Degrees180, 0x48}, {Rotate::Degrees270, 0xE8}}};
    for (const auto& [rotation, expected] : cases) {
        Screen s(rotation);
        CHECK_EQ(s.spi.commands.back(), (mock::Command{MADCTL, {expected}}), rotation);
        CHECK(s.spi.pixel_transfers.empty(), rotation, ": set_rotation sent pixels");
    }
}

TEST(rotation_dimensions_and_degrees) {
    const std::vector<std::tuple<Rotate, std::uint16_t, std::uint16_t, std::uint16_t>> cases{
        {Rotate::Degrees0, 320, 480, 0},
        {Rotate::Degrees90, 480, 320, 90},
        {Rotate::Degrees180, 320, 480, 180},
        {Rotate::Degrees270, 480, 320, 270},
    };
    for (const auto& [rotation, w, h, deg] : cases) {
        Screen s(rotation);
        CHECK_EQ(s.display.width(), w, rotation);
        CHECK_EQ(s.display.height(), h, rotation);
        CHECK_EQ(s.display.rotation(), rotation);
        CHECK_EQ(degrees(rotation), deg);
        CHECK_EQ(rotate_from_degrees(deg), std::optional<Rotate>(rotation));
    }
    CHECK(!rotate_from_degrees(45));
}

/// In every rotation the logical screen covers all of GRAM, each pixel once.
TEST(rotation_fill_covers_whole_panel_once) {
    for (const Rotate rotation : ROTATIONS) {
        Screen s(rotation);
        s.display.fill_screen(Color::WHITE);
        CHECK(std::all_of(s.spi.gram_writes.begin(), s.spi.gram_writes.end(), [](auto n) { return n == 1; }),
              rotation);
    }
}

/// The four rotations are quarter turns of each other, all in the same
/// direction, with no mirror images. (Which way is clockwise depends on how
/// the glass is mounted, so only consistency is checked.)
TEST(rotations_are_consistent_quarter_turns) {
    std::array<Orientation, 4> o{};
    for (std::size_t i = 0; i < 4; ++i) o[i] = orientation(ROTATIONS[i]);
    const int gw = int(GRAM_W) - 1, gh = int(GRAM_H) - 1;
    const std::array<Vec, 4> corners{{{0, 0}, {gw, 0}, {0, gh}, {gw, gh}}};
    // Cross product of the x and y steps: +1 or -1 depending on handedness
    const auto handedness = [](const Orientation& v) {
        return v.x_step.first * v.y_step.second - v.x_step.second * v.y_step.first;
    };

    for (std::size_t i = 0; i < 4; ++i) {
        const Rotate rotation = ROTATIONS[i];
        CHECK(std::find(corners.begin(), corners.end(), o[i].origin) != corners.end(), rotation,
              ": logical (0, 0) not at a corner");
        CHECK_EQ(std::abs(o[i].x_step.first) + std::abs(o[i].x_step.second), 1, rotation);
        CHECK_EQ(std::abs(o[i].y_step.first) + std::abs(o[i].y_step.second), 1, rotation);
        CHECK_EQ(handedness(o[i]), handedness(o[0]), rotation, " is mirrored relative to Degrees0");
    }

    // Each step to the next rotation turns the axes the same way
    const bool clockwise = o[1].x_step == quarter_turn(o[0].x_step);
    const auto turn = [&](Vec v) { return clockwise ? quarter_turn(v) : quarter_turn(quarter_turn(quarter_turn(v))); };
    for (std::size_t k = 0; k < 4; ++k) {
        const Orientation& from = o[k];
        const Orientation& to = o[(k + 1) % 4];
        CHECK_EQ(to.x_step, turn(from.x_step), ROTATIONS[k], " -> ", ROTATIONS[(k + 1) % 4]);
        CHECK_EQ(to.y_step, turn(from.y_step), ROTATIONS[k], " -> ", ROTATIONS[(k + 1) % 4]);
    }

    // The origins visit all four corners, and half turns swap opposite corners
    const auto opposite = [&](Vec v) { return Vec{gw - v.first, gh - v.second}; };
    CHECK_EQ(o[2].origin, opposite(o[0].origin));
    CHECK_EQ(o[3].origin, opposite(o[1].origin));
    std::set<Vec> origins;
    for (const auto& v : o) origins.insert(v.origin);
    CHECK_EQ(origins.size(), 4u, "logical (0, 0) should be a different corner in each rotation");
}

/// Something drawn in Degrees180 is the Degrees0 drawing turned upside down
/// (and the same for Degrees270 vs Degrees90).
TEST(half_turn_rotations_draw_upside_down) {
    const auto draw = [](Rotate rotation) {
        Screen s(rotation);
        s.display.draw_text(4, 6, "Fg", Color::WHITE, Color::BLUE, 3);
        s.display.fill_round_rectangle(30, 70, 50, 25, 8, Color::RED);
        s.display.draw_line(0, 0, 100, 40, Color::GREEN);
        return s.spi.gram;
    };
    for (const auto& [a, b] : {std::pair{Rotate::Degrees0, Rotate::Degrees180},
                               std::pair{Rotate::Degrees90, Rotate::Degrees270}}) {
        const auto gram_a = draw(a);
        const auto gram_b = draw(b);
        CHECK(std::any_of(gram_a.begin(), gram_a.end(), [](const Rgb666& px) { return px != BLACK; }));
        const std::vector<Rgb666> upside_down(gram_a.rbegin(), gram_a.rend());
        CHECK(upside_down == gram_b, b, " is not ", a, " turned upside down");
    }
}

TEST(rotation_address_bounds) {
    for (const Rotate rotation : ROTATIONS) {
        Screen s(rotation);
        const std::uint16_t w = s.display.width(), h = s.display.height();
        // Opposite corners are drawable
        s.display.set_addr_window(0, 0, w - 1, h - 1);
        // One past the edge is rejected before anything is sent
        const std::size_t sent_before = s.spi.commands.size();
        CHECK_THROWS(SizeError, s.display.set_addr_window(0, 0, w, h - 1));
        CHECK_THROWS(SizeError, s.display.set_addr_window(0, 0, w - 1, h));
        CHECK_EQ(s.spi.commands.size(), sent_before, rotation);
    }
}

// ---- shapes through the display ----

namespace {

/// Draws a shape on a landscape display and checks the panel shows exactly
/// the on-screen part of what `shapes` produces for it.
template <class Draw, class Raster>
void check_clipped_shape(const std::string& name, Draw&& draw, Raster&& raster) {
    std::set<std::pair<std::size_t, std::size_t>> expected;
    raster([&](const shapes::Rect& r) {
        for (std::int64_t px = std::max<std::int64_t>(r.x, 0); px < std::min<std::int64_t>(r.x + r.w, 480); ++px) {
            for (std::int64_t py = std::max<std::int64_t>(r.y, 0); py < std::min<std::int64_t>(r.y + r.h, 320);
                 ++py) {
                expected.insert({std::size_t(px), std::size_t(py)});
            }
        }
    });

    Landscape s;
    draw(s.display, Color::CYAN);
    const Rgb666 cyan = Color::CYAN.to_rgb666_bytes();
    check_frame(s.spi, [&](auto x, auto y) { return expected.count({x, y}) ? cyan : BLACK; });
    CHECK(!expected.empty(), name, ": test shape should be partly visible");
}

}  // namespace

TEST(lines_clipped_at_edges) {
    const std::vector<std::array<int, 4>> cases{
        {-100, -40, 600, 400},  // through the screen, both ends off it
        {240, 160, 900, 170},   // off the right
        {240, 160, 250, -900},  // off the top, steep
        {-5, 319, 484, 319},    // along the bottom row
        {479, -10, 479, 330},   // along the right column
        {-30, 40, 40, -30},     // clipping the top-left corner
    };
    for (const auto& [x0, y0, x1, y1] : cases) {
        check_clipped_shape(
            "line " + std::to_string(x0) + "," + std::to_string(y0),
            [&](TftDisplay& d, Color c) { d.draw_line(x0, y0, x1, y1, c); },
            [&](auto emit) { shapes::line(x0, y0, x1, y1, emit); });
    }
}

TEST(circles_clipped_at_edges) {
    const std::vector<std::array<int, 3>> cases{{0, 0, 50}, {479, 319, 30}, {240, -20, 40}, {-10, 160, 60},
                                                {240, 160, 280}};
    for (const auto& [cx, cy, r_int] : cases) {
        const auto r = std::uint16_t(r_int);
        const std::string name = std::to_string(cx) + "," + std::to_string(cy) + " r" + std::to_string(r);
        check_clipped_shape(
            "circle " + name, [&](TftDisplay& d, Color c) { d.draw_circle(cx, cy, r, c); },
            [&](auto emit) { shapes::circle(cx, cy, r, emit); });
        check_clipped_shape(
            "filled circle " + name, [&](TftDisplay& d, Color c) { d.fill_circle(cx, cy, r, c); },
            [&](auto emit) { shapes::filled_circle(cx, cy, r, emit); });
    }
}

TEST(rectangles_clipped_at_edges) {
    const std::vector<std::array<int, 5>> cases{
        {-20, -20, 100, 60, 10}, {440, 290, 100, 100, 20}, {-10, 100, 500, 50, 25}, {5, 5, 470, 310, 0}};
    for (const auto& [x, y, w_int, h_int, r_int] : cases) {
        const auto w = std::uint16_t(w_int), h = std::uint16_t(h_int), r = std::uint16_t(r_int);
        const std::string name = std::to_string(x) + "," + std::to_string(y);
        check_clipped_shape(
            "rectangle " + name, [&](TftDisplay& d, Color c) { d.draw_rectangle(x, y, w, h, c); },
            [&](auto emit) { shapes::rectangle(x, y, w, h, emit); });
        check_clipped_shape(
            "round rectangle " + name, [&](TftDisplay& d, Color c) { d.fill_round_rectangle(x, y, w, h, r, c); },
            [&](auto emit) { shapes::filled_round_rectangle(x, y, w, h, r, emit); });
    }
}

TEST(draw_pixel_on_and_off_screen) {
    Landscape s;
    for (const auto& [x, y] :
         {std::pair{0, 0}, {479, 319}, {100, 200}, {-1, 0}, {0, -1}, {480, 0}, {0, 320}}) {
        s.display.draw_pixel(x, y, Color::WHITE);
    }
    const Rgb666 white = Color::WHITE.to_rgb666_bytes();
    const std::set<std::pair<std::size_t, std::size_t>> on{{0, 0}, {479, 319}, {100, 200}};
    const auto sent = check_frame(s.spi, [&](auto x, auto y) { return on.count({x, y}) ? white : BLACK; });
    CHECK_EQ(sent, 3u);
}

/// Shapes far outside the screen, near the int32 limits, draw nothing and
/// don't overflow.
TEST(shapes_at_extreme_coordinates) {
    Landscape s;
    constexpr int max = std::numeric_limits<int>::max(), min = std::numeric_limits<int>::min();
    constexpr std::uint16_t umax = 0xFFFF;
    s.display.draw_pixel(max, min, Color::WHITE);
    s.display.draw_line(max - 10, max - 10, max, max, Color::WHITE);
    s.display.draw_line(min, min, min + 10, min + 3, Color::WHITE);
    s.display.draw_circle(max, max, umax, Color::WHITE);
    s.display.draw_circle(min, min, umax, Color::WHITE);
    s.display.fill_circle(max, min, umax, Color::WHITE);
    s.display.draw_rectangle(max, max, umax, umax, Color::WHITE);
    s.display.fill_round_rectangle(max - 5, max - 5, umax, umax, 100, Color::WHITE);
    const std::vector<std::uint8_t> tiny(12);
    s.display.draw_image(max - 1, max - 1, 2, 2, tiny);
    CHECK(s.spi.pixel_transfers.empty());
}

// ---- initialization ----

/// The init sequence from tft-spi-demo, minus its gamma tables (the panel's
/// defaults looked best on the board). Pinned so changes are deliberate.
TEST(initialize_sends_known_working_sequence) {
    Screen s(320, 480);
    s.display.initialize();

    const std::vector<mock::Command> expected{
        {0x01, {}},                        // soft reset
        {0xB1, {0x10, 0x11}},              // frame rate, normal mode: 30 Hz, 17 clocks/line
        {0xB2, {0x00, 0x11}},              // frame rate, idle mode
        {0xB3, {0x00, 0x11}},              // frame rate, partial mode
        {0xC2, {0x44}},                    // power control 3
        {0xC5, {0x00, 0x00, 0x00, 0x00}},  // VCOM control
        {0x3A, {0x66}},                    // pixel format: 18 bits/pixel
        {0xB0, {0x00}},                    // interface mode control
        {0x11, {}},                        // sleep out
        {0x29, {}},                        // display on
        {0x36, {0x88}},                    // MADCTL for the default rotation (Degrees0)
    };
    CHECK_EQ(s.spi.commands, expected);
    CHECK_EQ(s.spi.resets, (std::vector<std::size_t>{0}), "one hardware reset, before any command");
    CHECK(s.spi.pixel_transfers.empty(), "initialize sends no pixels");
    CHECK_EQ(s.display.width(), 320);
    CHECK_EQ(s.display.height(), 480);
    CHECK_EQ(s.display.rotation(), Rotate::Degrees0);
}

TEST(initialize_gamma_options) {
    const auto gamma_commands = [](Gamma gamma) {
        Screen s(320, 480);
        s.display.set_gamma(gamma);
        s.display.initialize();
        std::vector<std::uint8_t> out;
        for (const auto& [c, _] : s.spi.commands) {
            if (c >= 0xE0 && c <= 0xE2) out.push_back(c);
        }
        return out;
    };
    CHECK_EQ(gamma_commands(Gamma::Full), (std::vector<std::uint8_t>{0xE0, 0xE1, 0xE2}));
    CHECK_EQ(gamma_commands(Gamma::Analog), (std::vector<std::uint8_t>{0xE0, 0xE1}));
    CHECK(gamma_commands(Gamma::PanelDefault).empty());

    CHECK(gamma_from_name("Analog") == Gamma::Analog);
    CHECK(gamma_from_name("none") == Gamma::PanelDefault);
    CHECK(!gamma_from_name("bogus"));
}

/// The 3-byte pixels the driver sends only work in 18-bit mode.
TEST(initialize_selects_18_bit_pixels) {
    Screen s(320, 480);
    s.display.initialize();
    const auto format = std::find_if(s.spi.commands.begin(), s.spi.commands.end(), [](const auto& c) {
        return c.first == code(Command::InterfacePixelFormat);
    });
    CHECK(format != s.spi.commands.end());
    CHECK_EQ(*format, (mock::Command{0x3A, {0x66}}), "0x66 = 18 bits/pixel (RGB666)");
}

/// ILI9486 timing: wait 5 ms after a soft reset and 120 ms after sleep out
/// before sending more commands.
TEST(initialize_waits_after_reset_and_sleep_out) {
    using namespace std::chrono_literals;
    Screen s(320, 480);
    s.display.initialize();
    const auto delay = [&](Command cmd) {
        for (const auto& [c, d] : s.spi.delays) {
            if (c == code(cmd)) return d;
        }
        throw check::Failure("no delay after command " + std::to_string(code(cmd)));
    };
    CHECK(delay(Command::SoftReset) >= 5ms);
    CHECK(delay(Command::SleepOut) >= 120ms);
}

/// initialize keeps a rotation chosen beforehand.
TEST(initialize_applies_current_rotation) {
    Screen s(480, 320);
    s.display.set_rotation(Rotate::Degrees270);
    s.display.initialize();
    CHECK_EQ(s.spi.commands.back(), (mock::Command{MADCTL, {0xE8}}));
    CHECK_EQ(s.display.width(), 480);
    CHECK_EQ(s.display.height(), 320);
}

/// Changing rotation only affects what's drawn afterwards.
TEST(rotation_change_keeps_existing_pixels) {
    Screen s(480, 480);  // big enough for both logical sizes
    s.display.set_rotation(Rotate::Degrees0);
    s.display.fill_rectangle(0, 0, 10, 10, Color::RED);
    const auto before = s.spi.gram;

    s.display.set_rotation(Rotate::Degrees90);
    CHECK(s.spi.gram == before, "set_rotation changed GRAM");
    s.display.fill_rectangle(0, 0, 10, 10, Color::BLUE);

    const auto count = [&](Color c) {
        return std::count(s.spi.gram.begin(), s.spi.gram.end(), c.to_rgb666_bytes());
    };
    CHECK_EQ(count(Color::RED), 100, "first square still there");
    CHECK_EQ(count(Color::BLUE), 100, "second square in a different corner");
}
