#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

#include "check.hpp"
#include "helpers.hpp"
#include "picture.hpp"
#include "stb_image_write.h"

using namespace tft;
using namespace helpers;
using namespace picture;

namespace {

using Bytes = std::vector<std::uint8_t>;

Image make_image(std::uint32_t w, std::uint32_t h, int channels,
                 const std::function<std::vector<std::uint8_t>(std::uint32_t, std::uint32_t)>& pixel) {
    Image image{w, h, channels, {}};
    image.pixels.reserve(std::size_t{w} * h * channels);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto px = pixel(x, y);
            image.pixels.insert(image.pixels.end(), px.begin(), px.end());
        }
    }
    return image;
}

Image test_image(std::uint32_t w, std::uint32_t h) {
    return make_image(w, h, 3, [](auto x, auto y) { return Bytes{std::uint8_t(x), std::uint8_t(y), 128}; });
}

void append(void* context, void* data, int size) {
    auto* out = static_cast<Bytes*>(context);
    const auto* p = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}

Bytes encode_jpeg(const Image& image) {
    Bytes out;
    stbi_write_jpg_to_func(append, &out, int(image.width), int(image.height), image.channels, image.pixels.data(), 90);
    return out;
}

Bytes encode_png(const Image& image) {
    Bytes out;
    stbi_write_png_to_func(append, &out, int(image.width), int(image.height), image.channels, image.pixels.data(),
                           int(image.width) * image.channels);
    return out;
}

void write_file(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

void put16(Bytes& b, std::uint16_t v) { b.push_back(std::uint8_t(v >> 8)), b.push_back(std::uint8_t(v)); }
void put32(Bytes& b, std::uint32_t v) { put16(b, std::uint16_t(v >> 16)), put16(b, std::uint16_t(v)); }

/// A JPEG of `image` with an EXIF block holding just an Orientation tag.
Bytes jpeg_with_orientation(const Image& image, std::uint16_t orientation) {
    const Bytes jpeg = encode_jpeg(image);
    CHECK(jpeg.size() > 2 && jpeg[0] == 0xFF && jpeg[1] == 0xD8, "JPEG starts with SOI");

    // TIFF structure (big-endian): header, one IFD with one entry, no next IFD
    Bytes tiff{'M', 'M', 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08};
    put16(tiff, 1);       // entry count
    put16(tiff, 0x0112);  // tag: Orientation
    put16(tiff, 3);       // type: SHORT
    put32(tiff, 1);       // count
    put16(tiff, orientation);
    put16(tiff, 0);  // value padding
    put32(tiff, 0);  // no next IFD

    // APP1 segment right after SOI; its length counts itself but not the marker
    Bytes app1{0xFF, 0xE1};
    put16(app1, std::uint16_t(2 + 6 + tiff.size()));
    app1.insert(app1.end(), {'E', 'x', 'i', 'f', 0, 0});
    app1.insert(app1.end(), tiff.begin(), tiff.end());

    Bytes out(jpeg.begin(), jpeg.begin() + 2);
    out.insert(out.end(), app1.begin(), app1.end());
    out.insert(out.end(), jpeg.begin() + 2, jpeg.end());
    return out;
}

const std::uint8_t* at(const Image& image, std::uint32_t x, std::uint32_t y) {
    return &image.pixels[(std::size_t{y} * image.width + x) * image.channels];
}

}  // namespace

TEST(picture_fit_keeps_aspect_ratio) {
    // 4:3 photo on a 480x320 screen: limited by height
    auto p = scale(test_image(4000, 3000), 480, 320, Scale::Fit);
    CHECK_EQ(p.width, 427);
    CHECK_EQ(p.height, 320);
    CHECK_EQ(p.rgb.size(), 427u * 320u * 3u);

    // Small images are scaled up
    p = scale(test_image(48, 32), 480, 320, Scale::Fit);
    CHECK_EQ((std::pair{p.width, p.height}), (std::pair<std::uint16_t, std::uint16_t>{480, 320}));

    // Portrait image on a landscape screen
    p = scale(test_image(300, 600), 480, 320, Scale::Fit);
    CHECK_EQ((std::pair{p.width, p.height}), (std::pair<std::uint16_t, std::uint16_t>{160, 320}));
}

