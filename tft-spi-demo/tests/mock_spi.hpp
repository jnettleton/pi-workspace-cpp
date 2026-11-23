#pragma once

//! Mock ILI9486 for tests: decodes the column/row address and memory write
//! commands the display sends and paints the pixels into a framebuffer, so
//! tests can check what would be on screen.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "tft_display/color.hpp"
#include "tft_spi.hpp"

namespace mock {

/// Physical display memory (GRAM) size: the panel's native portrait layout.
inline constexpr std::size_t GRAM_W = 320;
inline constexpr std::size_t GRAM_H = 480;

inline constexpr std::uint8_t CASET = tft::code(tft::Command::ColumnAddressSet);
inline constexpr std::uint8_t RASET = tft::code(tft::Command::RowAddressSet);
inline constexpr std::uint8_t RAMWR = tft::code(tft::Command::MemoryWrite);
inline constexpr std::uint8_t MADCTL = tft::code(tft::Command::MemoryAccessControl);

using Command = std::pair<std::uint8_t, std::vector<std::uint8_t>>;

/// Where logical address (x, y) lands in GRAM for a MADCTL value, as
/// (column, row): MV swaps the column and row addresses, then MX mirrors the
/// column and MY mirrors the row. (For the four MADCTL values the driver uses,
/// the other reading of the datasheet, mirroring before the swap, gives the
/// same result.)
std::pair<std::size_t, std::size_t> gram_position(std::uint8_t madctl, std::size_t x, std::size_t y);

/// What the mock panel has received.
///
/// Pixels are recorded twice:
/// - `pixels`: at the logical (column, row) address the display sent, ignoring
///   rotation. Most tests check this, sized to the display's logical size.
/// - `gram`: in physical display memory, after applying MADCTL (0x36) the way
///   the ILI9486 does (see `gram_position`).
class MockSpi : public tft::TftSpi {
public:
    MockSpi(std::size_t width, std::size_t height);

    tft::Rgb666 pixel(std::size_t x, std::size_t y) const { return pixels[y * width + x]; }

    void reset_pin() override;
    std::size_t write_reg(tft::Command cmd, std::span<const std::uint8_t> params) override;
    std::size_t write_command(tft::Command cmd) override;
    std::size_t write_command_delay(tft::Command cmd, std::chrono::milliseconds delay) override;
    std::size_t write_data(std::span<const std::uint8_t> data) override;

    std::size_t width;
    std::size_t height;
    /// RGB666 bytes per pixel, as sent, at the logical address
    std::vector<tft::Rgb666> pixels;
    std::uint8_t cmd = 0;
    std::vector<std::uint8_t> params;
    /// Address window: x0, x1, y0, y1 (inclusive)
    std::size_t win_x0 = 0, win_x1 = 0, win_y0 = 0, win_y1 = 0;
    std::size_t cursor_x = 0, cursor_y = 0;
    std::size_t pixels_written = 0;
    /// Pixels sent after the window was already full
    std::size_t overflow = 0;
    /// Byte count of each memory write data transfer
    std::vector<std::size_t> pixel_transfers;
    /// Last Memory Access Control (MADCTL) value
    std::uint8_t madctl = 0;
    /// Physical display memory, GRAM_W x GRAM_H, row by row
    std::vector<tft::Rgb666> gram;
    /// How many times each GRAM pixel was written
    std::vector<std::uint32_t> gram_writes;
    /// Every command with its parameters, in order (pixel data not included)
    std::vector<Command> commands;
    /// Delays requested after commands: (command, delay)
    std::vector<std::pair<std::uint8_t, std::chrono::milliseconds>> delays;
    /// Hardware resets, as the number of commands sent before each one
    std::vector<std::size_t> resets;
};

}  // namespace mock
