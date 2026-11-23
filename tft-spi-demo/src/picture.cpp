#include "picture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "stb_image.h"

namespace picture {

using namespace tft;

namespace {

// ---- EXIF orientation ----

std::uint16_t read16(const std::uint8_t* p, bool big_endian) {
    return big_endian ? std::uint16_t((p[0] << 8) | p[1]) : std::uint16_t((p[1] << 8) | p[0]);
}

std::uint32_t read32(const std::uint8_t* p, bool big_endian) {
    return big_endian ? (std::uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]
                      : (std::uint32_t(p[3]) << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
}

/// The Orientation tag (1..=8) from a TIFF structure (the body of an EXIF
/// block), or 1 if there isn't a valid one.
int tiff_orientation(const std::uint8_t* tiff, std::size_t size) {
    if (size < 8) return 1;
    bool big_endian;
    if (tiff[0] == 'M' && tiff[1] == 'M') {
        big_endian = true;
    } else if (tiff[0] == 'I' && tiff[1] == 'I') {
        big_endian = false;
    } else {
        return 1;
    }
    if (read16(tiff + 2, big_endian) != 42) return 1;
    const std::uint32_t ifd = read32(tiff + 4, big_endian);
    if (ifd > size || size - ifd < 2) return 1;
    const std::uint16_t entries = read16(tiff + ifd, big_endian);
    for (std::uint32_t i = 0; i < entries; ++i) {
        const std::size_t entry = ifd + 2 + std::size_t{i} * 12;
        if (entry + 12 > size) return 1;
        const std::uint16_t tag = read16(tiff + entry, big_endian);
        const std::uint16_t type = read16(tiff + entry + 2, big_endian);
        if (tag == 0x0112 && type == 3 /* SHORT */) {
            const int value = read16(tiff + entry + 8, big_endian);
            return value >= 1 && value <= 8 ? value : 1;
        }
    }
    return 1;
}

/// Walks a JPEG's segments for an EXIF orientation and checks the file
/// isn't cut short (the image data must be followed by an end-of-image
/// marker). Throws `PictureError` for truncated files.
int jpeg_orientation(const std::vector<std::uint8_t>& b) {
    int orientation = 1;
    std::size_t i = 2;  // after SOI
    for (;;) {
        while (i < b.size() && b[i] == 0xFF && i + 1 < b.size() && b[i + 1] == 0xFF) ++i;  // fill bytes
        if (i + 1 >= b.size() || b[i] != 0xFF) {
            throw PictureError("truncated or corrupt JPEG");
        }
        const std::uint8_t marker = b[i + 1];
        i += 2;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;  // no length
        }
        if (marker == 0xD9) {
            throw PictureError("JPEG has no image data");
        }
        if (i + 2 > b.size()) {
            throw PictureError("truncated JPEG");
        }
        const std::size_t length = (std::size_t{b[i]} << 8) | b[i + 1];
        if (length < 2 || i + length > b.size()) {
            throw PictureError("truncated JPEG");
        }
        if (marker == 0xE1 && length >= 8 && std::memcmp(&b[i + 2], "Exif\0\0", 6) == 0) {
            orientation = tiff_orientation(&b[i + 8], length - 8);
        }
        if (marker == 0xDA) {
            // Start of scan: entropy-coded data follows, in which 0xFF is always
            // stuffed as FF 00, so FF D9 only appears as the real end marker.
            for (std::size_t k = i + length; k + 1 < b.size(); ++k) {
                if (b[k] == 0xFF && b[k + 1] == 0xD9) {
                    return orientation;
                }
            }
            throw PictureError("truncated JPEG (no end-of-image marker)");
        }
        i += length;
    }
}

/// EXIF orientation from a PNG's eXIf chunk, or 1.
int png_orientation(const std::vector<std::uint8_t>& b) {
    std::size_t i = 8;  // after the signature
    while (i + 12 <= b.size()) {
        const std::uint32_t length = read32(&b[i], true);
        if (length > b.size() - i - 12) break;
        if (std::memcmp(&b[i + 4], "eXIf", 4) == 0) {
            return tiff_orientation(&b[i + 8], length);
        }
        if (std::memcmp(&b[i + 4], "IDAT", 4) == 0 || std::memcmp(&b[i + 4], "IEND", 4) == 0) break;
        i += 12 + length;
    }
    return 1;
}

/// Turns/flips `image` as EXIF orientation `o` says, so it's the right way up.
void apply_orientation(Image& image, int o) {
    if (o <= 1 || o > 8) return;
    const std::uint32_t w = image.width, h = image.height;
    const bool swap = o >= 5;
    const std::uint32_t ow = swap ? h : w, oh = swap ? w : h;
    const int ch = image.channels;
    std::vector<std::uint8_t> out(image.pixels.size());
    for (std::uint32_t oy = 0; oy < oh; ++oy) {
        for (std::uint32_t ox = 0; ox < ow; ++ox) {
            std::uint32_t sx = 0, sy = 0;
            switch (o) {
                case 2: sx = w - 1 - ox, sy = oy; break;           // mirror horizontally
                case 3: sx = w - 1 - ox, sy = h - 1 - oy; break;   // turn 180
                case 4: sx = ox, sy = h - 1 - oy; break;           // mirror vertically
                case 5: sx = oy, sy = ox; break;                   // transpose
                case 6: sx = oy, sy = h - 1 - ox; break;           // turn 90 clockwise
                case 7: sx = w - 1 - oy, sy = h - 1 - ox; break;   // transverse
                case 8: sx = w - 1 - oy, sy = ox; break;           // turn 90 counter-clockwise
            }
            std::memcpy(&out[(std::size_t{oy} * ow + ox) * ch], &image.pixels[(std::size_t{sy} * w + sx) * ch], ch);
        }
    }
    image.pixels = std::move(out);
    image.width = ow;
    image.height = oh;
}

// ---- scaling ----

/// Triangle (bilinear) resampling weights for one output position, as the
/// Rust `image` crate computes them.
struct Taps {
    std::size_t left;
    std::vector<float> weights;
};

std::vector<Taps> taps(std::uint32_t in_len, std::uint32_t out_len) {
    const float ratio = float(in_len) / float(out_len);
    const float sratio = std::max(ratio, 1.0f);
    const float support = sratio;  // triangle support 1.0, scaled
    std::vector<Taps> result(out_len);
    for (std::uint32_t o = 0; o < out_len; ++o) {
        const float center = (float(o) + 0.5f) * ratio;
        const auto left = std::clamp<std::int64_t>(std::int64_t(std::floor(center - support)), 0, in_len - 1);
        const auto right =
            std::clamp<std::int64_t>(std::int64_t(std::ceil(center + support)), left + 1, std::int64_t{in_len});
        Taps& t = result[o];
        t.left = std::size_t(left);
        float sum = 0;
        for (std::int64_t i = left; i < right; ++i) {
            const float x = std::abs((float(i) - (center - 0.5f)) / sratio);
            const float w = x < 1.0f ? 1.0f - x : 0.0f;
            t.weights.push_back(w);
            sum += w;
        }
        for (float& w : t.weights) w /= sum;
    }
    return result;
}

/// Resizes to exactly `w` x `h` (aspect ratio not kept).
Image resize_exact(const Image& in, std::uint32_t w, std::uint32_t h) {
    if (in.width == w && in.height == h) return in;
    const int ch = in.channels;

    // Horizontal pass into floats, then vertical
    const auto htaps = taps(in.width, w);
    std::vector<float> mid(std::size_t{w} * in.height * ch);
    for (std::uint32_t y = 0; y < in.height; ++y) {
        const std::uint8_t* row = &in.pixels[std::size_t{y} * in.width * ch];
        float* out = &mid[std::size_t{y} * w * ch];
        for (std::uint32_t x = 0; x < w; ++x) {
            const Taps& t = htaps[x];
            for (int c = 0; c < ch; ++c) {
                float acc = 0;
                for (std::size_t k = 0; k < t.weights.size(); ++k) {
                    acc += t.weights[k] * row[(t.left + k) * ch + c];
                }
                out[std::size_t{x} * ch + c] = acc;
            }
        }
    }

    const auto vtaps = taps(in.height, h);
    Image out{w, h, ch, std::vector<std::uint8_t>(std::size_t{w} * h * ch)};
    const std::size_t stride = std::size_t{w} * ch;
    for (std::uint32_t y = 0; y < h; ++y) {
        const Taps& t = vtaps[y];
        for (std::size_t i = 0; i < stride; ++i) {
            float acc = 0;
            for (std::size_t k = 0; k < t.weights.size(); ++k) {
                acc += t.weights[k] * mid[(t.left + k) * stride + i];
            }
            out.pixels[y * stride + i] = static_cast<std::uint8_t>(std::clamp(std::round(acc), 0.0f, 255.0f));
        }
    }
    return out;
}

Image crop(const Image& in, const Crop& c) {
    Image out{c.width, c.height, in.channels, std::vector<std::uint8_t>(std::size_t{c.width} * c.height * in.channels)};
    const std::size_t row_bytes = std::size_t{c.width} * in.channels;
    for (std::uint32_t y = 0; y < c.height; ++y) {
        std::memcpy(&out.pixels[y * row_bytes], &in.pixels[((std::size_t{c.y} + y) * in.width + c.x) * in.channels],
                    row_bytes);
    }
    return out;
}

/// Blends a picture with transparency over a black background. (Just dropping
/// the alpha channel would show whatever color is stored under transparent
/// pixels.)
std::vector<std::uint8_t> to_rgb(const Image& image) {
    if (!image.has_alpha()) return image.pixels;
    std::vector<std::uint8_t> rgb(std::size_t{image.width} * image.height * 3);
    for (std::size_t i = 0, o = 0; o < rgb.size(); i += 4, o += 3) {
        const unsigned alpha = image.pixels[i + 3];
        for (int c = 0; c < 3; ++c) {
            rgb[o + c] = static_cast<std::uint8_t>((image.pixels[i + c] * alpha + 127) / 255);
        }
    }
    return rgb;
}

}  // namespace

Image decode(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > 0x7FFF'FFFF) {
        throw PictureError("file too large");
    }

