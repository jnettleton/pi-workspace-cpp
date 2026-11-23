#include "rpi/spidev.hpp"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "tft_display/error.hpp"

namespace rpi {

namespace {
std::string describe(const std::string& what, const std::string& path) {
    return what + " " + path + ": " + std::strerror(errno);
}
}  // namespace

SpiDev::SpiDev(const std::string& path, std::uint32_t speed_hz, std::uint8_t mode) : path_(path) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        throw tft::SpiError(describe("can't open", path) + " (is SPI enabled with dtparam=spi=on?)");
    }
    const std::uint8_t bits = 8;
    const std::uint8_t lsb_first = 0;
    if (::ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 || ::ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ::ioctl(fd_, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0 || ::ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0) {
        const std::string message = describe("can't configure", path);
        ::close(fd_);
        throw tft::SpiError(message);
    }
}

SpiDev::~SpiDev() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

std::size_t SpiDev::write(std::span<const std::uint8_t> data) {
    if (data.empty()) {
        return 0;
    }
    const ssize_t n = ::write(fd_, data.data(), data.size());
    if (n < 0) {
        throw tft::SpiError(describe("write to", path_));
    }
    return static_cast<std::size_t>(n);
}

void SpiDev::transfer(std::span<std::uint8_t> rx, std::span<const std::uint8_t> tx) {
    spi_ioc_transfer xfer{};
    xfer.tx_buf = reinterpret_cast<std::uintptr_t>(tx.data());
    xfer.rx_buf = reinterpret_cast<std::uintptr_t>(rx.data());
    xfer.len = static_cast<std::uint32_t>(std::min(rx.size(), tx.size()));
    xfer.bits_per_word = 8;
    if (::ioctl(fd_, SPI_IOC_MESSAGE(1), &xfer) < 0) {
        throw tft::SpiError(describe("transfer on", path_));
    }
}

}  // namespace rpi
