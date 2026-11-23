#pragma once

//! Raspberry Pi GPIO through /dev/gpiomem: the GPIO registers mapped into
//! this process, as the Rust `rppal` crate does it. Linux only, Pi 4 (BCM2711)
//! and earlier; the Pi 5's RP1 isn't supported.
//!
//! Register access rather than libgpiod, because the display needs GPIO 8:
//! with SPI enabled the kernel's SPI driver owns that line (CE0), so libgpiod
//! can't request it, but switching the pin's function to a plain output in
//! the registers takes it over, which is what the Rust driver relies on.

#include <cstdint>

namespace rpi {

enum class PinMode : std::uint32_t {
    Input = 0b000,
    Output = 0b001,
    Alt0 = 0b100,
    Alt1 = 0b101,
    Alt2 = 0b110,
    Alt3 = 0b111,
    Alt4 = 0b011,
    Alt5 = 0b010,
};

enum class Pull { Off, Up, Down };

/// The mapped GPIO registers, shared by all pins. Throws `tft::SpiError` if
/// /dev/gpiomem can't be opened (user not in the `gpio` group?).
class Gpio {
public:
    static Gpio& instance();

    PinMode mode(unsigned pin) const;
    void set_mode(unsigned pin, PinMode mode);
    void write(unsigned pin, bool high);
    bool read(unsigned pin) const;
    /// The pull resistor setting (BCM2711 only; older chips can't read it back
    /// and report Off).
    Pull pull(unsigned pin) const;
    void set_pull(unsigned pin, Pull pull);

    Gpio(const Gpio&) = delete;
    Gpio& operator=(const Gpio&) = delete;

private:
    Gpio();
    ~Gpio();

    volatile std::uint32_t* regs_ = nullptr;
    bool bcm2711_ = false;
};

/// A pin driven as an output. With `reset_on_drop`, the pin's previous mode
/// is put back when this is destroyed; otherwise it stays an output at its
/// last level.
class OutputPin {
public:
    OutputPin(unsigned pin, bool initial_high, bool reset_on_drop);
    ~OutputPin();

    void set_high() { gpio_.write(pin_, true); }
    void set_low() { gpio_.write(pin_, false); }

    OutputPin(const OutputPin&) = delete;
    OutputPin& operator=(const OutputPin&) = delete;

private:
    Gpio& gpio_;
    unsigned pin_;
    PinMode previous_mode_;
    bool reset_on_drop_;
};

/// A pin read as an input. Its previous mode and pull are restored when this
/// is destroyed.
class InputPin {
public:
    InputPin(unsigned pin, Pull pull);
    ~InputPin();

    bool is_high() const { return gpio_.read(pin_); }
    bool is_low() const { return !is_high(); }

    InputPin(const InputPin&) = delete;
    InputPin& operator=(const InputPin&) = delete;

private:
    Gpio& gpio_;
    unsigned pin_;
    PinMode previous_mode_;
    Pull previous_pull_;
};

}  // namespace rpi
