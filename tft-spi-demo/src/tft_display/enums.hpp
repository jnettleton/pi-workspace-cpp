#pragma once

#include <cstdint>
#include <optional>
#include <ostream>

namespace tft {

/// ILI9486 commands
enum class Command : std::uint8_t {
    // NoOp = 0x00,
    SoftReset = 0x01,
    // SleepIn = 0x10,
    SleepOut = 0x11,
    NormalDisplayModeOn = 0x13,
    // DisplayInversionOff = 0x20,
    // DisplayInversionOn = 0x21,
    // DisplayOff = 0x28,
    DisplayOn = 0x29,
    ColumnAddressSet = 0x2A,
    RowAddressSet = 0x2B,
    MemoryWrite = 0x2C,
    MemoryAccessControl = 0x36,
    InterfacePixelFormat = 0x3A,

    InterfaceModeControl = 0xB0,
    FrameRateControlNormal = 0xB1,   // normal, full colors
    FrameRateControlIdle = 0xB2,     // idle, 8 colors
    FrameRateControlPartial = 0xB3,  // partial, full colors
    PowerControl3 = 0xC2,
    VcomControl1 = 0xC5,

    PositiveGammaControl = 0xE0,
    NegativeGammaControl = 0xE1,
    DigitalGammaControl1 = 0xE2,
};

/// The command's byte value.
constexpr std::uint8_t code(Command cmd) { return static_cast<std::uint8_t>(cmd); }

/// MADCTL (0x36) bits
namespace madctl {
inline constexpr std::uint8_t MY = 0x80;   // row address order
inline constexpr std::uint8_t MX = 0x40;   // column address order
inline constexpr std::uint8_t MV = 0x20;   // row/column exchange
inline constexpr std::uint8_t BGR = 0x08;  // panel is wired BGR
}  // namespace madctl

enum class Rotate {
    /// Portrait, 320 x 480
    Degrees0,
    /// Landscape, 480 x 320
    Degrees90,
    /// Portrait (flipped), 320 x 480
    Degrees180,
    /// Landscape (flipped), 480 x 320
    Degrees270,
};

constexpr std::uint16_t degrees(Rotate rotation) {
    switch (rotation) {
        case Rotate::Degrees0: return 0;
        case Rotate::Degrees90: return 90;
        case Rotate::Degrees180: return 180;
        case Rotate::Degrees270: return 270;
    }
    return 0;
}

constexpr std::optional<Rotate> rotate_from_degrees(std::uint16_t value) {
    switch (value) {
        case 0: return Rotate::Degrees0;
        case 90: return Rotate::Degrees90;
        case 180: return Rotate::Degrees180;
        case 270: return Rotate::Degrees270;
        default: return std::nullopt;
    }
}

constexpr const char* to_string(Rotate rotation) {
    switch (rotation) {
        case Rotate::Degrees0: return "Degrees0";
        case Rotate::Degrees90: return "Degrees90";
        case Rotate::Degrees180: return "Degrees180";
        case Rotate::Degrees270: return "Degrees270";
    }
    return "?";
}

inline std::ostream& operator<<(std::ostream& os, Rotate rotation) { return os << to_string(rotation); }

}  // namespace tft
