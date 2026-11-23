#include "calibrate.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "util/strings.hpp"

namespace calibrate {

using namespace tft;

namespace {

/// Distance of the corner targets from the screen edges
constexpr std::uint16_t TARGET_INSET = 30;
/// Half-length of the target crosshair arms
constexpr std::uint16_t TARGET_ARM = 10;
/// A calibration is accepted if every target maps back within this many pixels.
constexpr double MAX_ERROR_PX = 8.0;

std::optional<Calibration> load(const std::filesystem::path& path, Rotate rotation) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }
    std::stringstream contents;
    contents << file.rdbuf();
    const std::string text = contents.str();

    const std::string_view trimmed = util::trim(text);
    const std::size_t space = trimmed.find(' ');
    if (space == std::string_view::npos) {
        return std::nullopt;
    }
    const auto degrees = util::parse<std::uint16_t>(trimmed.substr(0, space));
    if (!degrees) {
        return std::nullopt;
    }
    const auto saved_rotation = rotate_from_degrees(*degrees);
    if (!saved_rotation || *saved_rotation != rotation) {
        return std::nullopt;
    }
    return Calibration::from_text(trimmed.substr(space + 1));
}

void save(const std::filesystem::path& path, Rotate rotation, const Calibration& calibration) {
    std::ofstream file(path, std::ios::trunc);
    file << degrees(rotation) << ' ' << calibration.to_text() << '\n';
    file.flush();
    if (!file) {
        throw std::runtime_error("couldn't write " + path.string());
    }
}

void draw_target(TftDisplay& display, Point target, Color color) {
    const auto [x, y] = target;
    constexpr std::uint16_t arm_len = 2 * TARGET_ARM + 1;
    display.fill_rectangle(x - TARGET_ARM, y, arm_len, 1, color);
    display.fill_rectangle(x, y - TARGET_ARM, 1, arm_len, color);
}

void draw_centered(TftDisplay& display, std::uint16_t y, std::string_view text, Color color, std::uint8_t size) {
    const std::uint16_t text_w = TftDisplay::text_width(text, size);
    const std::uint16_t x = display.width() > text_w ? (display.width() - text_w) / 2 : 0;
    display.draw_text(x, y, text, color, Color::BLACK, size);
}

}  // namespace

Calibration load_or_calibrate(TftDisplay& display, TouchInput& touch, const std::filesystem::path& path, bool force,
                              std::chrono::milliseconds message_time) {
    if (!force) {
        if (auto calibration = load(path, display.rotation())) {
            std::cout << "loaded touch calibration from " << path.string() << '\n';
            return *calibration;
        }
        std::cout << "no touch calibration for " << degrees(display.rotation()) << " degrees in " << path.string()
                  << ", calibrating\n";
    }

    const Calibration calibration = run(display, touch, message_time);
    save(path, display.rotation(), calibration);
    std::cout << "saved touch calibration to " << path.string() << '\n';
    return calibration;
}

Calibration run(TftDisplay& display, TouchInput& touch, std::chrono::milliseconds message_time) {
    const std::uint16_t w = display.width(), h = display.height();
    const std::array<Point, 5> targets{{
        {TARGET_INSET, TARGET_INSET},
        {w - 1 - TARGET_INSET, TARGET_INSET},
        {w - 1 - TARGET_INSET, h - 1 - TARGET_INSET},
        {TARGET_INSET, h - 1 - TARGET_INSET},
        {w / 2, h / 2},
    }};

    for (;;) {
        display.fill_screen(Color::BLACK);
        draw_centered(display, h / 2 + 30, "Touch each target", Color::WHITE, 2);

        std::vector<CalibrationPoint> points;
        points.reserve(targets.size());
        for (const Point& target : targets) {
            draw_target(display, target, Color::RED);
            const Point raw = touch.wait_for_touch();
            draw_target(display, target, Color::GREEN);
            touch.wait_for_release();
            draw_target(display, target, Color::BLACK);

            std::cout << "target " << target << ": raw " << raw << '\n';
            points.push_back({raw, target});
        }

        std::string message;
        if (const auto calibration = Calibration::fit(points)) {
            const double error = calibration->max_error(points);
            std::cout << "calibration max error " << std::fixed << std::setprecision(1) << error << " px\n"
                      << std::defaultfloat;
            if (error <= MAX_ERROR_PX) {
                display.fill_screen(Color::BLACK);
                draw_centered(display, h / 2 - 8, "Calibrated", Color::GREEN, 2);
                std::this_thread::sleep_for(message_time / 2);
                return *calibration;
            }
            char buf[64];
            std::snprintf(buf, sizeof buf, "Off by %.0f px, try again", error);
            message = buf;
        } else {
            message = "Bad readings, try again";
        }

        display.fill_screen(Color::BLACK);
        draw_centered(display, h / 2 - 8, message, Color::YELLOW, 2);
        std::this_thread::sleep_for(message_time);
    }
}

}  // namespace calibrate