TEST(picture_fill_covers_screen) {
    for (const auto& [w, h] : {std::pair{4000u, 3000u}, {300u, 600u}, {48u, 32u}, {1000u, 1000u}}) {
        const auto p = scale(test_image(w, h), 480, 320, Scale::Fill);
        CHECK_EQ((std::pair{p.width, p.height}), (std::pair<std::uint16_t, std::uint16_t>{480, 320}), w, "x", h);
    }
}

TEST(picture_opens_jpeg_and_png) {
    TempDir dir("picture-open");
    const Image source = test_image(64, 40);
    for (const std::string name : {"test.jpg", "test.png", "jpeg-without-extension"}) {
        const auto path = dir.path / name;
        write_file(path, name.find("png") != std::string::npos ? encode_png(source) : encode_jpeg(source));
        const Image image = open(path);
        CHECK_EQ(image.width, 64u, name);
        CHECK_EQ(image.height, 40u, name);
    }
}

/// Phone cameras store the sensor's pixels and an EXIF Orientation saying
/// how to turn them; `open` should return the picture the right way up.
TEST(picture_applies_exif_orientation) {
    // 64x32, red in the top-left 32x16 quarter, dark elsewhere
    const Image stored = make_image(64, 32, 3, [](auto x, auto y) {
        return x < 32 && y < 16 ? Bytes{230, 20, 20} : Bytes{20, 20, 20};
    });
    TempDir dir("picture-exif");

    // (orientation, size after turning, where the red quarter ends up)
    const std::vector<std::tuple<std::uint16_t, std::uint32_t, std::uint32_t, std::string>> cases{
        {1, 64, 32, "top-left"},      // as stored
        {3, 64, 32, "bottom-right"},  // turn 180
        {6, 32, 64, "top-right"},     // turn 90 clockwise
        {8, 32, 64, "bottom-left"},   // turn 90 counter-clockwise
    };
    for (const auto& [orientation, w, h, red_corner] : cases) {
        const auto path = dir.path / ("orientation-" + std::to_string(orientation) + ".jpg");
        write_file(path, jpeg_with_orientation(stored, orientation));
        const Image image = open(path);
        CHECK_EQ(image.width, w, "orientation ", orientation);
        CHECK_EQ(image.height, h, "orientation ", orientation);

        // Sample the middle of each quarter (JPEG is lossy, so just check which is red)
        const std::vector<std::tuple<std::string, std::uint32_t, std::uint32_t>> quarters{
            {"top-left", w / 4, h / 4},
            {"top-right", 3 * w / 4, h / 4},
            {"bottom-left", w / 4, 3 * h / 4},
            {"bottom-right", 3 * w / 4, 3 * h / 4},
        };
        for (const auto& [name, x, y] : quarters) {
            CHECK_EQ(at(image, x, y)[0] > 150, name == red_corner, "orientation ", orientation, ": ", name,
                     " quarter");
        }
    }
}

TEST(picture_open_reports_missing_and_invalid_files) {
    TempDir dir("picture-error");
    CHECK_THROWS(PictureError, open(dir.path / "does-not-exist.jpg"));

    const auto not_an_image = dir.path / "not-an-image.jpg";
    const std::string text = "this is not a JPEG";
    write_file(not_an_image, Bytes(text.begin(), text.end()));
    CHECK_THROWS(PictureError, open(not_an_image));

    const auto truncated = dir.path / "truncated.jpg";
    const Bytes jpeg = jpeg_with_orientation(test_image(64, 40), 1);
    write_file(truncated, Bytes(jpeg.begin(), jpeg.begin() + std::ptrdiff_t(jpeg.size() / 2)));
    CHECK_THROWS(PictureError, open(truncated));
}

