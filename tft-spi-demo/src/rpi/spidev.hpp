#pragma once

//! A Linux spidev device (/dev/spidevB.S). Linux only.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace rpi {

class SpiDev {
public:
    /// Opens `path` in SPI mode `mode` with 8-bit words and `speed_hz` as the
    /// maximum clock. Throws `tft::SpiError`.
    SpiDev(const std::string& path, std::uint32_t speed_hz, std::uint8_t mode = 0);
    ~SpiDev();

    /// Sends `data` as one transfer.
    std::size_t write(std::span<const std::uint8_t> data);
    /// Full duplex: sends `tx` while reading the same number of bytes into `rx`.
    void transfer(std::span<std::uint8_t> rx, std::span<const std::uint8_t> tx);

    SpiDev(const SpiDev&) = delete;
    SpiDev& operator=(const SpiDev&) = delete;

private:
    int fd_ = -1;
    std::string path_;
};

}  // namespace rpi
