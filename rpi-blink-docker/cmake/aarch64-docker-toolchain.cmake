# Toolchain file used INSIDE the cross-compile container.
# Debian's multi-arch layout puts arm64 libs on the cross compiler's default
# search paths, so this file is intentionally tiny -- no sysroot, no env vars.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Pi 4  -> cortex-a72
# Pi 5  -> cortex-a76
# Pi Zero 2 W -> cortex-a53
add_compile_options(-mcpu=cortex-a72)
