# tft-spi-demo (C++)

C++20 driver and demo for a Waveshare 3.5" RPi LCD (A) touch display on a
Raspberry Pi 4: shapes, text, touch input with calibration, and a JPEG/PNG
viewer. A port of the Rust `tft-spi-demo`, with the same behavior,
settings and tests.

## Display specs

| | |
|---|---|
| Board | Waveshare 3.5" RPi LCD (A), plugs onto the 40-pin header |
| LCD controller | ILI9486 |
| Resolution | 320 × 480 native (portrait), 480 × 320 in landscape |
| Interface | SPI into shift registers that feed the ILI9486's 16-bit parallel bus: every command, parameter and pixel is a 16-bit word. DC low = command, DC high = parameter/pixel |
| Color | 16-bit RGB565, 2 bytes per pixel |
| Color order | Panel is wired BGR; set with the MADCTL BGR bit |
| Touch controller | XPT2046, resistive, 12-bit readings (0–4095) |
| Touch interface | SPI on its own chip select (CE1), pen-down interrupt on TP_IRQ |

The driver sends 16-bit words as Waveshare's own driver does: commands as
`[0x00, cmd]`, each parameter as `[0x00, p]`, and RGB565 pixels. Single bytes
(as for a plain SPI ILI9486) get paired with their neighbors by the board's
shift registers, so rotation, address windows and text break. `TFT_BUS=8`
switches to the 8-bit way for comparison.

## Pinout

| Signal | GPIO | Physical pin | Used for |
|---|---|---|---|
| SPI0 MOSI | 10 | 19 | Data to LCD and touch controller |
| SPI0 MISO | 9 | 21 | Data from touch controller |
| SPI0 SCLK | 11 | 23 | SPI clock for both |
| SPI0 CE0 (LCD_CS) | 8 | 24 | LCD chip select, active low (driven as a GPIO) |
| SPI0 CE1 (TP_CS) | 7 | 26 | Touch chip select, active low |
| LCD_RS (DC) | 24 | 18 | Low = command, high = data |
| RST | 25 | 22 | LCD reset, active low |
| TP_IRQ | 17 | 11 | Low while the panel is touched |

The display (`/dev/spidev0.0`) and touch controller (`/dev/spidev0.1`) are
opened as separate spidev devices, each with its own clock speed. The LCD's
chip select (GPIO 8) is driven as a plain GPIO and held low: CS going high
resets the board's 16-bit word counter. Touch reads release it while they use
CE1.

GPIO goes through `/dev/gpiomem` (the registers mapped into the process, as
the Rust `rppal` crate does), not libgpiod. With SPI enabled the kernel's SPI
driver owns GPIO 8, so libgpiod can't request it; switching the pin to an
output in the registers takes it over, and on exit it's put back to CE0. DC
and RST stay driven after exit, so the picture stays on screen (RST has a
pull-down and would otherwise hold the LCD in reset). Pi 4 and earlier only:
the Pi 5's RP1 GPIO is different.

## Settings used by this driver

| | |
|---|---|
| Display SPI clock | 16 MHz requested, about 15.6 MHz actual on a Pi 4. At 31.2 MHz solid colors and text look fine but photo colors are badly off. Override with `TFT_SPI_HZ` |
| Touch SPI clock | 2 MHz (XPT2046 maximum is about 2.5 MHz) |
| SPI mode | Mode 0 |
| Pixel format (0x3A) | `0x55`: 16 bits per pixel (`0x66`, 18 bits, with `TFT_BUS=8`) |
| Transfers | Whole 16-bit words only. One row of pixels per SPI transfer (at most 480 × 2 = 960 bytes) |
| Gamma | The panel's defaults. `TFT_GAMMA=full` or `analog` sends tft-spi-demo's tables instead |
| Touch commands | `0xD0` read X, `0x90` read Y (12-bit, differential) |

Rotation is set with MADCTL (0x36), always with the BGR bit (`0x08`):

| Rotation | MADCTL bits | Value | Size |
|---|---|---|---|
| `Degrees0` | MY | `0x88` | 320 × 480 |
| `Degrees90` | MV | `0x28` | 480 × 320 |
| `Degrees180` | MX | `0x48` | 320 × 480 |
| `Degrees270` | MX, MY, MV | `0xE8` | 480 × 320 |

## Raspberry Pi setup

