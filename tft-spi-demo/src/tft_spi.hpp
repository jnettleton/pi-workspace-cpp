#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "tft_display/enums.hpp"

namespace tft {

/// Low-level access to the ILI9486 on the Waveshare 3.5" RPi LCD (A).
///
/// Callers always speak plain 8-bit ILI9486: one byte per command and
/// parameter (DC low for commands, high for data) and 18-bit pixels, 3 bytes
/// each (see `Color::to_rgb666_bytes`). `rpi::TftSpiImpl` (Raspberry Pi, Linux
/// only) translates that for the board's 16-bit interface (see `bus16`);
/// tests use a mock panel that decodes the 8-bit form.
///
/// Errors are thrown (`SpiError`).
class TftSpi {
public:
    virtual ~TftSpi() = default;

    virtual void reset_pin() = 0;
    /// Sends a command followed by its parameter bytes.
    virtual std::size_t write_reg(Command cmd, std::span<const std::uint8_t> params) = 0;
    virtual std::size_t write_command(Command cmd) = 0;
    virtual std::size_t write_command_delay(Command cmd, std::chrono::milliseconds delay) = 0;
    /// Sends raw bytes with DC high (parameters or pixel data).
    virtual std::size_t write_data(std::span<const std::uint8_t> data) = 0;
};

/// How bytes reach the panel.
enum class BusWidth {
    /// One byte per command and parameter, 18-bit pixels (as tft-spi-demo does).
    Eight,
    /// 16-bit words, as Waveshare's own driver does (see `bus16`).
    Sixteen,
};

/// `TFT_BUS=8` or `TFT_BUS=16` (the default).
BusWidth bus_width_from_env();

constexpr const char* to_string(BusWidth bus) { return bus == BusWidth::Eight ? "Eight" : "Sixteen"; }

/// The Waveshare 3.5" RPi LCD (A) doesn't take SPI directly: shift registers
/// turn the SPI stream into 16-bit words for the ILI9486's parallel bus,
/// latching one word every 16 clocks while CS is low. So every transfer must
/// be whole 16-bit words:
///
/// - a command byte `c` is sent as `[0x00, c]` (DC low)
/// - each parameter byte `p` as `[0x00, p]` (DC high)
/// - pixels as RGB565, 2 bytes each, big-endian (pixel format `0x55`)
///
/// Single bytes, as tft-spi-demo sends them, get paired with their
/// neighbors, so the panel only sees the right words when the number of
/// bytes before them happens to line up. On the board that showed as bars
/// when transfer sizes changed, landscape fills covering a third of the
/// screen, and unreadable text. With 16-bit words all four rotations fill
/// and text is readable.
namespace bus16 {

/// Pixel format parameter for 16 bits per pixel (DPI and DBI)
inline constexpr std::uint8_t PIXEL_FORMAT_16 = 0x55;

constexpr std::array<std::uint8_t, 2> command(std::uint8_t cmd) { return {0x00, cmd}; }

/// Data bytes following `last_command`, as the board needs them.
std::vector<std::uint8_t> data(std::uint8_t last_command, std::span<const std::uint8_t> data);

/// 18-bit pixels (3 bytes each, the top 6 bits used) to RGB565.
std::vector<std::uint8_t> pixels(std::span<const std::uint8_t> rgb666);

}  // namespace bus16

}  // namespace tft
