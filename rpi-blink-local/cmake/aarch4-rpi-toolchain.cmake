# aarch64-rpi-toolchain.cmake
#
# CMake toolchain file for cross-compiling from Windows (Visual Studio 2026)
# to 64-bit Raspberry Pi OS (Pi 4, aarch64).
#
# Requires two environment variables to be set BEFORE VS invokes CMake -- either
# as system variables, or (preferred) via CMakeUserPresets.json:
#
#   RPI_TOOLCHAIN_ROOT -> folder where you unpacked the Arm GNU toolchain,
#                         e.g. C:\Tools\arm-gnu-toolchain-15.2
#                         (the folder that contains 'bin\', etc.)
#
#   RPI_SYSROOT        -> local copy of the Pi's /usr + /lib, synced down via
#                         VS Connection Manager ("Update Headers") or rsync.
#                         e.g. C:\RpiSysroot\pi4

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# --- Toolchain root ----------------------------------------------------------
if (NOT DEFINED ENV{RPI_TOOLCHAIN_ROOT})
    message(FATAL_ERROR
        "RPI_TOOLCHAIN_ROOT environment variable is not set. "
        "Point it at the unpacked Arm GNU toolchain folder.")
endif()
set(_rpi_tc "$ENV{RPI_TOOLCHAIN_ROOT}")
file(TO_CMAKE_PATH "${_rpi_tc}" _rpi_tc)

set(_triple "aarch64-none-linux-gnu")

set(CMAKE_C_COMPILER   "${_rpi_tc}/bin/${_triple}-gcc.exe")
set(CMAKE_CXX_COMPILER "${_rpi_tc}/bin/${_triple}-g++.exe")
set(CMAKE_AR           "${_rpi_tc}/bin/${_triple}-ar.exe"      CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB       "${_rpi_tc}/bin/${_triple}-ranlib.exe"  CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP        "${_rpi_tc}/bin/${_triple}-strip.exe"   CACHE FILEPATH "" FORCE)

# --- Sysroot -----------------------------------------------------------------
if (NOT DEFINED ENV{RPI_SYSROOT})
    message(FATAL_ERROR
        "RPI_SYSROOT environment variable is not set. "
        "Point it at a local mirror of your Pi's /usr (+ /lib). "
        "Fastest way: in VS, Tools > Options > Cross Platform > Connection Manager, "
        "select your Pi, click 'Update Headers', then copy that folder here.")
endif()
set(CMAKE_SYSROOT "$ENV{RPI_SYSROOT}")
file(TO_CMAKE_PATH "${CMAKE_SYSROOT}" CMAKE_SYSROOT)

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# --- pkg-config --------------------------------------------------------------
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
    "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
unset(ENV{PKG_CONFIG_PATH})

# --- Compile/link flags ------------------------------------------------------
# Cortex-A72 = Pi 4. For Pi Zero 2 W use -mcpu=cortex-a53. For Pi 5 use cortex-a76.
add_compile_options(-mcpu=cortex-a72)
add_link_options(-Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu)
