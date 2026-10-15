#!/bin/sh
# Put R.COM and W.COM onto a CP/M disk image.
#
# They are what moves files between CP/M and the card, so they cannot move
# themselves; this is the one step that has to happen on a PC. Assembles both
# out of z80pack's cpmtools and writes them into the image with cpmdisk.py.
#
#   ./tools/install-xfer.sh disks/cpm22.dsk
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
Z80PACK=${Z80PACK:-$HERE/../submodules/z80pack}
DISK=$1

if [ -z "$DISK" ] || [ ! -f "$DISK" ]; then
  echo "usage: $0 <disk.dsk>" >&2
  exit 1
fi

echo "assembling R.COM and W.COM..."
make -C "$Z80PACK/cpmtools" r.com w.com >/dev/null

echo "writing into $(basename "$DISK")..."
python3 "$HERE/cpmdisk.py" put "$DISK" "$Z80PACK/cpmtools/r.com" "$Z80PACK/cpmtools/w.com"

python3 "$HERE/cpmdisk.py" list "$DISK" | grep -E "^(R|W)\.COM" || true
echo "done - copy the image to /Z80/disks on the card"