    int orientation = 1;
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        orientation = jpeg_orientation(bytes);
    } else if (bytes.size() >= 8 && std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) == 0) {
        orientation = png_orientation(bytes);
    }

    int w = 0, h = 0, n = 0;
    std::unique_ptr<stbi_uc, void (*)(void*)> data(
        stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, 0), stbi_image_free);
    if (!data) {
        throw PictureError(std::string("can't decode image: ") + stbi_failure_reason());
    }

    // Gray -> RGB, gray + alpha -> RGBA
    Image image{std::uint32_t(w), std::uint32_t(h), (n == 2 || n == 4) ? 4 : 3, {}};
    const std::size_t count = std::size_t(w) * h;
    image.pixels.resize(count * image.channels);
    for (std::size_t p = 0; p < count; ++p) {
        const stbi_uc* src = data.get() + p * n;
        std::uint8_t* dst = &image.pixels[p * image.channels];
        if (n <= 2) {
            dst[0] = dst[1] = dst[2] = src[0];
        } else {
            dst[0] = src[0], dst[1] = src[1], dst[2] = src[2];
        }
        if (image.channels == 4) {
            dst[3] = src[n - 1];
        }
    }

    apply_orientation(image, orientation);
    return image;
}

Image open(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw PictureError("can't open file");
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return decode(bytes);
}

