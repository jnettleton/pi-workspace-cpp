#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "calibrate.hpp"
#include "check.hpp"
#include "helpers.hpp"
#include "util/strings.hpp"

using namespace tft;
using namespace helpers;
using namespace std::chrono_literals;

namespace {

using Crosshair = std::optional<std::pair<std::size_t, std::size_t>>;

/// Center of the red crosshair on the panel, or nothing unless there is
/// exactly one: the row and the column that each hold more than one red pixel.
Crosshair red_crosshair(const mock::MockSpi& p) {
    const Rgb666 red = Color::RED.to_rgb666_bytes();
    std::vector<std::size_t> rows, cols;
    for (std::size_t y = 0; y < p.height; ++y) {
        std::size_t n = 0;
        for (std::size_t x = 0; x < p.width; ++x) n += p.pixel(x, y) == red;
        if (n > 1) rows.push_back(y);
    }
    for (std::size_t x = 0; x < p.width; ++x) {
        std::size_t n = 0;
        for (std::size_t y = 0; y < p.height; ++y) n += p.pixel(x, y) == red;
        if (n > 1) cols.push_back(x);
    }
    if (rows.size() == 1 && cols.size() == 1) return std::pair{cols[0], rows[0]};
    return std::nullopt;
}

/// Touches played back in order. Each time a touch is requested it also
/// records where the red target crosshair is on the mock panel.
class ScriptedTouch : public TouchInput {
public:
    ScriptedTouch(const mock::MockSpi& panel, std::vector<Point> touches)
        : panel_(panel), touches(touches.begin(), touches.end()) {}

