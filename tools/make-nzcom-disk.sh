#!/bin/sh
# Build the NZ-COM boot disk: CPM22.DSK with the development tools taken off
# to make room, the NZ-COM files added and PROFILE.SUB running NZCOM at boot.
#
#   ./make-nzcom-disk.sh [nzcom-dir] [out.dsk]
#   defaults: cpm-files/nzcom and sd_files/Z80/disks/NZCOM.DSK
#
# The NZ-COM files came configured for z80pack's cpmsim, which has drives J:
# and P:, so the environment is rewritten for A: to D: and I: here, and the
# startup alias, which fetched files from J:, is emptied.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$HERE/..
SRC=${1:-$TOP/cpm-files/nzcom}
OUT=${2:-$TOP/sd_files/Z80/disks/NZCOM.DSK}
CPMDISK="python3 $HERE/cpmdisk.py"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cp "$TOP/sd_files/Z80/disks/CPM22.DSK" "$OUT"

# the CCP runs its command buffer once at cold boot, which is how Udo's
# AUTO.SUB makes a disk start with SUBMIT PROFILE
python3 - "$TOP/cpm/cpm.bin" "$tmp/cpm.bin" <<'PY'
import sys
cpm = bytearray(open(sys.argv[1], 'rb').read())
ccp = 17 * 128
if cpm[ccp + 6] != 0x7f or b'COPYRIGHT' not in cpm[ccp + 8:ccp + 0x40]:
    sys.exit('cpm.bin: CCP command buffer not where expected')
cmd = b'SUBMIT PROFILE'
cpm[ccp + 7] = len(cmd)
cpm[ccp + 8:ccp + 9 + len(cmd)] = cmd + b'\0'
open(sys.argv[2], 'wb').write(cpm)
PY
$CPMDISK sysgen "$OUT" "$TOP/cpm/boot.bin" "$tmp/cpm.bin" "$TOP/cpm/bios.bin" >/dev/null

$CPMDISK era "$OUT" ASM.COM AUTO.SUB BIOS.ASM BIOS.HEX BIOS.PRN BOOT.ASM \
    BOOT.HEX BOOT.PRN CPM64.SYS DDT.COM LINK.COM LOAD.COM MAC.COM MEMMAP.COM \
    MOVCPM.COM RMAC.COM SURVEY.MAC SYSGEN.COM SYSGEN64.SUB >/dev/null

python3 - "$SRC" "$tmp" <<'PY'
import os, sys
src, out = sys.argv[1:]

def read(name):
    return bytearray(open(os.path.join(src, name), 'rb').read())

def write(name, data):
    open(os.path.join(out, name), 'wb').write(data)

# drive I: is the highest, the valid drives are A-D and I (bit 0 is A:),
# and the screen is the 64x32 of the built-in font
env = read('nzcom.env')
if env[3:8] != b'Z3ENV':
    sys.exit('nzcom.env: not an environment descriptor')
env[0x2c] = 9
env[0x31:0x34] = bytes([64, 32, 30])
env[0x34:0x36] = (0x010f).to_bytes(2, 'little')
write('NZCOM.ENV', env)

# MKZCM keeps its own copy of those settings
zcm = read('nzcom.zcm')
for old, new in ((b'0010 MAXDRV', b'0009 MAXDRV'), (b'FFFF DRVEC', b'010F DRVEC'),
                 (b'0050 COLS', b'0040 COLS'), (b'0018 ROWS', b'0020 ROWS'),
                 (b'0016 LINS', b'001E LINS')):
    if old not in zcm:
        sys.exit('nzcom.zcm: no %s' % old.decode())
    zcm = zcm.replace(old, new)
write('NZCOM.ZCM', zcm)

# the alias ran NAME and PATH off J:; the path NZCOM installs already
# reaches A0:, so it has nothing left to do
alias = read('startzcm.com')
text = alias.find(b'Z3 ALIAS') + 8
if text < 8 or alias[text:text + 2] != b'j:':
    sys.exit('startzcm.com: not the expected alias')
alias[text:alias.index(b'\0', text)] = bytes(alias.index(b'\0', text) - text)
write('STARTZCM.COM', alias)

# EXIT, CPM and OFF expect NZCPM in A15:, it is in A0: here
write('ALIAS.CMD', read('alias.cmd').replace(b'a15:nzcpm', b'a0:nzcpm'))

write('PROFILE.SUB', b'nzcom\r\n\x1a')
PY

(cd "$tmp" && $CPMDISK put "$OUT" PROFILE.SUB NZCOM.ENV NZCOM.ZCM STARTZCM.COM \
    ALIAS.CMD >/dev/null)
(cd "$SRC" && $CPMDISK put "$OUT" nzcom.com nzcom.lbr nzcom.ccp nzcom.z3t \
    nzcpm.com mkzcm.com arunz.com cmdrun.com filer.com if.com ldir.com lx.com \
    salias.com sdz.com show.com zex.com zfiler.cmd >/dev/null)
$CPMDISK list "$OUT"
