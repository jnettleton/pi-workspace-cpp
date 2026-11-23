#include <cmath>
#include <vector>

#include "check.hpp"
#include "tft_touch/calibration.hpp"
#include "tft_touch/filter.hpp"

using namespace tft;
using Points = std::vector<Point>;

// ---- filter ----

TEST(median_of_each_axis) {
    CHECK_EQ(median_reading(Points{{100, 900}, {300, 700}, {200, 800}}), std::optional<Point>({200, 800}));
    // Axes are independent: the result needn't be one of the readings
    CHECK_EQ(median_reading(Points{{1, 30}, {2, 10}, {3, 20}}), std::optional<Point>({2, 20}));
    // Even count: the upper of the two middle values
    CHECK_EQ(median_reading(Points{{10, 10}, {20, 20}, {30, 30}, {40, 40}}), std::optional<Point>({30, 30}));
    CHECK_EQ(median_reading(Points{{1234, 2345}}), std::optional<Point>({1234, 2345}));
}

TEST(median_ignores_wild_readings) {
    Points readings(10, {2000, 1500});
    readings.insert(readings.end(), {{4000, 100}, {50, 3900}, {2001, 1501}});
    CHECK_EQ(median_reading(readings), std::optional<Point>({2000, 1500}));
}

TEST(median_drops_stuck_readings) {
    CHECK_EQ(median_reading(Points{{0, 1500}, {2000, 4095}, {4095, 0}, {2000, 1500}, {0, 0}}),
             std::optional<Point>({2000, 1500}));
    CHECK(!median_reading(Points{{0, 0}, {4095, 4095}, {0, 1000}}));
    // 1 and 4094 are real readings
    CHECK_EQ(median_reading(Points{{1, 4094}}), std::optional<Point>({1, 4094}));
}

TEST(median_no_readings) { CHECK(!median_reading(Points{})); }

// ---- calibration math ----

namespace {

/// Simulates a touch panel whose axes are swapped and mirrored relative to
/// a 480x320 landscape screen: raw_x follows screen y, raw_y follows
/// (inverted) screen x.
Point simulated_raw(Point screen) {
    const double sx = screen.first, sy = screen.second;
    const double raw_x = 200.0 + sy * (3700.0 / 320.0);
    const double raw_y = 3900.0 - sx * (3600.0 / 480.0);
    return {std::uint16_t(std::round(raw_x)), std::uint16_t(std::round(raw_y))};
}

std::vector<CalibrationPoint> points(const Points& targets) {
    std::vector<CalibrationPoint> out;
    for (const Point& screen : targets) out.push_back({simulated_raw(screen), screen});
    return out;
}

}  // namespace

TEST(calibration_fits_swapped_and_mirrored_axes) {
    const auto pts = points({{30, 30}, {450, 30}, {450, 290}, {30, 290}, {240, 160}});
    const auto cal = Calibration::fit(pts);
    CHECK(cal.has_value(), "fit");
    CHECK(cal->max_error(pts) < 1.0, "max error ", cal->max_error(pts));
    // A point that wasn't used for the fit
    CHECK_EQ(cal->to_screen(simulated_raw({100, 250}), 480, 320), (Point{100, 250}));
}

TEST(calibration_clamps_to_screen) {
    const auto cal = Calibration::fit(points({{30, 30}, {450, 30}, {450, 290}, {30, 290}}));
    CHECK(cal.has_value());
    // Raw readings past the panel edges
    CHECK_EQ(cal->to_screen({0, 4095}, 480, 320), (Point{0, 0}));
    CHECK_EQ(cal->to_screen({4095, 0}, 480, 320), (Point{479, 319}));
}

TEST(calibration_rejects_too_few_or_collinear_points) {
    CHECK(!Calibration::fit(points({{30, 30}, {450, 290}})));
    CHECK(!Calibration::fit(points({{30, 30}, {240, 160}, {450, 290}})));
}

TEST(calibration_text_round_trip) {
    const auto cal = Calibration::fit(points({{30, 30}, {450, 30}, {450, 290}, {30, 290}}));
    CHECK(cal.has_value());
    CHECK(Calibration::from_text(cal->to_text()) == cal, cal->to_text());
    CHECK(!Calibration::from_text("1 2 3"));
    CHECK(!Calibration::from_text("1 2 3 4 5 x"));
    CHECK(!Calibration::from_text("1 2 3 4 5 NaN"));
    CHECK(!Calibration::from_text("1 2 3 4 5 6 7"));
}
