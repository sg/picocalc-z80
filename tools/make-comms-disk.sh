#!/bin/sh
# Build the comms disk from z80pack's IMSAI comms disk.
#
#   ./make-comms-disk.sh [out.dsk]   default: sd_files/Z80/disks/COMMS.DSK
#
# XMODEM.ASM is left behind to make room, README.TXT and XMODEM.CFG come
# from cpm/comms, and QT.COM is QT-IMSAI.COM starting on the modem port.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$HERE/..
Z80PACK=${Z80PACK:-$TOP/submodules/z80pack}
SRC=$Z80PACK/imsaisim/disks/library/comms.dsk
OUT=${1:-$TOP/sd_files/Z80/disks/COMMS.DSK}
CPMDISK="python3 $HERE/cpmdisk.py"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

for f in KERMIT.COM KERMIT3.COM QT-IMSAI.COM QT-IMSAI.ASM QTERM.DOC \
         QTCHAT.DOC QTERM.PAT XMODEM.COM; do
    $CPMDISK get "$SRC" "$f" "$tmp/$f" >/dev/null
done

python3 - "$tmp/QT-IMSAI.COM" "$tmp/QT.COM" <<'PY'
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
# the IN/OUT port operands of QTerm's four modem I/O patch routines
for addr, old, new in ((0x111, 0x23, 0x25), (0x121, 0x22, 0x24),
                       (0x131, 0x23, 0x25), (0x141, 0x22, 0x24)):
    if d[addr - 0x100] != old:
        sys.exit("QT-IMSAI.COM: unexpected byte at %04X" % addr)
    d[addr - 0x100] = new
open(sys.argv[2], 'wb').write(d)
PY

for f in README.TXT XMODEM.CFG; do
    sed 's/$/\r/' "$TOP/cpm/comms/$f" > "$tmp/$f"
done

cp "$TOP/disks/empty.dsk" "$OUT"
(cd "$tmp" && $CPMDISK put "$OUT" README.TXT QT.COM QT-IMSAI.COM KERMIT.COM \
    KERMIT3.COM XMODEM.COM XMODEM.CFG QTERM.DOC QTCHAT.DOC QTERM.PAT \
    QT-IMSAI.ASM >/dev/null)
$CPMDISK list "$OUT"
