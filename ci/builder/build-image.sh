#!/bin/sh
# Build the release workflow's job image on the Docker host the runner uses.
# Rerun now and then to pick up Ubuntu updates; --pull and --no-cache make
# sure the base image and packages are fresh rather than cached.
#
#   ./build-image.sh [sdk-version]      default: 2.2.0
set -e

SDK=${1:-2.2.0}
cd "$(dirname "$0")"
docker build --pull --no-cache \
    --build-arg PICO_SDK_VERSION="$SDK" \
    -t "picocalc-builder:$SDK" -t picocalc-builder:latest .
docker run --rm "picocalc-builder:$SDK" sh -c \
    'arm-none-eabi-gcc --version | head -1; picotool version; git -C "$PICO_SDK_PATH" describe --tags'