Crop center_crop(std::uint32_t image_w, std::uint32_t image_h, std::uint32_t w32, std::uint32_t h32) {
    const std::uint64_t iw = image_w, ih = image_h, w = w32, h = h32;
    std::uint64_t crop_w, crop_h;
    if (iw * h > ih * w) {
        // Wider than the screen: use the full height, cut the sides
        crop_w = std::max<std::uint64_t>(ih * w / h, 1);
        crop_h = ih;
    } else {
        // Taller than the screen: use the full width, cut top and bottom
        crop_w = iw;
        crop_h = std::max<std::uint64_t>(iw * h / w, 1);
    }
    // All values are at most the image's own uint32 size
    return {std::uint32_t((iw - crop_w) / 2), std::uint32_t((ih - crop_h) / 2), std::uint32_t(crop_w),
            std::uint32_t(crop_h)};
}

Picture scale(const Image& image, std::uint16_t width, std::uint16_t height, Scale mode) {
    Image scaled;
    if (mode == Scale::Fit) {
        const double ratio = std::min(double(width) / image.width, double(height) / image.height);
        const auto w = std::uint32_t(std::max(std::round(image.width * ratio), 1.0));
        const auto h = std::uint32_t(std::max(std::round(image.height * ratio), 1.0));
        scaled = resize_exact(image, std::min<std::uint32_t>(w, width), std::min<std::uint32_t>(h, height));
    } else {
        // Crop the source to the screen's shape first, then resize. (Resizing
        // first makes a huge intermediate image for very thin pictures: a
        // 5000x1 strip would become 1,600,000x320.)
        scaled = resize_exact(crop(image, center_crop(image.width, image.height, width, height)), width, height);
    }
    // Both are at most width/height, so they fit in uint16_t
    return {std::uint16_t(scaled.width), std::uint16_t(scaled.height), to_rgb(scaled)};
}

Shown show(TftDisplay& display, const Image& image, Scale mode) {
    using Clock = std::chrono::steady_clock;
    const Rotate rotation = is_portrait(image) ? Rotate::Degrees0 : Rotate::Degrees90;
    display.set_rotation(rotation);

    auto start = Clock::now();
    const Picture pic = scale(image, display.width(), display.height(), mode);
    const auto scale_time = Clock::now() - start;

    start = Clock::now();
    if (pic.width != display.width() || pic.height != display.height()) {
        display.fill_screen(Color::BLACK);
    }
    const std::int32_t x = (std::int32_t{display.width()} - pic.width) / 2;
    const std::int32_t y = (std::int32_t{display.height()} - pic.height) / 2;
    display.draw_image(x, y, pic.width, pic.height, pic.rgb);

    return {rotation, x, y, pic.width, pic.height, scale_time, Clock::now() - start};
}

}  // namespace picture
