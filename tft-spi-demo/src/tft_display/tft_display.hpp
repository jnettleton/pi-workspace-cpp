#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>

#include "tft_display/color.hpp"
#include "tft_display/enums.hpp"
#include "tft_display/error.hpp"
#include "tft_display/shapes.hpp"
#include "tft_spi.hpp"

namespace tft {

/// Which gamma tables `initialize` sends. With the 8-bit bus most of these
/// commands never reached the panel intact; with the 16-bit bus they do, and
/// photos came out with strong, banded color shifts (grey as cyan, orange as
/// yellow-green). The panel's own defaults looked best on the board.
enum class Gamma {
    /// Positive, negative and digital gamma (tft-spi-demo's sequence)
    Full,
    /// Positive and negative gamma only, no digital gamma table
    Analog,
    /// None: the panel's own defaults after reset (the default)
    PanelDefault,
};

/// From a name: `full`, `analog` or `none` (any case).
std::optional<Gamma> gamma_from_name(std::string_view name);
const char* to_string(Gamma gamma);

/// Driver for the ILI9486 on the Waveshare 3.5" RPi LCD (A), 18 bits per pixel.
///
/// Holds a reference to its `TftSpi`, which must outlive it. Errors are thrown
/// (`tft::Error` and subclasses).
class TftDisplay {
public:
    /// Native (portrait) panel size of the ILI9486
    static constexpr std::uint16_t NATIVE_WIDTH = 320;
    static constexpr std::uint16_t NATIVE_HEIGHT = 480;

    explicit TftDisplay(TftSpi& spi);

    /// Sets which gamma tables the next `initialize` sends.
    void set_gamma(Gamma gamma) { gamma_ = gamma; }

    std::uint16_t width() const { return width_; }
    std::uint16_t height() const { return height_; }
    Rotate rotation() const { return rotation_; }

    /// Init sequence from tft-spi-demo (the one that works on the board).
    void initialize();

    /// Sets the orientation (MADCTL values from tft-spi-demo).
    void set_rotation(Rotate rotation);

    void fill_screen(Color color);

    /// Fills a rectangle, clipped to the screen.
    void fill_rectangle(std::uint16_t x, std::uint16_t y, std::uint16_t w, std::uint16_t h, Color color);

    /// Sets one pixel. Off-screen coordinates are ignored.
    void draw_pixel(std::int32_t x, std::int32_t y, Color color);

    /// Draws a 1-pixel line from (x0, y0) to (x1, y1), both ends included.
    /// Coordinates may be off-screen; the line is clipped.
    void draw_line(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Color color);

    /// Draws a 1-pixel rectangle outline with its top-left corner at (x, y).
    /// Clipped to the screen. Use `fill_rectangle` for a solid one.
    void draw_rectangle(std::int32_t x, std::int32_t y, std::uint16_t w, std::uint16_t h, Color color);

    /// Fills a rectangle with rounded corners, top-left corner at (x, y).
    /// `radius` is reduced to fit if it's more than half the shorter side
    /// (so a large radius gives a pill shape). Clipped to the screen.
    void fill_round_rectangle(std::int32_t x, std::int32_t y, std::uint16_t w, std::uint16_t h,
                              std::uint16_t radius, Color color);

    /// Draws a 1-pixel circle outline centered on (cx, cy). Clipped to the screen.
    void draw_circle(std::int32_t cx, std::int32_t cy, std::uint16_t radius, Color color);

    /// Draws a filled circle centered on (cx, cy). Clipped to the screen.
    void fill_circle(std::int32_t cx, std::int32_t cy, std::uint16_t radius, Color color);

    /// Draws an image with its top-left corner at (x, y). `rgb` holds
    /// `width * height` pixels, row by row, 3 bytes each (R, G, B, 8 bits per
    /// channel). Clipped to the screen. Throws `ImageSizeError` if `rgb` is
    /// the wrong length.
    void draw_image(std::int32_t x, std::int32_t y, std::uint16_t width, std::uint16_t height,
                    std::span<const std::uint8_t> rgb);

    /// Draws UTF-8 `text` with its top-left corner at (`x`, `y`), scaled by
    /// `size` (1 = 6x8 pixels per character, 2 = 12x16, ...). `'\n'` starts a
    /// new line at `x`; `'\r'` is ignored, so Windows line endings work. Text
    /// running off the screen is clipped, not wrapped.
    void draw_text(std::uint16_t x, std::uint16_t y, std::string_view text, Color fg, Color bg, std::uint8_t size);

    /// Width in pixels of the longest line of `text` at `size`.
    static std::uint16_t text_width(std::string_view text, std::uint8_t size);

    /// Draws one character cell (glyph plus 1 column of spacing, in `bg`),
    /// clipped to the screen.
    void draw_char(std::uint16_t x, std::uint16_t y, char32_t c, Color fg, Color bg, std::uint8_t size);

    /// Sets the drawing window (inclusive coordinates). Throws `SizeError`
    /// for coordinates outside the screen, before sending anything.
    void set_addr_window(std::uint16_t x0, std::uint16_t y0, std::uint16_t x1, std::uint16_t y1);

private:
    /// The on-screen part of a rectangle, end exclusive.
    struct Clipped {
        std::uint16_t x0, y0, x1, y1;
    };

    /// `fill_rectangle` for signed, possibly off-screen rectangles.
    void fill_rect_clipped(const shapes::Rect& rect, Color color);
    std::optional<Clipped> clip(const shapes::Rect& rect) const;
    void reg(Command cmd, std::initializer_list<std::uint8_t> params);

    TftSpi& spi_;
    std::uint16_t width_ = NATIVE_WIDTH;
    std::uint16_t height_ = NATIVE_HEIGHT;
    Rotate rotation_ = Rotate::Degrees0;
    Gamma gamma_ = Gamma::PanelDefault;
};

}  // namespace tft
