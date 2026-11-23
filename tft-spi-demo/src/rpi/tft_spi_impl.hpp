#pragma once

#include <cstdint>
#include <utility>

#include "rpi/gpio.hpp"
#include "rpi/spidev.hpp"
#include "tft_spi.hpp"

namespace rpi {

/// `TftSpi` for the Waveshare 3.5" RPi LCD (A) on a Raspberry Pi. Linux only.
class TftSpiImpl : public tft::TftSpi {
public:
    /// Default SPI clock. The Pi rounds down to its core clock divided by
    /// an even number, so on a Pi 4 this runs at about 15.6 MHz.
    /// tft-spi-demo's 31.2 MHz (about 27.8 MHz) fills solid colors and
    /// draws text fine, but the board's shift registers then latch some
    /// pixel bits a position off: green and blue values roughly double
    /// and wrap, so photos show grey as cyan and orange as yellow-green.
    /// Override with the `TFT_SPI_HZ` environment variable.
    static constexpr std::uint32_t SPI_CLOCK_SPEED = 16'000'000;

    /// Reset. GPIO 25, physical pin 22
    static constexpr unsigned TFT_RST = 25;
    /// Command/Data Register Select (LCD_RS). GPIO 24, physical pin 18
    static constexpr unsigned TFT_DC = 24;
    /// LCD chip select (CE0), driven as a plain GPIO. GPIO 8, physical pin 24
    static constexpr unsigned TFT_CS = 8;

    TftSpiImpl();

    /// Runs `f` with the display's chip select released (high), so another
    /// device on SPI0 (the touch controller on CE1) can be used without the
    /// LCD seeing that traffic.
    template <class F>
    decltype(auto) with_display_deselected(F&& f) {
        struct Reselect {
            OutputPin& cs;
            ~Reselect() { cs.set_low(); }
        } reselect{tft_cs_};
        tft_cs_.set_high();
        return std::forward<F>(f)();
    }

    void reset_pin() override;
    std::size_t write_reg(tft::Command cmd, std::span<const std::uint8_t> params) override;
    std::size_t write_command(tft::Command cmd) override;
    std::size_t write_command_delay(tft::Command cmd, std::chrono::milliseconds delay) override;
    std::size_t write_data(std::span<const std::uint8_t> data) override;

private:
    void dc_set_low();
    void dc_set_high();

    /// SPI0 on CE0 (/dev/spidev0.0), mode 0. MOSI GPIO 10 / pin 19 (LCD and
    /// touch data in), MISO GPIO 9 / pin 21 (touch data out), SCLK GPIO 11 /
    /// pin 23. GPIO 8 (CE0) is then taken over as a plain output (`tft_cs_`),
    /// so the SPI block's own chip select isn't used.
    SpiDev spi_device_;
    // Start DC low so it matches `command_ = true`. DC and RST stay driven
    // after the program exits: GPIO 25 has a pull-down by default, so if it
    // went back to an input RST would go low and hold the LCD in reset,
    // blanking whatever was drawn (e.g. after the picture viewer).
    OutputPin tft_dc_{TFT_DC, false, false};
    OutputPin tft_rst_{TFT_RST, true, false};
    // Hold the LCD's chip select low the whole time, as tft-spi-demo does,
    // instead of letting the SPI hardware raise CE0 after every transfer (CS
    // going high resets the board's 16-bit word counter; with 8-bit transfers
    // that showed bars). Making GPIO 8 an output disconnects it from the SPI
    // block; on exit it goes back to CE0.
    OutputPin tft_cs_{TFT_CS, false, true};
    tft::BusWidth bus_;
    /// Last command sent, so `write_data` knows whether it's pixels
    std::uint8_t last_command_ = 0;
    bool command_ = true;
};

}  // namespace rpi
