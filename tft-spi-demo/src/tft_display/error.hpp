#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace tft {

/// Base class for everything the display driver throws.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Error from the SPI/GPIO layer (spidev or /dev/gpiomem on the Pi)
class SpiError : public Error {
public:
    explicit SpiError(const std::string& what) : Error("SPI Error: " + what) {}
};

/// A coordinate outside the screen
class SizeError : public Error {
public:
    SizeError(std::uint16_t given, std::uint16_t max)
        : Error("Given size: " + std::to_string(given) + ", Max size: " + std::to_string(max)),
          given(given),
          max(max) {}

    std::uint16_t given;
    std::uint16_t max;
};

/// Pixel buffer length doesn't match width * height * 3
class ImageSizeError : public Error {
public:
    ImageSizeError(std::size_t given, std::size_t expected)
        : Error("Image buffer is " + std::to_string(given) + " bytes, expected " + std::to_string(expected) +
                " (width * height * 3)"),
          given(given),
          expected(expected) {}

    std::size_t given;
    std::size_t expected;
};

}  // namespace tft
