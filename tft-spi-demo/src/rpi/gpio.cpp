#include "rpi/gpio.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include "tft_display/error.hpp"

namespace rpi {

namespace {

// Register offsets in 32-bit words from the start of the GPIO block
constexpr unsigned GPFSEL0 = 0x00 / 4;
constexpr unsigned GPSET0 = 0x1C / 4;
constexpr unsigned GPCLR0 = 0x28 / 4;
constexpr unsigned GPLEV0 = 0x34 / 4;
constexpr unsigned GPPUD = 0x94 / 4;                    // BCM2835..2837
constexpr unsigned GPPUDCLK0 = 0x98 / 4;                // BCM2835..2837
constexpr unsigned GPIO_PUP_PDN_CNTRL_REG0 = 0xE4 / 4;  // BCM2711

constexpr unsigned PIN_COUNT = 54;
constexpr std::size_t BLOCK_SIZE = 4096;

std::string device_tree_compatible() {
    std::ifstream file("/proc/device-tree/compatible", std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void check_pin(unsigned pin) {
    if (pin >= PIN_COUNT) {
        throw tft::SpiError("GPIO " + std::to_string(pin) + " doesn't exist");
    }
}

}  // namespace

Gpio& Gpio::instance() {
    static Gpio gpio;
    return gpio;
}

Gpio::Gpio() {
    const std::string compatible = device_tree_compatible();
    if (compatible.find("bcm2712") != std::string::npos) {
        throw tft::SpiError("Raspberry Pi 5 GPIO (RP1) isn't supported; this driver is for a Pi 4");
    }
    bcm2711_ = compatible.find("bcm2711") != std::string::npos;

    const int fd = ::open("/dev/gpiomem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) {
        throw tft::SpiError(std::string("can't open /dev/gpiomem: ") + std::strerror(errno) +
                            " (run as a user in the gpio group, or with sudo)");
    }
    void* map = ::mmap(nullptr, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    const int map_errno = errno;
    ::close(fd);
    if (map == MAP_FAILED) {
        throw tft::SpiError(std::string("can't map /dev/gpiomem: ") + std::strerror(map_errno));
    }
    regs_ = static_cast<volatile std::uint32_t*>(map);
}

Gpio::~Gpio() {
    if (regs_ != nullptr) {
        ::munmap(const_cast<std::uint32_t*>(regs_), BLOCK_SIZE);
    }
}

PinMode Gpio::mode(unsigned pin) const {
    check_pin(pin);
    const std::uint32_t reg = regs_[GPFSEL0 + pin / 10];
    return static_cast<PinMode>((reg >> ((pin % 10) * 3)) & 0b111);
}

void Gpio::set_mode(unsigned pin, PinMode mode) {
    check_pin(pin);
    const unsigned shift = (pin % 10) * 3;
    const std::uint32_t reg = regs_[GPFSEL0 + pin / 10];
    regs_[GPFSEL0 + pin / 10] = (reg & ~(0b111u << shift)) | (static_cast<std::uint32_t>(mode) << shift);
}

void Gpio::write(unsigned pin, bool high) {
    check_pin(pin);
    regs_[(high ? GPSET0 : GPCLR0) + pin / 32] = 1u << (pin % 32);
}

bool Gpio::read(unsigned pin) const {
    check_pin(pin);
    return (regs_[GPLEV0 + pin / 32] >> (pin % 32)) & 1;
}

Pull Gpio::pull(unsigned pin) const {
    check_pin(pin);
    if (!bcm2711_) {
        return Pull::Off;
    }
    const std::uint32_t reg = regs_[GPIO_PUP_PDN_CNTRL_REG0 + pin / 16];
    switch ((reg >> ((pin % 16) * 2)) & 0b11) {
        case 0b01: return Pull::Up;
        case 0b10: return Pull::Down;
        default: return Pull::Off;
    }
}

void Gpio::set_pull(unsigned pin, Pull pull) {
    check_pin(pin);
    if (bcm2711_) {
        const std::uint32_t bits = pull == Pull::Up ? 0b01 : pull == Pull::Down ? 0b10 : 0b00;
        const unsigned shift = (pin % 16) * 2;
        const std::uint32_t reg = regs_[GPIO_PUP_PDN_CNTRL_REG0 + pin / 16];
        regs_[GPIO_PUP_PDN_CNTRL_REG0 + pin / 16] = (reg & ~(0b11u << shift)) | (bits << shift);
        return;
    }
    // BCM2835..2837: set the control signal, clock it into the pin, then clear both
    regs_[GPPUD] = pull == Pull::Up ? 0b10 : pull == Pull::Down ? 0b01 : 0b00;
    std::this_thread::sleep_for(std::chrono::microseconds(20));
    regs_[GPPUDCLK0 + pin / 32] = 1u << (pin % 32);
    std::this_thread::sleep_for(std::chrono::microseconds(20));
    regs_[GPPUD] = 0;
    regs_[GPPUDCLK0 + pin / 32] = 0;
}

OutputPin::OutputPin(unsigned pin, bool initial_high, bool reset_on_drop)
    : gpio_(Gpio::instance()), pin_(pin), previous_mode_(gpio_.mode(pin)), reset_on_drop_(reset_on_drop) {
    // Level first, so the pin doesn't glitch to the wrong level when it becomes an output
    gpio_.write(pin_, initial_high);
    gpio_.set_mode(pin_, PinMode::Output);
}

OutputPin::~OutputPin() {
    if (reset_on_drop_) {
        gpio_.set_mode(pin_, previous_mode_);
    }
}

InputPin::InputPin(unsigned pin, Pull pull)
    : gpio_(Gpio::instance()), pin_(pin), previous_mode_(gpio_.mode(pin)), previous_pull_(gpio_.pull(pin)) {
    gpio_.set_mode(pin_, PinMode::Input);
    gpio_.set_pull(pin_, pull);
}

InputPin::~InputPin() {
    gpio_.set_pull(pin_, previous_pull_);
    gpio_.set_mode(pin_, previous_mode_);
}

}  // namespace rpi
