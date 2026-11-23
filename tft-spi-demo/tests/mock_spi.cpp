#include "mock_spi.hpp"

#include <stdexcept>
#include <string>

#include "tft_display/enums.hpp"

namespace mock {

std::pair<std::size_t, std::size_t> gram_position(std::uint8_t madctl, std::size_t x, std::size_t y) {
    using namespace tft::madctl;
    std::size_t col = (madctl & MV) ? y : x;
    std::size_t row = (madctl & MV) ? x : y;
    if (col >= GRAM_W || row >= GRAM_H) {
        throw std::logic_error("address (" + std::to_string(x) + ", " + std::to_string(y) +
                               ") is outside GRAM with MADCTL " + std::to_string(madctl));
    }
    if (madctl & MX) col = GRAM_W - 1 - col;
    if (madctl & MY) row = GRAM_H - 1 - row;
    return {col, row};
}

MockSpi::MockSpi(std::size_t width, std::size_t height)
    : width(width),
      height(height),
      pixels(width * height, tft::Rgb666{0, 0, 0}),
      gram(GRAM_W * GRAM_H, tft::Rgb666{0, 0, 0}),
      gram_writes(GRAM_W * GRAM_H, 0) {}

void MockSpi::reset_pin() { resets.push_back(commands.size()); }

std::size_t MockSpi::write_reg(tft::Command c, std::span<const std::uint8_t> p) {
    const std::size_t n = write_command(c);
    return n + write_data(p);
}

std::size_t MockSpi::write_command(tft::Command c) {
    cmd = tft::code(c);
    params.clear();
    commands.emplace_back(cmd, std::vector<std::uint8_t>{});
    if (cmd == RAMWR) {
        cursor_x = win_x0;
        cursor_y = win_y0;
    }
    return 1;
}

std::size_t MockSpi::write_command_delay(tft::Command c, std::chrono::milliseconds delay) {
    const std::size_t n = write_command(c);
    delays.emplace_back(tft::code(c), delay);
    return n;
}

std::size_t MockSpi::write_data(std::span<const std::uint8_t> data) {
    const auto be16 = [](const std::uint8_t* b) { return (std::size_t{b[0]} << 8) | b[1]; };
    if (cmd != RAMWR && !commands.empty()) {
        auto& recorded = commands.back().second;
        recorded.insert(recorded.end(), data.begin(), data.end());
    }
    switch (cmd) {
        case MADCTL:
            if (!data.empty()) madctl = data[0];
            break;
        case CASET:
        case RASET:
            params.insert(params.end(), data.begin(), data.end());
            if (params.size() == 4) {
                const std::size_t start = be16(&params[0]), end = be16(&params[2]);
                if (cmd == CASET) {
                    win_x0 = start, win_x1 = end;
                } else {
                    win_y0 = start, win_y1 = end;
                }
            }
            break;
        case RAMWR:
            if (data.size() % 3 != 0) {
                throw std::logic_error("transfer of " + std::to_string(data.size()) + " bytes splits a pixel");
            }
            if (data.size() > 4096) {
                throw std::logic_error("transfer of " + std::to_string(data.size()) + " bytes exceeds spidev bufsiz");
            }
            pixel_transfers.push_back(data.size());
            for (std::size_t i = 0; i < data.size(); i += 3) {
                const tft::Rgb666 px{data[i], data[i + 1], data[i + 2]};
                if ((px[0] | px[1] | px[2]) & 0x03) {
                    throw std::logic_error("low 2 bits set in a pixel");
                }
                if (cursor_y > win_y1) {
                    ++overflow;
                    continue;
                }
                pixels[cursor_y * width + cursor_x] = px;
                ++pixels_written;
                const auto [col, row] = gram_position(madctl, cursor_x, cursor_y);
                gram[row * GRAM_W + col] = px;
                ++gram_writes[row * GRAM_W + col];
                if (cursor_x == win_x1) {
                    cursor_x = win_x0;
                    ++cursor_y;
                } else {
                    ++cursor_x;
                }
            }
            break;
        default:
            break;
    }
    return data.size();
}

}  // namespace mock
