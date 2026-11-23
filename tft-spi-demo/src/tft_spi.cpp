#include "tft_spi.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

#include "util/strings.hpp"

namespace tft {

BusWidth bus_width_from_env() {
    const char* value = std::getenv("TFT_BUS");
    if (value == nullptr) {
        return BusWidth::Sixteen;
    }
    const std::string_view trimmed = util::trim(value);
    if (trimmed == "8") {
        return BusWidth::Eight;
    }
    if (trimmed != "16") {
        std::cerr << "ignoring TFT_BUS=" << util::quoted(value) << ": use 8 or 16\n";
    }
    return BusWidth::Sixteen;
}

namespace bus16 {

std::vector<std::uint8_t> data(std::uint8_t last_command, std::span<const std::uint8_t> bytes) {
    if (last_command == code(Command::MemoryWrite)) {
        return pixels(bytes);
    }
    std::vector<std::uint8_t> out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t p : bytes) {
        out.push_back(0x00);
        // The caller asks for 18-bit (0x66); this bus sends 16-bit pixels
        out.push_back(last_command == code(Command::InterfacePixelFormat) ? PIXEL_FORMAT_16 : p);
    }
    return out;
}

std::vector<std::uint8_t> pixels(std::span<const std::uint8_t> rgb666) {
    assert(rgb666.size() % 3 == 0 && "bytes aren't whole pixels");
    std::vector<std::uint8_t> out;
    out.reserve(rgb666.size() / 3 * 2);
    for (std::size_t i = 0; i + 3 <= rgb666.size(); i += 3) {
        const unsigned r = rgb666[i], g = rgb666[i + 1], b = rgb666[i + 2];
        const unsigned rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        out.push_back(static_cast<std::uint8_t>(rgb565 >> 8));
        out.push_back(static_cast<std::uint8_t>(rgb565));
    }
    return out;
}

}  // namespace bus16

}  // namespace tft
