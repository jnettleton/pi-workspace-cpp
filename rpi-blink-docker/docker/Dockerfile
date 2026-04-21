# Cross-compile environment for Raspberry Pi 4 (aarch64).
# Debian Bookworm matches the Raspberry Pi OS baseline (libstdc++/glibc versions).
FROM debian:bookworm

# Multi-arch is the magic that makes this work without a sysroot:
# arm64 libraries from apt land at /usr/lib/aarch64-linux-gnu, exactly
# where aarch64-linux-gnu-g++ already looks.
RUN dpkg --add-architecture arm64 \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        git \
        rsync \
        openssh-client \
        ca-certificates \
        gcc-aarch64-linux-gnu \
        g++-aarch64-linux-gnu \
        gdb-multiarch \
        libc6-dev-arm64-cross \
        libstdc++-12-dev-arm64-cross \
        libgpiod-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*

# pkg-config: search the arm64 .pc files, not the host amd64 ones.
ENV PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
ENV PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig

WORKDIR /src