- Enable SPI: `dtparam=spi=on` in `/boot/firmware/config.txt`, or with `raspi-config`.
- Don't load the Waveshare `waveshare35a` overlay or LCD-show driver; they
  take over the display.
- Run as a user in the `spi` and `gpio` groups, or with `sudo`.
- Build tools: `sudo apt install cmake ninja-build g++`. No other libraries
  are needed (JPEG/PNG decoding uses the bundled `stb_image`).

## Building and running

On the Pi (or from Visual Studio with the `pi4-debug-remote` /
`pi4-release-remote` presets, which copy the sources over SSH and build there):

```
cmake --preset pi4-release-remote
cmake --build --preset pi4-release-remote
cd out/build/pi4-release-remote/bin

./tft_spi_demo                      # demo: rotations, shapes, text, touch drawing
./tft_spi_demo --calibrate          # redo the touch calibration first
./tft_spi_demo photo.jpg            # show a picture (fit, with borders)
./tft_spi_demo --fill photo.jpg     # show a picture filling the screen
./tft_spi_demo --colors             # color gradients, to judge the gamma
```

The touch calibration is saved to `touch_calibration.txt` in the current
directory. Ctrl-C quits the demo.

`pi4-debug` / `pi4-release` cross-compile from Windows with
`cmake/aarch64-rpi-toolchain.cmake` (set `RPI_TOOLCHAIN_ROOT` and
`RPI_SYSROOT` in `CMakeUserPresets.json`). The toolchain's glibc must not be
newer than the Pi's: Arm GNU 15.2's libstdc++ needs glibc 2.38, Raspberry Pi
OS Bookworm has 2.36, so use an older toolchain (or the Docker setup from
`rpi-blink-docker`) for that route.

## Tests

The tests (`tft_tests`) run on any Linux machine, including WSL and the Pi:

```
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests                       # or run out/build/host-tests/bin/tft_tests
out/build/host-tests/bin/tft_tests calibrate    # only tests whose names contain "calibrate"
```

85 tests, about 1.5 s in a Release build. They use a tiny built-in framework
(`tests/check.hpp`), so nothing is downloaded. What a test prints is only
shown if it fails.

Display tests use a mock ILI9486 (`tests/mock_spi.cpp`) that decodes the
commands the driver sends and paints the pixels into a framebuffer. It also
models the panel's physical memory, applying the MADCTL rotation bits, and
logs every command, so tests can check exactly what would be on screen.

| Area | Tests |
|---|---|
| `fill_rectangle`, `draw_text`, `draw_image` | Every pixel on screen, clipping at all edges, nothing sent when off screen, one transfer per row, UTF-8 text |
| Lines, circles, rectangles, rounded rectangles | Pixel-for-pixel against plain per-pixel versions of the algorithms; clipped at every edge; no overflow at extreme coordinates |
| Rotations | MADCTL values verified on the hardware are pinned; each rotation covers the whole panel once; the four are consistent quarter turns with no mirroring |
| Initialization | The known-working command sequence, gamma options, 18-bit pixel format, reset and ILI9486 timing |
| 16-bit bus | Commands and parameters as words, RGB565 pixels |
| Touch | Filtering a burst of readings (median, dropping stuck values); the calibration math; the calibration screens with scripted touches (target positions, retries, saving/loading the file, per rotation) |
| Pictures | JPEG/PNG loading, EXIF orientation, bad and truncated files, transparency, Fit/Fill scaling and cropping, the viewer's centering and borders |
| Demo screen | Button layout (rounded, gaps, centered labels), pressed state, which button a touch hits |

Not covered (needs the hardware): the `/dev/gpiomem` and spidev calls,
reading the XPT2046 touch controller, and the demo's touch-drawing loop.

## Layout

| Path | |
|---|---|
| `src/tft_display/` | ILI9486 driver (`TftDisplay`), colors, font, shape rasterizing |
| `src/tft_spi.*` | `TftSpi` interface and the 16-bit bus translation |
| `src/tft_touch/` | Touch input interface, median filter, calibration math |
| `src/rpi/` | Pi only: `/dev/gpiomem` GPIO, spidev, `TftSpiImpl`, XPT2046 `TftTouch` |
| `src/calibrate.*`, `src/ui.*`, `src/picture.*`, `src/app.*` | Calibration screens, demo UI, picture loading/scaling, the demo |
| `tests/` | Mock panel and tests |
| `third_party/stb/` | `stb_image` / `stb_image_write` (public domain / MIT), pinned to the commit in `COMMIT` |
