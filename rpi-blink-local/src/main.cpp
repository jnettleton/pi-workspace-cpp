// main.cpp - Blink an LED on Raspberry Pi 4 using libgpiod (C++ bindings)
//
// Wiring:
//   BCM GPIO17 (physical pin 11) --[330 ohm resistor]-- LED anode (+)
//   LED cathode (-) -- GND (physical pin 9 or 14)
//
// Run with sudo unless your user is in the 'gpio' group:
//   sudo usermod -aG gpio $USER   (then log out/in)

#include <gpiod.hpp>

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
// Pi 4 uses "gpiochip0" (label: pinctrl-bcm2711).
// Pi 5 uses "gpiochip4" instead -- change here if you ever move to a Pi 5.
constexpr const char* kChipName    = "gpiochip0";
constexpr unsigned    kLedLine     = 17;   // BCM GPIO17 == physical pin 11
constexpr auto        kBlinkPeriod = std::chrono::milliseconds(500);

volatile std::sig_atomic_t g_stop = 0;
void handle_signal(int) { g_stop = 1; }
}  // namespace

int main() {
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        gpiod::chip chip(kChipName);
        auto line = chip.get_line(kLedLine);

        line.request({
            "rpi-blink",
            gpiod::line_request::DIRECTION_OUTPUT,
            0  // default flags
        }, 0);  // initial value = LOW

        std::cout << "Blinking GPIO" << kLedLine
                  << " on " << kChipName
                  << " -- Ctrl+C to stop.\n";

        bool on = false;
        while (!g_stop) {
            on = !on;
            line.set_value(on ? 1 : 0);
            std::this_thread::sleep_for(kBlinkPeriod);
        }

        line.set_value(0);
        line.release();
        std::cout << "\nStopped cleanly.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
