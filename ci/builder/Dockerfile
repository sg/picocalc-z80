# Job image for the release workflow: Ubuntu 24.04 brought up to date, the
# same gcc-arm-none-eabi 13.2 as the dev machine, the Pico SDK with its
# submodules, and picotool installed where the SDK finds it, so a run spends
# its time building firmware rather than installing a toolchain.
FROM ubuntu:24.04

ARG PICO_SDK_VERSION=2.3.1
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -q \
 && apt-get upgrade -y -q \
 && apt-get install -y -q --no-install-recommends \
      build-essential cmake git python3 ca-certificates curl \
      gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
      pkg-config libusb-1.0-0-dev \
 && rm -rf /var/lib/apt/lists/*

RUN git clone --quiet --depth 1 --branch "$PICO_SDK_VERSION" \
      --recurse-submodules --shallow-submodules \
      https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk
ENV PICO_SDK_PATH=/opt/pico-sdk

# the SDK looks for this exact version before it falls back to building one
RUN git clone --quiet --depth 1 --branch "$PICO_SDK_VERSION" \
      https://github.com/raspberrypi/picotool.git /tmp/picotool \
 && cmake -S /tmp/picotool -B /tmp/picotool/build -DCMAKE_BUILD_TYPE=Release \
 && make -C /tmp/picotool/build -j"$(nproc)" install \
 && rm -rf /tmp/picotool

# checkouts in the job workspace belong to another uid
RUN git config --system --add safe.directory '*'

LABEL org.opencontainers.image.description="Pico SDK $PICO_SDK_VERSION firmware builder for picocalc-z80 and picocalc_lua"
