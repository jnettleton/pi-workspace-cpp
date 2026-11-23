#include "tft_display/tft_display.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "tft_display/font.hpp"
#include "util/strings.hpp"
#include "util/utf8.hpp"

namespace tft {

namespace {

using namespace std::chrono_literals;

/// Bytes per pixel in 18-bit mode
constexpr std::size_t BYTES_PER_PIXEL = 3;
constexpr std::size_t MAX_TRANSFER_BYTES = TftDisplay::MAX_TRANSFER_PIXELS * BYTES_PER_PIXEL;

std::uint16_t saturating_add(std::uint16_t a, std::uint32_t b) {
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(a + b, 0xFFFF));
}

std::array<std::uint8_t, 2> be_bytes(std::uint16_t v) {
    return {static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v)};
}

}  // namespace

std::optional<Gamma> gamma_from_name(std::string_view name) {
    std::string lower(util::trim(name));
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower == "full") return Gamma::Full;
    if (lower == "analog") return Gamma::Analog;
    if (lower == "none" || lower == "default") return Gamma::PanelDefault;
    return std::nullopt;
}

const char* to_string(Gamma gamma) {
    switch (gamma) {
        case Gamma::Full: return "Full";
        case Gamma::Analog: return "Analog";
        case Gamma::PanelDefault: return "PanelDefault";
    }
    return "?";
}

TftDisplay::TftDisplay(TftSpi& spi) : spi_(spi) {}

void TftDisplay::reg(Command cmd, std::initializer_list<std::uint8_t> params) {
    spi_.write_reg(cmd, std::span<const std::uint8_t>(params.begin(), params.size()));
}

void TftDisplay::initialize() {
    spi_.reset_pin();
    spi_.write_command_delay(Command::SoftReset, 150ms);

    // Frame rate: 30 Hz, division ratio fosc, 17 clocks per line
    constexpr std::uint8_t frs = 0b0001;  // 30 Hz
    constexpr std::uint8_t div = 0b0000;  // fosc
    constexpr std::uint8_t rtn = 0x11;    // 17 clocks per line
    reg(Command::FrameRateControlNormal, {(frs << 4) | div, rtn});
    std::this_thread::sleep_for(10ms);
    reg(Command::FrameRateControlIdle, {div, rtn});
    reg(Command::FrameRateControlPartial, {div, rtn});

    // Step-up circuit frequencies in normal mode: 4 H / 4 H
    constexpr std::uint8_t dc0 = 0b0100;
    constexpr std::uint8_t dc1 = 0b0100;
    reg(Command::PowerControl3, {(dc1 << 4) | dc0});

    reg(Command::VcomControl1, {0x00, 0x00, 0x00, 0x00});

    // 18 bits / pixel for DPI and DBI (the only color depth ILI9486 supports over SPI)
    constexpr std::uint8_t dpi = 0b0110;
    constexpr std::uint8_t dbi = 0b0110;
    reg(Command::InterfacePixelFormat, {(dpi << 4) | dbi});
    reg(Command::InterfaceModeControl, {0x00});

    if (gamma_ != Gamma::PanelDefault) {
        reg(Command::PositiveGammaControl,
            {0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98, 0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00});
        reg(Command::NegativeGammaControl,
            {0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75, 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00});
    }
    if (gamma_ == Gamma::Full) {
        reg(Command::DigitalGammaControl1,
            {0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75, 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00});
    }

    spi_.write_command_delay(Command::SleepOut, 120ms);
    spi_.write_command_delay(Command::DisplayOn, 100ms);

    set_rotation(rotation_);
}

void TftDisplay::set_rotation(Rotate rotation) {
    std::uint8_t value = 0;
    std::uint16_t width = NATIVE_WIDTH, height = NATIVE_HEIGHT;
    switch (rotation) {
        case Rotate::Degrees0: value = madctl::MY; break;
        case Rotate::Degrees90: value = madctl::MV, width = NATIVE_HEIGHT, height = NATIVE_WIDTH; break;
        case Rotate::Degrees180: value = madctl::MX; break;
        case Rotate::Degrees270:
            value = madctl::MX | madctl::MY | madctl::MV, width = NATIVE_HEIGHT, height = NATIVE_WIDTH;
            break;
    }

    reg(Command::MemoryAccessControl, {static_cast<std::uint8_t>(value | madctl::BGR)});

    rotation_ = rotation;
    width_ = width;
    height_ = height;
}

void TftDisplay::fill_screen(Color color) { fill_rectangle(0, 0, width_, height_, color); }

