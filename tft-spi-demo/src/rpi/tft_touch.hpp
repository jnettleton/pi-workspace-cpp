#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "rpi/gpio.hpp"
#include "rpi/spidev.hpp"
#include "rpi/tft_spi_impl.hpp"
#include "tft_touch/touch_input.hpp"

namespace rpi {

/// XPT2046 touch controller on the Waveshare 3.5" RPi LCD (A). Linux only.
///
/// Uses its own SPI device on CE1 (`/dev/spidev0.1`, GPIO 7 / physical pin 26)
/// with its own clock speed. The display holds its chip select (GPIO 8) low,
/// so every touch read releases it first via `with_display_deselected`.
class TftTouch : public tft::TouchInput {
public:
    /// XPT2046 max clock is ~2.5 MHz.
    static constexpr std::uint32_t SPI_CLOCK_SPEED = 2'000'000;
    /// TP_IRQ: low while the panel is touched. GPIO 17 / physical pin 11.
    static constexpr unsigned TP_IRQ = 17;

    // Control bytes: start bit, channel, 12-bit, differential, PD = 00 (IRQ enabled)
    static constexpr std::uint8_t READ_X = 0xD0;
    static constexpr std::uint8_t READ_Y = 0x90;

    /// Readings per filtered sample (median)
    static constexpr std::size_t FILTER_SAMPLES = 15;

    /// `display` is the display's SPI (shares SPI0 and its chip select); it
    /// must outlive this.
    explicit TftTouch(TftSpiImpl& display);

    bool is_touched() const { return irq_.is_low(); }

    /// Raw 12-bit (0..=4095) X/Y readings, or nothing if not touched.
    /// These still need calibrating to screen coordinates.
    std::optional<tft::Point> read_raw();

    /// Median of `samples` raw readings, or nothing if the panel isn't touched
    /// for the whole time. The first reading is thrown away (it's often off
    /// while the finger is still settling), as are readings stuck at 0 or 4095.
    std::optional<tft::Point> read_filtered(std::size_t samples);

    /// Blocks until the panel is touched and returns a filtered raw reading.
    tft::Point wait_for_touch() override;

    /// Blocks until the panel has been released for at least 100 ms.
    void wait_for_release() override;

private:
    std::uint16_t read_channel(std::uint8_t cmd);

    SpiDev spi_;
    TftSpiImpl& display_;
    InputPin irq_;
};

}  // namespace rpi
