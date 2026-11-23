#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>

namespace tft {

/// The 3 bytes of one pixel as sent in 18-bit mode (RGB666, top 6 bits used).
using Rgb666 = std::array<std::uint8_t, 3>;

/// 24-bit RGB color, sent to the panel as 18-bit RGB666.
class Color {
public:
    static const Color BLACK;
    static const Color WHITE;
    static const Color RED;
    static const Color GREEN;
    static const Color BLUE;
    static const Color CYAN;
    static const Color MAGENTA;
    static const Color YELLOW;
    static const Color TAN;
    static const Color GREY;
    static const Color BROWN;

    constexpr Color(std::uint8_t r, std::uint8_t g, std::uint8_t b) : r_(r), g_(g), b_(b) {}

    static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) { return {r, g, b}; }

    /// Converts 24-bit color (e.g. white 0xFFFFFF) to `Color`, or nothing if
    /// the value is greater than 0xFFFFFF.
    static constexpr std::optional<Color> from_24_bit(std::uint32_t color) {
        if (color > 0xFF'FF'FF) {
            return std::nullopt;
        }
        return Color(static_cast<std::uint8_t>(color >> 16), static_cast<std::uint8_t>(color >> 8),
                     static_cast<std::uint8_t>(color));
    }

    constexpr std::uint8_t red() const { return r_; }
    constexpr std::uint8_t green() const { return g_; }
    constexpr std::uint8_t blue() const { return b_; }

    /// The 3 bytes sent over SPI in 18-bit mode. The ILI9486 reads the top
    /// 6 bits of each byte (D7..D2), so the 8-bit value is used as-is with the
    /// low 2 bits cleared.
    constexpr Rgb666 to_rgb666_bytes() const {
        return {static_cast<std::uint8_t>(r_ & 0xFC), static_cast<std::uint8_t>(g_ & 0xFC),
                static_cast<std::uint8_t>(b_ & 0xFC)};
    }

    friend constexpr bool operator==(const Color&, const Color&) = default;

private:
    std::uint8_t r_;
    std::uint8_t g_;
    std::uint8_t b_;
};

inline constexpr Color Color::BLACK{0, 0, 0};
inline constexpr Color Color::WHITE{255, 255, 255};
inline constexpr Color Color::RED{255, 0, 0};
inline constexpr Color Color::GREEN{0, 255, 0};
inline constexpr Color Color::BLUE{0, 0, 255};
inline constexpr Color Color::CYAN{0, 255, 255};
inline constexpr Color Color::MAGENTA{255, 0, 255};
inline constexpr Color Color::YELLOW{255, 255, 0};
inline constexpr Color Color::TAN{210, 180, 140};
inline constexpr Color Color::GREY{128, 128, 128};
inline constexpr Color Color::BROWN{165, 42, 42};

/// Prints `(r, g, b)`.
inline std::ostream& operator<<(std::ostream& os, const Color& c) {
    return os << '(' << int(c.red()) << ", " << int(c.green()) << ", " << int(c.blue()) << ')';
}

}  // namespace tft