    Point wait_for_touch() override {
        targets_seen.push_back(red_crosshair(panel_));
        if (touches.empty()) throw std::runtime_error("no more scripted touches");
        const Point p = touches.front();
        touches.pop_front();
        return p;
    }
    void wait_for_release() override { ++releases; }

private:
    const mock::MockSpi& panel_;

public:
    std::deque<Point> touches;
    std::vector<Crosshair> targets_seen;
    std::size_t releases = 0;
};

/// Raw reading for a screen point on a simulated touch panel whose axes
/// are swapped and mirrored relative to the 480x320 landscape screen.
Point raw_for(Point p) {
    const double raw_x = 200.0 + p.second * (3700.0 / 320.0);
    const double raw_y = 3900.0 - p.first * (3600.0 / 480.0);
    return {std::uint16_t(std::round(raw_x)), std::uint16_t(std::round(raw_y))};
}

/// The 5 targets on a 480x320 screen, in the order they are shown
const std::vector<Point> TARGETS{{30, 30}, {449, 30}, {449, 289}, {30, 289}, {240, 160}};

std::vector<Point> accurate_touches() {
    std::vector<Point> out;
    for (const Point& t : TARGETS) out.push_back(raw_for(t));
    return out;
}

std::vector<Point> concat(std::vector<Point> a, const std::vector<Point>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::vector<Crosshair> as_seen(const std::vector<Point>& targets) {
    std::vector<Crosshair> out;
    for (const auto& [x, y] : targets) out.push_back(std::pair<std::size_t, std::size_t>{x, y});
    return out;
}

void check_maps_accurately(const Calibration& calibration) {
    std::vector<Point> points = TARGETS;
    points.insert(points.end(), {{100, 250}, {400, 60}, {0, 0}, {479, 319}});
    for (const Point& point : points) {
        const auto [x, y] = calibration.to_screen(raw_for(point), 480, 320);
        const double error = std::hypot(double(x) - point.first, double(y) - point.second);
        CHECK(error <= 1.5, check::show(point), " mapped to (", x, ", ", y, ")");
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

void write_file(const std::filesystem::path& path, const std::string& contents) { std::ofstream(path) << contents; }

bool contains(const std::vector<Rgb666>& pixels, Color c) {
    return std::find(pixels.begin(), pixels.end(), c.to_rgb666_bytes()) != pixels.end();
}

}  // namespace

TEST(calibrate_run_shows_targets_in_order_and_fits) {
    Landscape s;
    ScriptedTouch touch(s.spi, accurate_touches());
    const Calibration calibration = calibrate::run(s.display, touch, 0ms);

    check_maps_accurately(calibration);
    CHECK_EQ(touch.targets_seen, as_seen(TARGETS), "one red target at a time, in order");
    CHECK_EQ(touch.releases, 5u);

    // Ends on the "Calibrated" message: green text, no targets left
    CHECK(contains(s.spi.pixels, Color::GREEN));
    CHECK(!contains(s.spi.pixels, Color::RED));
}

TEST(calibrate_run_retries_after_an_inaccurate_round) {
    Landscape s;
    // First round: the center touch lands 60 px right of the target
    auto first = accurate_touches();
    first[4] = raw_for({300, 160});
    ScriptedTouch touch(s.spi, concat(first, accurate_touches()));

    const Calibration calibration = calibrate::run(s.display, touch, 0ms);
    check_maps_accurately(calibration);
    CHECK_EQ(touch.targets_seen.size(), 10u, "two full rounds of targets");
    CHECK(touch.touches.empty());
}

TEST(calibrate_run_retries_after_unusable_readings) {
    Landscape s;
    // First round: every touch reads the same, so no calibration can be fitted
    ScriptedTouch touch(s.spi, concat(std::vector<Point>(5, {2000, 2000}), accurate_touches()));
    const Calibration calibration = calibrate::run(s.display, touch, 0ms);
    check_maps_accurately(calibration);
    CHECK_EQ(touch.targets_seen.size(), 10u);
}

TEST(calibrate_run_passes_on_touch_errors) {
    Landscape s;
    auto two = accurate_touches();
    two.resize(2);
    ScriptedTouch touch(s.spi, two);
    const auto err = CHECK_THROWS(std::runtime_error, calibrate::run(s.display, touch, 0ms));
    CHECK_EQ(std::string(err.what()), std::string("no more scripted touches"));
}

TEST(calibrate_load_or_calibrate_saves_then_loads) {
    TempDir dir("save-load");
    const auto path = dir.path / "touch_calibration.txt";
    Landscape s;

    // No file yet: calibrates and saves
    ScriptedTouch touch(s.spi, accurate_touches());
    const Calibration calibrated = calibrate::load_or_calibrate(s.display, touch, path, false, 0ms);
    CHECK_EQ(touch.targets_seen.size(), 5u);
    const std::string saved = read_file(path);
    CHECK(saved.rfind("90 ", 0) == 0, "file records the rotation: ", saved);
    CHECK_EQ(util::split_whitespace(saved).size(), 7u, "rotation + 6 coefficients: ", saved);

    // File present: loads it without asking for touches
    ScriptedTouch no_touches(s.spi, {});
    const Calibration loaded = calibrate::load_or_calibrate(s.display, no_touches, path, false, 0ms);
    CHECK(loaded == calibrated);
    CHECK(no_touches.targets_seen.empty());
}

TEST(calibrate_load_or_calibrate_recalibrates_when_forced) {
    TempDir dir("force");
    const auto path = dir.path / "touch_calibration.txt";
    Landscape s;
    ScriptedTouch touch(s.spi, concat(accurate_touches(), accurate_touches()));

    calibrate::load_or_calibrate(s.display, touch, path, false, 0ms);
    calibrate::load_or_calibrate(s.display, touch, path, true, 0ms);
    CHECK_EQ(touch.targets_seen.size(), 10u, "--calibrate runs it again");
}

TEST(calibrate_load_or_calibrate_ignores_unusable_files) {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"other-rotation", "0 1 0 0 0 1 0\n"}, {"garbage", "not a calibration\n"},
        {"too-few-numbers", "90 1 2 3\n"},     {"bad-rotation", "45 1 0 0 0 1 0\n"},
        {"empty", ""},
    };
    for (const auto& [name, contents] : cases) {
        TempDir dir(name);
        const auto path = dir.path / "touch_calibration.txt";
        write_file(path, contents);

        Landscape s;
        ScriptedTouch touch(s.spi, accurate_touches());
        const Calibration calibration = calibrate::load_or_calibrate(s.display, touch, path, false, 0ms);
        CHECK_EQ(touch.targets_seen.size(), 5u, name, ": should calibrate");
        check_maps_accurately(calibration);
        CHECK(read_file(path).rfind("90 ", 0) == 0, name, ": file replaced");
    }
}

TEST(calibrate_is_per_rotation) {
    TempDir dir("rotation");
    const auto path = dir.path / "touch_calibration.txt";
    {
        Landscape s;
        ScriptedTouch touch(s.spi, accurate_touches());
        calibrate::load_or_calibrate(s.display, touch, path, false, 0ms);
    }

    // Same file, display now in portrait: needs a new calibration
    Screen portrait(Rotate::Degrees0);
    const std::vector<Point> portrait_targets{{30, 30}, {289, 30}, {289, 449}, {30, 449}, {160, 240}};
    std::vector<Point> touches;
    for (const auto& [x, y] : portrait_targets) touches.push_back(raw_for({y, x}));
    ScriptedTouch touch(portrait.spi, touches);
    calibrate::load_or_calibrate(portrait.display, touch, path, false, 0ms);

    CHECK_EQ(touch.targets_seen, as_seen(portrait_targets), "portrait targets");
    CHECK(read_file(path).rfind("0 ", 0) == 0);
}
