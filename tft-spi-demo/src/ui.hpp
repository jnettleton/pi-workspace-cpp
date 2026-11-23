#pragma once

//! The demo's touch screen: a title, some text and a row of buttons along
//! the bottom of a landscape screen.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "tft_display/tft_display.hpp"

namespace ui {

inline constexpr std::uint16_t BUTTON_W = 120;
inline constexpr std::uint16_t BUTTON_H = 40;
inline constexpr std::uint16_t BUTTON_RADIUS = 8;
/// Black gap around each button
inline constexpr std::uint16_t BUTTON_GAP = 2;
inline constexpr std::uint8_t LABEL_SIZE = 2;

struct Button {
    std::string_view label;
    tft::Color text;
    tft::Color button;
};

inline constexpr std::array<Button, 4> BUTTONS{{
    {"Executed", tft::Color::WHITE, tft::Color::RED},
    {"Scheduled", tft::Color::BLACK, tft::Color::YELLOW},
    {"Routines", tft::Color::WHITE, tft::Color::GREY},
    {"Scenes", tft::Color::BLACK, tft::Color::MAGENTA},
}};

/// Clears the screen and draws a title, some sample text and the buttons.
void draw_ui(tft::TftDisplay& display);

/// Draws a button; `pressed` swaps its text and background colors.
void draw_button(tft::TftDisplay& display, std::size_t index, bool pressed);

/// Which button a touch at screen (x, y) is on, if any, for a screen
/// `screen_height` pixels tall. The whole bottom strip counts, gaps included.
std::optional<std::size_t> button_at(std::uint16_t x, std::uint16_t y, std::uint16_t screen_height);

}  // namespace ui