void TftDisplay::fill_rectangle(std::uint16_t x, std::uint16_t y, std::uint16_t w, std::uint16_t h, Color color) {
    if (w == 0 || h == 0 || x >= width_ || y >= height_) {
        return;
    }
    w = std::min<std::uint16_t>(w, width_ - x);
    h = std::min<std::uint16_t>(h, height_ - y);

    set_addr_window(x, y, x + w - 1, y + h - 1);
    spi_.write_command(Command::MemoryWrite);

    // One transfer's worth of the color, sent as many times as needed
    const std::size_t total = std::size_t{w} * h;
    const Rgb666 px = color.to_rgb666_bytes();
    std::vector<std::uint8_t> chunk;
    chunk.reserve(std::min(total, MAX_TRANSFER_PIXELS) * BYTES_PER_PIXEL);
    for (std::size_t i = 0; i < std::min(total, MAX_TRANSFER_PIXELS); ++i) {
        chunk.insert(chunk.end(), px.begin(), px.end());
    }
    for (std::size_t left = total; left > 0;) {
        const std::size_t n = std::min(left, MAX_TRANSFER_PIXELS);
        spi_.write_data(std::span<const std::uint8_t>(chunk).first(n * BYTES_PER_PIXEL));
        left -= n;
    }
}

void TftDisplay::draw_pixel(std::int32_t x, std::int32_t y, Color color) { fill_rect_clipped({x, y, 1, 1}, color); }

void TftDisplay::draw_line(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Color color) {
    shapes::line(x0, y0, x1, y1, [&](const shapes::Rect& r) { fill_rect_clipped(r, color); });
}

void TftDisplay::draw_rectangle(std::int32_t x, std::int32_t y, std::uint16_t w, std::uint16_t h, Color color) {
    shapes::rectangle(x, y, w, h, [&](const shapes::Rect& r) { fill_rect_clipped(r, color); });
}

void TftDisplay::fill_round_rectangle(std::int32_t x, std::int32_t y, std::uint16_t w, std::uint16_t h,
                                      std::uint16_t radius, Color color) {
    shapes::filled_round_rectangle(x, y, w, h, radius, [&](const shapes::Rect& r) { fill_rect_clipped(r, color); });
}

void TftDisplay::draw_circle(std::int32_t cx, std::int32_t cy, std::uint16_t radius, Color color) {
    shapes::circle(cx, cy, radius, [&](const shapes::Rect& r) { fill_rect_clipped(r, color); });
}

void TftDisplay::fill_circle(std::int32_t cx, std::int32_t cy, std::uint16_t radius, Color color) {
    shapes::filled_circle(cx, cy, radius, [&](const shapes::Rect& r) { fill_rect_clipped(r, color); });
}

void TftDisplay::draw_image(std::int32_t x, std::int32_t y, std::uint16_t width, std::uint16_t height,
                            std::span<const std::uint8_t> rgb) {
    const std::size_t expected = std::size_t{width} * height * 3;
    if (rgb.size() != expected) {
        throw ImageSizeError(rgb.size(), expected);
    }
    const auto visible = clip({x, y, width, height});
    if (!visible) {
        return;
    }
    const auto [x0, y0, x1, y1] = *visible;

    set_addr_window(x0, y0, x1 - 1, y1 - 1);
    spi_.write_command(Command::MemoryWrite);

    // Where the visible part starts in the source image
    const auto src_x = static_cast<std::size_t>(std::int64_t{x0} - x);
    const auto src_y = static_cast<std::size_t>(std::int64_t{y0} - y);
    const std::size_t visible_bytes = std::size_t(x1 - x0) * 3;
    const std::size_t row_bytes = std::size_t{width} * 3;

    // The visible rows one after another, in transfers of up to
    // MAX_TRANSFER_PIXELS (which may end mid-row)
    std::vector<std::uint8_t> buffer;
    buffer.reserve(MAX_TRANSFER_BYTES);
    for (std::size_t row = 0; row < std::size_t(y1 - y0); ++row) {
        const std::size_t start = (src_y + row) * row_bytes + src_x * 3;
        for (std::size_t i = 0; i < visible_bytes; i += BYTES_PER_PIXEL) {
            for (std::size_t c = 0; c < BYTES_PER_PIXEL; ++c) {
                buffer.push_back(rgb[start + i + c] & 0xFC);  // same as Color::to_rgb666_bytes
            }
            if (buffer.size() == MAX_TRANSFER_BYTES) {
                spi_.write_data(buffer);
                buffer.clear();
            }
        }
    }
    if (!buffer.empty()) {
        spi_.write_data(buffer);
    }
}

void TftDisplay::fill_rect_clipped(const shapes::Rect& rect, Color color) {
    if (const auto c = clip(rect)) {
        fill_rectangle(c->x0, c->y0, c->x1 - c->x0, c->y1 - c->y0, color);
    }
}

