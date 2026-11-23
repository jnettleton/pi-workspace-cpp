#pragma once

//! 5x8 ASCII font (0x20..=0x7E), 5 column bytes per glyph.
//!
//! Each byte is one column, bit 0 = top row, bit 7 = bottom row (descenders).
//! `FONT` holds ' '..='R', `FONT2` holds 'S'..='~'; use `glyph` to look up.

#include <array>
#include <cstdint>
#include <span>

namespace tft::font {

/// Glyph width in pixels
inline constexpr std::uint16_t GLYPH_WIDTH = 5;
/// Glyph height in pixels
inline constexpr std::uint16_t GLYPH_HEIGHT = 8;
/// Horizontal advance per character (glyph + 1 column spacing)
inline constexpr std::uint16_t CHAR_ADVANCE = GLYPH_WIDTH + 1;

extern const std::array<std::uint8_t, 255> FONT;
extern const std::array<std::uint8_t, 220> FONT2;

/// Column bytes for the code point `c`. Characters outside printable ASCII
/// render as '?'.
std::span<const std::uint8_t, GLYPH_WIDTH> glyph(char32_t c);

}  // namespace tft::font