TEST(picture_center_crop_matches_screen_shape) {
    // Wider than 3:2: sides cut
    CHECK_EQ(center_crop(960, 320, 480, 320), (Crop{240, 0, 480, 320}));
    CHECK_EQ(center_crop(4000, 2000, 480, 320), (Crop{500, 0, 3000, 2000}));
    // Taller than 3:2: top and bottom cut
    CHECK_EQ(center_crop(4000, 3000, 480, 320), (Crop{0, 167, 4000, 2666}));
    CHECK_EQ(center_crop(300, 600, 320, 480), (Crop{0, 75, 300, 450}));
    // Same shape: nothing cut
    CHECK_EQ(center_crop(960, 640, 480, 320), (Crop{0, 0, 960, 640}));
    // Thin strips keep at least one pixel
    CHECK_EQ(center_crop(5000, 1, 480, 320), (Crop{2499, 0, 1, 1}));
    CHECK_EQ(center_crop(1, 5000, 480, 320), (Crop{0, 2499, 1, 1}));
}

/// Transparent parts of a picture are shown over black, not as whatever
/// color happens to be stored under the transparency.
TEST(picture_transparency_shows_black) {
    // 3 pixels: transparent red, half-transparent white, opaque blue
    const Image image = make_image(3, 1, 4, [](auto x, auto) {
        return x == 0 ? Bytes{255, 0, 0, 0} : x == 1 ? Bytes{255, 255, 255, 128} : Bytes{0, 0, 255, 255};
    });
    const auto p = scale(image, 3, 1, Scale::Fit);
    CHECK_EQ(Bytes(p.rgb.begin(), p.rgb.begin() + 3), (Bytes{0, 0, 0}), "fully transparent");
    for (int i = 3; i < 6; ++i) CHECK(p.rgb[i] >= 127 && p.rgb[i] <= 129, "half transparent: ", int(p.rgb[i]));
    CHECK_EQ(Bytes(p.rgb.begin() + 6, p.rgb.end()), (Bytes{0, 0, 255}), "opaque");
}

/// A PNG with transparency loads as RGBA and is blended over black.
TEST(picture_png_with_alpha) {
    TempDir dir("picture-alpha");
    const Image source = make_image(4, 2, 4, [](auto x, auto) {
        return x < 2 ? Bytes{255, 255, 255, 0} : Bytes{255, 0, 0, 255};
    });
    const auto path = dir.path / "alpha.png";
    write_file(path, encode_png(source));
    const Image image = open(path);
    CHECK(image.has_alpha());
    const auto p = scale(image, 4, 2, Scale::Fit);
    CHECK_EQ(Bytes(p.rgb.begin(), p.rgb.begin() + 3), (Bytes{0, 0, 0}));
    CHECK_EQ(Bytes(p.rgb.begin() + 6, p.rgb.begin() + 9), (Bytes{255, 0, 0}));
}

/// Very thin pictures still scale to at least 1x1 and stay on screen.
TEST(picture_extreme_aspect_ratios) {
    for (const auto& [w, h] : {std::pair{4000u, 3u}, {3u, 4000u}, {1u, 1u}, {5000u, 1u}}) {
        for (const Scale mode : {Scale::Fit, Scale::Fill}) {
            const auto p = scale(test_image(w, h), 480, 320, mode);
            CHECK(p.width >= 1 && p.width <= 480 && p.height >= 1 && p.height <= 320, w, "x", h, " ",
                  to_string(mode), ": ", p.width, "x", p.height);
            CHECK_EQ(p.rgb.size(), std::size_t{p.width} * p.height * 3);
        }
    }
}

TEST(picture_portrait_detection) {
    CHECK(is_portrait(test_image(30, 40)));
    CHECK(!is_portrait(test_image(40, 30)));
    CHECK(!is_portrait(test_image(40, 40)));
}

// ---- show() against the mock panel ----

