#pragma once

//! Loading JPEG/PNG files and scaling them for the display.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "tft_display/enums.hpp"
#include "tft_display/tft_display.hpp"

namespace picture {

/// How to scale a picture to the screen.
enum class Scale {
    /// Whole picture visible, scaled up or down; may leave borders.
    Fit,
    /// Screen completely covered, scaled and center-cropped.
    Fill,
};

constexpr const char* to_string(Scale s) { return s == Scale::Fit ? "Fit" : "Fill"; }

/// Couldn't open or decode a picture.
class PictureError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// A decoded picture: 8 bits per channel, RGB (3 channels) or RGBA (4).
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int channels = 3;
    /// `width * height * channels` bytes, row by row
    std::vector<std::uint8_t> pixels;

    bool has_alpha() const { return channels == 4; }
};

/// RGB pixels ready for `TftDisplay::draw_image`.
struct Picture {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    /// `width * height` pixels, 3 bytes each (R, G, B)
    std::vector<std::uint8_t> rgb;

    friend bool operator==(const Picture&, const Picture&) = default;
};

/// Opens a JPEG or PNG (format detected from the contents) and applies its
/// EXIF orientation, so phone photos come out the right way up. Throws
/// `PictureError` for missing, unreadable or truncated files.
Image open(const std::filesystem::path& path);

/// Decodes a JPEG or PNG held in memory, as `open` does.
Image decode(const std::vector<std::uint8_t>& bytes);

/// Scales `image` to a `width` x `height` screen. Keeps the aspect ratio.
Picture scale(const Image& image, std::uint16_t width, std::uint16_t height, Scale mode);

struct Crop {
    std::uint32_t x, y, width, height;
    friend bool operator==(const Crop&, const Crop&) = default;
};

/// The largest centered part of an `image_w` x `image_h` picture with the
/// same shape as a `w` x `h` screen.
Crop center_crop(std::uint32_t image_w, std::uint32_t image_h, std::uint32_t w, std::uint32_t h);

/// Whether a picture is taller than it is wide (after EXIF orientation).
inline bool is_portrait(const Image& image) { return image.height > image.width; }

/// Where and how `show` put a picture on the screen.
struct Shown {
    tft::Rotate rotation;
    /// Top-left corner of the scaled picture (centered on the screen)
    std::int32_t x;
    std::int32_t y;
    std::uint16_t width;
    std::uint16_t height;
    std::chrono::nanoseconds scale_time;
    std::chrono::nanoseconds draw_time;
};

/// Shows `image` centered on the screen: portrait rotation for portrait
/// pictures, landscape otherwise. With `Scale::Fit` the rest of the screen
/// is cleared to black.
Shown show(tft::TftDisplay& display, const Image& image, Scale mode);

}  // namespace picture
