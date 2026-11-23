#include "app.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "calibrate.hpp"
#include "picture.hpp"
#include "rpi/tft_spi_impl.hpp"
#include "rpi/tft_touch.hpp"
#include "tft_display/tft_display.hpp"
#include "ui.hpp"
#include "util/strings.hpp"

namespace app {

using namespace tft;
using namespace std::chrono_literals;

namespace {

/// Saved in the current directory. Run with `--calibrate` to redo it.
constexpr const char* CALIBRATION_FILE = "touch_calibration.txt";

/// Landscape screen of gradients, dark (left) to bright (right): red, green,
/// blue, grey, and orange to beige (the colors of the sample photo). Each
/// band should brighten smoothly and keep its hue.
void color_test(TftDisplay& display) {
    display.set_rotation(Rotate::Degrees90);
    const std::size_t w = display.width(), h = display.height();
    const std::array<std::pair<const char*, std::array<std::uint8_t, 3>>, 6> bands{{
        {"red", {255, 0, 0}},
        {"green", {0, 255, 0}},
        {"blue", {0, 0, 255}},
        {"grey", {255, 255, 255}},
        {"orange", {255, 128, 32}},
        {"beige", {240, 210, 180}},
    }};
    const std::size_t band_h = h / bands.size();
    std::vector<std::uint8_t> rgb(w * h * 3);
    for (std::size_t y = 0; y < h; ++y) {
        const auto& full = bands[std::min(y / band_h, bands.size() - 1)].second;
        for (std::size_t x = 0; x < w; ++x) {
            for (std::size_t c = 0; c < 3; ++c) {
                rgb[(y * w + x) * 3 + c] = static_cast<std::uint8_t>(std::size_t{full[c]} * x / (w - 1));
            }
        }
    }
    display.draw_image(0, 0, std::uint16_t(w), std::uint16_t(h), rgb);
    for (std::size_t i = 0; i < bands.size(); ++i) {
        const auto y = static_cast<std::uint16_t>(i * band_h + band_h / 2 - 4);
        display.draw_text(4, y, bands[i].first, Color::WHITE, Color::BLACK, 1);
    }
}

/// Lines and circles, including some that run off the screen edges.
void shapes_test(TftDisplay& display) {
    display.set_rotation(Rotate::Degrees90);
    display.fill_screen(Color::BLACK);
    const std::int32_t w = display.width(), h = display.height();

    // Fan of lines from the top-left corner to points along the bottom and right edges
    for (std::int32_t i = 0; i <= 8; ++i) {
        display.draw_line(0, 0, i * (w - 1) / 8, h - 1, Color::CYAN);
        display.draw_line(0, 0, w - 1, i * (h - 1) / 8, Color::GREEN);
    }

    // Concentric outlines, a filled circle, and circles clipped by the edges
    for (std::uint16_t r = 10; r <= 60; r += 10) {
        display.draw_circle(w * 3 / 4, h / 2, r, Color::YELLOW);
    }
    display.fill_circle(w * 3 / 4, h / 2, 6, Color::RED);
    display.fill_circle(w - 1, h - 1, 40, Color::MAGENTA);
    display.draw_circle(-10, h / 2, 50, Color::WHITE);

    // Nested rectangle outlines, and one clipped by the top edge
    for (std::int32_t i = 0; i < 5; ++i) {
        const std::int32_t inset = i * 8;
        const auto size = static_cast<std::uint16_t>(100 - 2 * inset);
        display.draw_rectangle(w / 4 - 50 + inset, h / 2 - 50 + inset, size, size, Color::RED);
    }
    display.draw_rectangle(w / 2 - 30, -20, 60, 50, Color::BLUE);

    // Rounded rectangles: small corners, a pill (radius larger than half the
    // height), and one clipped by the left edge
    display.fill_round_rectangle(w / 2 - 60, h / 2 + 40, 120, 40, 6, Color::GREEN);
    display.fill_round_rectangle(w / 2 - 60, h / 2 + 90, 120, 30, 100, Color::BROWN);
    display.fill_round_rectangle(-20, 10, 80, 50, 15, Color::TAN);

    const auto text_y = static_cast<std::uint16_t>(display.height() - 20);
    display.draw_text(10, text_y, "lines, circles, rectangles", Color::WHITE, Color::BLACK, 2);
    std::this_thread::sleep_for(2s);
}

/// Shows a picture centered on the screen, rotating the screen to portrait
/// for portrait pictures. Fit leaves black borders; Fill crops to cover.
void show_image(TftDisplay& display, const std::filesystem::path& path, picture::Scale scale) {
    const auto start = std::chrono::steady_clock::now();
    picture::Image image;
    try {
        image = picture::open(path);
    } catch (const std::exception& err) {
        throw std::runtime_error(path.string() + ": " + err.what());
    }
    std::cout << path.string() << ": " << image.width << 'x' << image.height << ", decoded in "
              << util::format_duration(std::chrono::steady_clock::now() - start) << '\n';

    const picture::Shown shown = picture::show(display, image, scale);
    std::cout << "scaled to " << shown.width << 'x' << shown.height << " (" << picture::to_string(scale) << ") in "
              << util::format_duration(shown.scale_time) << ", drawn at (" << shown.x << ", " << shown.y << ") in "
              << util::format_duration(shown.draw_time) << '\n';
}

/// Each orientation gets a background color and a green block in its
/// bottom-left corner.
void rotation_test(TftDisplay& display) {
    const std::array<std::pair<Rotate, Color>, 4> rotations{{
        {Rotate::Degrees0, Color::BLUE},
        {Rotate::Degrees90, Color::RED},
        {Rotate::Degrees180, Color::YELLOW},
        {Rotate::Degrees270, Color::MAGENTA},
    }};
    for (const auto& [rotation, background] : rotations) {
        display.set_rotation(rotation);
        std::cout << rotation << ": " << display.width() << 'x' << display.height() << '\n';
        display.fill_screen(background);
        display.fill_rectangle(0, display.height() - 40, 120, 40, Color::GREEN);
        // Upright, readable text means the rotation is right; mirrored or
        // sideways text shows which way it's wrong.
        const std::string label = std::to_string(degrees(rotation)) + " deg\n" + std::to_string(display.width()) +
                                  "x" + std::to_string(display.height());
        display.draw_text(20, 20, label, Color::BLACK, background, 4);
        std::this_thread::sleep_for(3s);
    }
}

}  // namespace

void run(const std::vector<std::string>& args) {
    const auto has_flag = [&](std::string_view flag) { return std::find(args.begin(), args.end(), flag) != args.end(); };
    const auto image_path =
        std::find_if(args.begin(), args.end(), [](const std::string& arg) { return arg.rfind("--", 0) != 0; });

    // Display on CE0 (held low as a GPIO), touch on CE1: separate SPI devices.
    rpi::TftSpiImpl spi;
    TftDisplay display(spi);
    Gamma gamma = Gamma::PanelDefault;
    if (const char* name = std::getenv("TFT_GAMMA")) {
        if (const auto g = gamma_from_name(name)) {
            gamma = *g;
        } else {
            std::cerr << "ignoring TFT_GAMMA=" << util::quoted(name) << ": use full, analog or none\n";
        }
    }
    std::cout << "gamma: " << to_string(gamma) << " (set TFT_GAMMA=full, analog or none to change)\n";
    display.set_gamma(gamma);
    display.initialize();

    if (has_flag("--colors")) {
        color_test(display);
        return;
    }

    if (image_path != args.end()) {
        show_image(display, *image_path, has_flag("--fill") ? picture::Scale::Fill : picture::Scale::Fit);
        return;
    }

    const bool force_calibration = has_flag("--calibrate");
    rpi::TftTouch touch(spi);

    rotation_test(display);
    shapes_test(display);

    // Landscape UI. The calibration only applies to the rotation it was made in.
    display.set_rotation(Rotate::Degrees90);
    const Calibration calibration =
        calibrate::load_or_calibrate(display, touch, CALIBRATION_FILE, force_calibration);

    ui::draw_ui(display);

    // Drag to draw; tap a button to highlight it. Ctrl-C to quit.
    std::cout << "touch the screen (Ctrl-C to quit)..." << std::endl;
    // Where the current stroke last reached. A flag plus a point rather than a
    // std::optional, which trips a false -Wmaybe-uninitialized in g++ 12.
    bool in_stroke = false;
    std::int32_t last_x = 0, last_y = 0;
    for (;;) {
        const auto raw = touch.read_filtered(rpi::TftTouch::FILTER_SAMPLES);
        if (!raw) {
            in_stroke = false;  // finger lifted: next touch starts a new stroke
            std::this_thread::sleep_for(10ms);
            continue;
        }
        const auto [x, y] = calibration.to_screen(*raw, display.width(), display.height());

        if (const auto index = ui::button_at(x, y, display.height())) {
            in_stroke = false;
            std::cout << "touch (" << x << ", " << y << "): button " << ui::BUTTONS[*index].label << std::endl;
            ui::draw_button(display, *index, true);
            touch.wait_for_release();
            ui::draw_button(display, *index, false);
        } else {
            std::cout << "touch (" << x << ", " << y << ") raw " << *raw << std::endl;
            if (in_stroke) {
                display.draw_line(last_x, last_y, x, y, Color::WHITE);
            } else {
                display.fill_circle(x, y, 1, Color::WHITE);
            }
            in_stroke = true;
            last_x = x;
            last_y = y;
        }
    }
}

}  // namespace app
