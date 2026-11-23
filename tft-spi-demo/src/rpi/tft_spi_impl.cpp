#include "rpi/tft_spi_impl.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>

#include "util/strings.hpp"

namespace rpi {

using namespace std::chrono_literals;

namespace {

std::uint32_t clock_speed() {
    const char* value = std::getenv("TFT_SPI_HZ");
    if (value == nullptr) {
        return TftSpiImpl::SPI_CLOCK_SPEED;
    }
    if (const auto hz = util::parse<std::uint32_t>(util::trim(value))) {
        return *hz;
    }
    std::cerr << "ignoring TFT_SPI_HZ=" << util::quoted(value) << ": not a number\n";
    return TftSpiImpl::SPI_CLOCK_SPEED;
}

std::uint32_t announce(std::uint32_t speed) {
    std::cout << "display SPI clock: " << speed << " Hz requested\n";
    return speed;
}

}  // namespace

TftSpiImpl::TftSpiImpl()
    : spi_device_("/dev/spidev0.0", announce(clock_speed()), 0), bus_(tft::bus_width_from_env()) {
    std::cout << "display bus: " << tft::to_string(bus_) << " (set TFT_BUS=8 or 16 to change)\n";
}

void TftSpiImpl::reset_pin() {
    tft_rst_.set_high();
    std::this_thread::sleep_for(120ms);
    tft_rst_.set_low();
    std::this_thread::sleep_for(120ms);
    tft_rst_.set_high();
    // ILI9486 needs up to 120 ms after reset before accepting commands
    std::this_thread::sleep_for(120ms);
}

std::size_t TftSpiImpl::write_reg(tft::Command cmd, std::span<const std::uint8_t> params) {
    std::size_t bytes = write_command(cmd);
    if (!params.empty()) {
        bytes += write_data(params);
    }
    return bytes;
}

std::size_t TftSpiImpl::write_command(tft::Command cmd) {
    dc_set_low();
    last_command_ = tft::code(cmd);
    if (bus_ == tft::BusWidth::Eight) {
        const std::uint8_t byte = last_command_;
        return spi_device_.write(std::span<const std::uint8_t>(&byte, 1));
    }
    return spi_device_.write(tft::bus16::command(last_command_));
}

std::size_t TftSpiImpl::write_command_delay(tft::Command cmd, std::chrono::milliseconds delay) {
    const std::size_t bytes = write_command(cmd);
    if (delay.count() > 0) {
        std::this_thread::sleep_for(delay);
    }
    return bytes;
}

std::size_t TftSpiImpl::write_data(std::span<const std::uint8_t> data) {
    dc_set_high();
    if (bus_ == tft::BusWidth::Eight) {
        return spi_device_.write(data);
    }
    return spi_device_.write(tft::bus16::data(last_command_, data));
}

void TftSpiImpl::dc_set_low() {
    if (!command_) {
        tft_dc_.set_low();
        command_ = true;
    }
}

void TftSpiImpl::dc_set_high() {
    if (command_) {
        tft_dc_.set_high();
        command_ = false;
    }
}

}  // namespace rpi