std::optional<TftDisplay::Clipped> TftDisplay::clip(const shapes::Rect& r) const {
    const std::int64_t x0 = std::max<std::int64_t>(r.x, 0);
    const std::int64_t y0 = std::max<std::int64_t>(r.y, 0);
    const std::int64_t x1 = std::min<std::int64_t>(r.x + r.w, width_);
    const std::int64_t y1 = std::min<std::int64_t>(r.y + r.h, height_);
    if (x1 <= x0 || y1 <= y0) {
        return std::nullopt;
    }
    return Clipped{static_cast<std::uint16_t>(x0), static_cast<std::uint16_t>(y0), static_cast<std::uint16_t>(x1),
                   static_cast<std::uint16_t>(y1)};
}

void TftDisplay::draw_text(std::uint16_t x, std::uint16_t y, std::string_view text, Color fg, Color bg,
                           std::uint8_t size_in) {
    const std::uint16_t size = std::max<std::uint8_t>(size_in, 1);
    std::uint16_t cursor_x = x, cursor_y = y;

    for (const char32_t c : util::decode_utf8(text)) {
        if (c == U'\n') {
            cursor_x = x;
            cursor_y = saturating_add(cursor_y, std::uint32_t{font::GLYPH_HEIGHT} * size);
            continue;
        }
        if (c == U'\r') {
            continue;
        }
        draw_char(cursor_x, cursor_y, c, fg, bg, static_cast<std::uint8_t>(size));
        cursor_x = saturating_add(cursor_x, std::uint32_t{font::CHAR_ADVANCE} * size);
    }
}

std::uint16_t TftDisplay::text_width(std::string_view text, std::uint8_t size) {
    // Same rules as draw_text: lines end at '\n', '\r' takes no space
    std::size_t longest = 0, current = 0;
    for (const char32_t c : util::decode_utf8(text)) {
        if (c == U'\n') {
            current = 0;
        } else if (c != U'\r') {
            longest = std::max(longest, ++current);
        }
    }
    const std::uint64_t width = std::uint64_t{longest} * font::CHAR_ADVANCE * std::max<std::uint8_t>(size, 1);
    return static_cast<std::uint16_t>(std::min<std::uint64_t>(width, 0xFFFF));
}

void TftDisplay::draw_char(std::uint16_t x, std::uint16_t y, char32_t c, Color fg_color, Color bg_color,
                           std::uint8_t size_in) {
    if (x >= width_ || y >= height_) {
        return;
    }

    const std::uint16_t size = std::max<std::uint8_t>(size_in, 1);
    const auto w = static_cast<std::uint16_t>(std::min<std::uint32_t>(font::CHAR_ADVANCE * size, width_ - x));
    const auto h = static_cast<std::uint16_t>(std::min<std::uint32_t>(font::GLYPH_HEIGHT * size, height_ - y));

    const auto glyph = font::glyph(c);
    const Rgb666 fg = fg_color.to_rgb666_bytes();
    const Rgb666 bg = bg_color.to_rgb666_bytes();

    std::vector<std::uint8_t> buffer;
    buffer.reserve(std::size_t{w} * h * BYTES_PER_PIXEL);
    for (std::uint16_t py = 0; py < h; ++py) {
        const unsigned row = py / size;
        for (std::uint16_t px = 0; px < w; ++px) {
            const unsigned col = px / size;
            const bool on = col < glyph.size() && ((glyph[col] >> row) & 1) == 1;
            const Rgb666& color = on ? fg : bg;
            buffer.insert(buffer.end(), color.begin(), color.end());
        }
    }

    set_addr_window(x, y, x + w - 1, y + h - 1);
    spi_.write_command(Command::MemoryWrite);
    // Usually the whole cell in one transfer
    for (std::size_t offset = 0; offset < buffer.size(); offset += MAX_TRANSFER_BYTES) {
        spi_.write_data(std::span<const std::uint8_t>(buffer).subspan(
            offset, std::min(MAX_TRANSFER_BYTES, buffer.size() - offset)));
    }
}

void TftDisplay::set_addr_window(std::uint16_t x0, std::uint16_t y0, std::uint16_t x1, std::uint16_t y1) {
    const std::array<std::pair<std::uint16_t, std::uint16_t>, 4> checks{
        {{x0, width_}, {x1, width_}, {y0, height_}, {y1, height_}}};
    for (const auto& [given, max] : checks) {
        if (given >= max) {
            throw SizeError(given, max);
        }
    }

    // Each coordinate as its own 2-byte transfer, as tft-spi-demo does
    spi_.write_command(Command::ColumnAddressSet);
    spi_.write_data(be_bytes(x0));
    spi_.write_data(be_bytes(x1));
    spi_.write_command(Command::RowAddressSet);
    spi_.write_data(be_bytes(y0));
    spi_.write_data(be_bytes(y1));
}

}  // namespace tft
