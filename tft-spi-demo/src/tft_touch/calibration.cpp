#include "tft_touch/calibration.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

#include "util/strings.hpp"

namespace tft {

namespace {

using Matrix3 = std::array<std::array<double, 3>, 3>;

double det3(const Matrix3& m) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/// Solves the 3x3 system `m * c = v` with Cramer's rule.
std::optional<std::array<double, 3>> solve3(const Matrix3& m, const std::array<double, 3>& v) {
    const double det = det3(m);
    const double scale = m[0][0] * m[1][1] * m[2][2];
    if (!std::isfinite(det) || std::abs(det) <= 1e-9 * std::abs(scale)) {
        return std::nullopt;
    }

    std::array<double, 3> result{};
    for (std::size_t col = 0; col < 3; ++col) {
        Matrix3 mc = m;
        for (std::size_t row = 0; row < 3; ++row) {
            mc[row][col] = v[row];
        }
        result[col] = det3(mc) / det;
    }
    return result;
}

std::string shortest(double v) {
    char buf[64];
    const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
    return ec == std::errc() ? std::string(buf, end) : std::string("nan");
}

}  // namespace

std::optional<Calibration> Calibration::fit(std::span<const CalibrationPoint> points) {
    if (points.size() < 3) {
        return std::nullopt;
    }

    // Normal equations: (Aᵀ A) c = Aᵀ s, with rows of A = [raw_x, raw_y, 1]
    Matrix3 ata{};
    std::array<double, 3> atx{}, aty{};
    for (const auto& p : points) {
        const std::array<double, 3> row{double(p.raw.first), double(p.raw.second), 1.0};
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                ata[i][j] += row[i] * row[j];
            }
            atx[i] += row[i] * p.screen.first;
            aty[i] += row[i] * p.screen.second;
        }
    }

    const auto x = solve3(ata, atx);
    const auto y = solve3(ata, aty);
    if (!x || !y) {
        return std::nullopt;
    }
    return Calibration{*x, *y};
}

std::pair<double, double> Calibration::map(Point raw) const {
    const double rx = raw.first, ry = raw.second;
    return {x[0] * rx + x[1] * ry + x[2], y[0] * rx + y[1] * ry + y[2]};
}

Point Calibration::to_screen(Point raw, std::uint16_t width, std::uint16_t height) const {
    const auto [sx, sy] = map(raw);
    const auto clamp = [](double v, std::uint16_t size) {
        if (std::isnan(v)) return std::uint16_t{0};
        const double max = size > 0 ? size - 1 : 0;
        return static_cast<std::uint16_t>(std::clamp(std::round(v), 0.0, max));
    };
    return {clamp(sx, width), clamp(sy, height)};
}

double Calibration::max_error(std::span<const CalibrationPoint> points) const {
    double worst = 0.0;
    for (const auto& p : points) {
        const auto [mx, my] = map(p.raw);
        worst = std::max(worst, std::hypot(mx - p.screen.first, my - p.screen.second));
    }
    return worst;
}

std::string Calibration::to_text() const {
    std::string out;
    for (const double v : {x[0], x[1], x[2], y[0], y[1], y[2]}) {
        if (!out.empty()) out += ' ';
        out += shortest(v);
    }
    return out;
}

std::optional<Calibration> Calibration::from_text(std::string_view text) {
    const auto words = util::split_whitespace(text);
    if (words.size() != 6) {
        return std::nullopt;
    }
    std::array<double, 6> values{};
    for (std::size_t i = 0; i < 6; ++i) {
        const auto v = util::parse<double>(words[i]);
        if (!v || !std::isfinite(*v)) {
            return std::nullopt;
        }
        values[i] = *v;
    }
    return Calibration{{values[0], values[1], values[2]}, {values[3], values[4], values[5]}};
}

}  // namespace tft
