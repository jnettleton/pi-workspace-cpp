#include "ui.hpp"

#include <algorithm>

namespace ui {

using namespace tft;

void draw_ui(TftDisplay& display) {
    display.fill_screen(Color::BLACK);
    display.draw_text(10, 10, "Hello, World!", Color::WHITE, Color::BLUE, 4);
    display.draw_text(10, 60, "Touch to draw, tap a button\nRun with --calibrate to redo", Color::GREEN,
                      Color::BLACK, 2);

    for (std::size_t index = 0; index < BUTTONS.size(); ++index) {
        draw_button(display, index, false);
    }
}

void draw_button(TftDisplay& display, std::size_t index, bool pressed) {
    const Button& b = BUTTONS[index];
    const Color fg = pressed ? b.button : b.text;
    const Color bg = pressed ? b.text : b.button;

    const auto button_x = static_cast<std::uint16_t>(index * BUTTON_W);
    const auto button_y = static_cast<std::uint16_t>(display.height() - BUTTON_H);
    // Rounded button with a black gap around it
    display.fill_rectangle(button_x, button_y, BUTTON_W, BUTTON_H, Color::BLACK);
    display.fill_round_rectangle(button_x + BUTTON_GAP, button_y + BUTTON_GAP, BUTTON_W - 2 * BUTTON_GAP,
                                 BUTTON_H - 2 * BUTTON_GAP, BUTTON_RADIUS, bg);

    // Center the label in the button. text_width includes the 1-column gap
    // after the last character, which isn't visible, so leave it out.
    const std::uint16_t width = TftDisplay::text_width(b.label, LABEL_SIZE);
    const std::uint16_t visible_w = width > LABEL_SIZE ? width - LABEL_SIZE : 0;
    const auto label_x = static_cast<std::uint16_t>(button_x + (BUTTON_W > visible_w ? BUTTON_W - visible_w : 0) / 2);
    const auto label_y = static_cast<std::uint16_t>(button_y + (BUTTON_H - 8 * LABEL_SIZE) / 2);
    display.draw_text(label_x, label_y, b.label, fg, bg, LABEL_SIZE);
}

std::optional<std::size_t> button_at(std::uint16_t x, std::uint16_t y, std::uint16_t screen_height) {
    const std::uint16_t top = screen_height > BUTTON_H ? screen_height - BUTTON_H : 0;
    if (y < top || y >= screen_height) {
        return std::nullopt;
    }
    return std::min<std::size_t>(x / BUTTON_W, BUTTONS.size() - 1);
}

}  // namespace ui
