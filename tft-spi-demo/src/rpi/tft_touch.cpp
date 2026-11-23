#include "rpi/tft_touch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <vector>

#include "tft_touch/filter.hpp"

namespace rpi {

using namespace std::chrono_literals;

TftTouch::TftTouch(TftSpiImpl& display)
    : spi_("/dev/spidev0.1", SPI_CLOCK_SPEED, 0), display_(display), irq_(TP_IRQ, Pull::Up) {}

std::optional<tft::Point> TftTouch::read_raw() {
    if (!is_touched()) {
        return std::nullopt;
    }
    const std::uint16_t x = read_channel(READ_X);
    const std::uint16_t y = read_channel(READ_Y);
    return tft::Point{x, y};
}

std::optional<tft::Point> TftTouch::read_filtered(std::size_t samples) {
    if (!read_raw()) {
        return std::nullopt;
    }

    std::vector<tft::Point> readings;
    readings.reserve(samples);
    for (std::size_t i = 0; i < std::max<std::size_t>(samples, 1); ++i) {
        const auto reading = read_raw();
        if (!reading) {
            return std::nullopt;
        }
        readings.push_back(*reading);
    }
    return tft::median_reading(readings);
}

tft::Point TftTouch::wait_for_touch() {
    for (;;) {
        if (const auto raw = read_filtered(FILTER_SAMPLES)) {
            return *raw;
        }
        std::this_thread::sleep_for(10ms);
    }
}

void TftTouch::wait_for_release() {
    using Clock = std::chrono::steady_clock;
    std::optional<Clock::time_point> released_since;
    for (;;) {
        if (is_touched()) {
            released_since.reset();
        } else {
            if (!released_since) {
                released_since = Clock::now();
            }
            if (Clock::now() - *released_since >= 100ms) {
                return;
            }
        }
        std::this_thread::sleep_for(10ms);
    }
}

std::uint16_t TftTouch::read_channel(std::uint8_t cmd) {
    const std::array<std::uint8_t, 3> tx{cmd, 0x00, 0x00};
    std::array<std::uint8_t, 3> rx{};
    display_.with_display_deselected([&] { spi_.transfer(rx, tx); });
    return static_cast<std::uint16_t>(((rx[1] << 8) | rx[2]) >> 3);
}

}  // namespace rpi