namespace {

const Bytes COLOR{200, 100, 52};

/// A solid-color image: it stays exactly this color when scaled, so every
/// on-screen pixel can be checked.
Image solid(std::uint32_t w, std::uint32_t h) {
    return make_image(w, h, 3, [](auto, auto) { return COLOR; });
}

/// Shows `image` on a mock panel of the logical size `show` should pick,
/// and checks the picture covers exactly the expected area with black around it.
void check_show(const Image& image, Scale mode, Rotate rotation, int x, int y, std::uint16_t w, std::uint16_t h) {
    Screen s(rotation);
    // Start from a screen full of something else, like a real panel
    s.display.fill_screen(Color::WHITE);

    const Shown shown = show(s.display, image, mode);
    const std::string name = std::to_string(image.width) + "x" + std::to_string(image.height) + " " + to_string(mode);
    CHECK_EQ(shown.rotation, rotation, name);
    CHECK_EQ(shown.x, x, name);
    CHECK_EQ(shown.y, y, name);
    CHECK_EQ(shown.width, w, name);
    CHECK_EQ(shown.height, h, name);

    const Rgb666 color = Color::rgb(COLOR[0], COLOR[1], COLOR[2]).to_rgb666_bytes();
    for (std::size_t py = 0; py < s.spi.height; ++py) {
        for (std::size_t px = 0; px < s.spi.width; ++px) {
            const bool in = int(px) >= x && int(px) < x + w && int(py) >= y && int(py) < y + h;
            if (s.spi.pixel(px, py) != (in ? color : BLACK)) {
                CHECK_EQ(s.spi.pixel(px, py), (in ? color : BLACK), name, ": pixel (", px, ", ", py, ")");
            }
        }
    }
}

}  // namespace

TEST(show_landscape_picture_fits_with_side_borders) {
    // 4:3 on 480x320: limited by height, centered horizontally
    check_show(solid(4000, 3000), Scale::Fit, Rotate::Degrees90, 26, 0, 427, 320);
}

TEST(show_wide_picture_fits_with_top_and_bottom_borders) {
    check_show(solid(1600, 600), Scale::Fit, Rotate::Degrees90, 0, 70, 480, 180);
}

TEST(show_portrait_picture_uses_portrait_rotation) {
    check_show(solid(300, 600), Scale::Fit, Rotate::Degrees0, 40, 0, 240, 480);
}

TEST(show_square_picture_is_shown_in_landscape) {
    check_show(solid(1000, 1000), Scale::Fit, Rotate::Degrees90, 80, 0, 320, 320);
}

TEST(show_small_picture_is_scaled_up) { check_show(solid(48, 32), Scale::Fit, Rotate::Degrees90, 0, 0, 480, 320); }

TEST(show_fill_covers_the_screen) {
    check_show(solid(4000, 3000), Scale::Fill, Rotate::Degrees90, 0, 0, 480, 320);
    check_show(solid(300, 600), Scale::Fill, Rotate::Degrees0, 0, 0, 320, 480);
}

/// Fill crops to the middle: the left and right edges of a wide picture are cut off.
TEST(show_fill_crops_to_the_center) {
    // 960x320: left third red, middle third green, right third blue
    const Image image = make_image(960, 320, 3, [](auto x, auto) {
        return x / 320 == 0 ? Bytes{255, 0, 0} : x / 320 == 1 ? Bytes{0, 255, 0} : Bytes{0, 0, 255};
    });
    mock::MockSpi spi(480, 320);
    TftDisplay display(spi);
    show(display, image, Scale::Fill);

    // Nothing to scale (already 320 high), so the screen shows picture
    // columns 240..720: the last quarter of red, all of green, and the
    // first quarter of blue; the outer 240 columns on each side are cut.
    CHECK_EQ(spi.pixel(40, 160), Color::RED.to_rgb666_bytes());
    CHECK_EQ(spi.pixel(240, 160), Color::GREEN.to_rgb666_bytes());
    CHECK_EQ(spi.pixel(440, 160), Color::BLUE.to_rgb666_bytes());
}

/// The sample photo in the repository decodes.
TEST(picture_sample_photo_decodes) {
    const auto path = std::filesystem::path(TFT_SOURCE_DIR) / "orange-profile.jpg";
    if (!std::filesystem::exists(path)) return;
    const Image image = open(path);
    CHECK(image.width > 0 && image.height > 0);
}
