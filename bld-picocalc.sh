#!/bin/sh
# Quick development build for the Pimoroni Pico Plus 2 W; see build.sh for
# the other boards.
#   ./bld-picocalc.sh [version]      default: dev
exec "$(dirname "$0")/build.sh" pim2w "${1:-dev}"
