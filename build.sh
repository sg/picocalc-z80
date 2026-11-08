#!/bin/sh
set -e

# both can be overridden from the environment, which is how CI calls this;
# the defaults sit beside this checkout, absolute as CMake wants the SDK's
top=$(cd "$(dirname "$0")/.." && pwd)
DEPLOY=${DEPLOY:-$top/uf2-deploy}
PICO_SDK_PATH=${PICO_SDK_PATH:-$top/pico-sdk}

usage() {
    echo "Usage: $0 <board|all> [version]"
    echo ""
    echo "The version is used as the uf2 filename suffix and defaults"
    echo "to the output of git describe."
    echo ""
    echo "Boards:"
    echo "  pico    - Raspberry Pi Pico (RP2040, no WiFi)"
    echo "  picow   - Raspberry Pi Pico W (RP2040, WiFi)"
    echo "  pico2   - Raspberry Pi Pico 2 (RP2350, no WiFi)"
    echo "  pico2w  - Raspberry Pi Pico 2 W (RP2350, WiFi)"
    echo "  pim2w   - Pimoroni Pico Plus 2 W (RP2350, WiFi)"
    echo "  all     - Build all boards sequentially"
    exit 1
}

board_to_pico_board() {
    case "$1" in
        pico)   echo "pico" ;;
        picow)  echo "pico_w" ;;
        pico2)  echo "pico2" ;;
        pico2w) echo "pico2_w" ;;
        pim2w)  echo "pimoroni_pico_plus2_w_rp2350" ;;
        *)      echo ""; return 1 ;;
    esac
}

ALL_BOARDS="pico picow pico2 pico2w pim2w"

build_board() {
    board="$1"
    build_num="$2"

    pico_board=$(board_to_pico_board "$board")
    if [ -z "$pico_board" ]; then
        echo "*** Unknown board: $board ***"
        return 1
    fi

    build_dir="build-${board}"
    build_name="picocalc-z80-${board}"

    echo "========================================"
    echo "Building for: $board (PICO_BOARD=$pico_board)"
    echo "Build dir:    $build_dir"
    echo "========================================"

    rm -rf "$build_dir"

    if ! PICO_SDK_PATH="$PICO_SDK_PATH" cmake \
        -DPICO_BOARD="$pico_board" \
        -DPICOCALC_BOARD="$board" \
        -DPICOCALC_VERSION="$build_num" \
        -B "$build_dir" -S . ; then
        echo "*** cmake failed for $board ***"
        return 1
    fi

    echo "Running make for $board..."
    if ! make -j"$(nproc)" -C "$build_dir"; then
        echo "*** build failed for $board ***"
        return 1
    fi

    if [ -n "$build_num" ]; then
        uf2_name="${build_name}-${build_num}.uf2"
    else
        uf2_name="${build_name}.uf2"
    fi

    mkdir -p "$DEPLOY"
    echo "Copying $uf2_name to $DEPLOY..."
    if cp "$build_dir/picocalc-z80.uf2" "$DEPLOY/$uf2_name"; then
        echo "$board build complete: $DEPLOY/$uf2_name"
    else
        echo "*** copying uf2 to $DEPLOY failed for $board ***"
        return 1
    fi

    echo ""
}

# --- main ---

if [ $# -lt 1 ]; then
    usage
fi

cd "$(dirname "$0")"

BOARD="$1"
# CI passes the release tag, so local builds get the same style of name
BUILD_NUM=${2:-$(git describe --tags --always --dirty=+ 2>/dev/null || true)}

if [ "$BOARD" = "all" ]; then
    for b in $ALL_BOARDS; do
        build_board "$b" "$BUILD_NUM"
    done
    echo "All boards built successfully!"
else
    build_board "$BOARD" "$BUILD_NUM"
fi
