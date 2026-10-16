#!/bin/sh
# Build Miguel Garcia's TE text editor for the PicoCalc and put it on a disk.
#
#   ./make-te-disk.sh [out.dsk]   default: sd_files/Z80/disks/TE.DSK
#
# TE is written in MESCC, a C compiler that runs under CP/M, so the build
# runs under CP/M too: z80pack's cpmsim is built from the submodule, given
# its CP/M 2.2 boot disk on A: and a 4 MB work disk on I: holding MESCC, its
# headers, TE's sources and cpm/te/tepcalc.c, the PicoCalc adaptation, and
# fed the command lines of TE's make.bat. TECF then patches the editor with
# cpm/te/TEPCALC.CF. Needs cpmtools (cpmcp, cpmls, cpmrm) for the work disk.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$HERE/..
Z80PACK=${Z80PACK:-$TOP/submodules/z80pack}
TE=${TE:-$TOP/submodules/te}
MESCC=${MESCC:-$TOP/submodules/mescc}
OUT=${1:-$TOP/sd_files/Z80/disks/TE.DSK}
CPMDISK="python3 $HERE/cpmdisk.py"

for t in cpmcp cpmls cpmrm; do
    command -v $t >/dev/null || { echo "*** $t not found: install cpmtools ***" >&2; exit 1; }
done

# the simulator and its helper, which it spawns for the AUX port
make -s -C "$Z80PACK/cpmsim/srcsim" >/dev/null
make -s -C "$Z80PACK/cpmsim/srctools" >/dev/null
PATH=$Z80PACK/cpmsim/srctools:$PATH
export PATH

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/disks"
cp "$Z80PACK/cpmsim/disks/library/cpm22-1.dsk" "$tmp/disks/drivea.dsk"
(cd "$tmp" && mkdskimg i >/dev/null)
HD="$tmp/disks/drivei.dsk"

# text already in CP/M form (CR LF, ^Z) goes in as it is, the rest is converted
put() {
    f=$1
    n=$(basename "$f" | tr '[:upper:]' '[:lower:]')
    if grep -qI . "$f" 2>/dev/null && ! grep -q "$(printf '\r')" "$f"; then
        cpmcp -f z80pack-hd -t "$HD" "$f" "0:$n"
    else
        cpmcp -f z80pack-hd "$HD" "$f" "0:$n"
    fi
}

for f in "$MESCC"/cc.com "$MESCC"/CCOPT.COM "$MESCC"/ZSM.COM "$MESCC"/HEXTOCOM.COM \
         "$MESCC"/*.h "$TE"/TECF.COM "$TE"/te.h "$TE"/te_keys.h \
         "$TE"/te.c "$TE"/te_conf.c "$TE"/te_edit.c "$TE"/te_error.c "$TE"/te_file.c \
         "$TE"/te_keys.c "$TE"/te_lines.c "$TE"/te_macro.c "$TE"/te_misc.c "$TE"/te_ui.c \
         "$TOP"/cpm/te/tepcalc.c "$TOP"/cpm/te/TEPCALC.CF; do
    put "$f"
done

# TE's make.bat, line by line. -E- stops the compiler pausing on an error.
# The BDOS keeps one typed-ahead character in a buffer that a warm boot
# throws away, so every command after a program has run loses its first
# character; a leading space, which the CCP skips anyway, is what it loses.
(cd "$tmp" && printf 'I:\rCC TEPCALC -E-\r CCOPT TEPCALC\r ZSM TEPCALC\r HEXTOCOM TEPCALC\r TECF PATCH TEPCALC.COM TEPCALC.CF\r TECF DUMP TEPCALC.COM\r A:BYE\r' \
    | timeout 600 "$Z80PACK/cpmsim/cpmsim" > build.log 2>&1) || true
tr -d '\r' < "$tmp/build.log" | grep -E "^Errors|bytes saved|Size of code|^te.confName|^screen\." || true
if ! grep -q "Errors:     0" "$tmp/build.log" || ! grep -q "^Errors    0" "$tmp/build.log"; then
    echo "*** the compiler or the assembler reported errors, see above ***" >&2
    tr -d '\r' < "$tmp/build.log" | grep -iv "function:\|include" | tail -40 >&2
    exit 1
fi
cpmcp -f z80pack-hd "$HD" 0:tepcalc.com "$tmp/TE.COM"

# the disk: the editor, its configuration tool with the configuration under
# the name TECF PATCH takes by default, the text tool and the macro examples
sed 's/$/\r/' "$TOP/cpm/te/TEPCALC.CF" > "$tmp/TE.CF"
cp "$TE"/TECF.COM "$TE"/TETX.COM "$tmp"
for f in "$TE"/*.M; do
    if grep -q "$(printf '\r')" "$f"; then cp "$f" "$tmp"; else sed 's/$/\r/' "$f" > "$tmp/$(basename "$f")"; fi
done
$CPMDISK mkdsk "$OUT" >/dev/null
(cd "$tmp" && $CPMDISK put "$OUT" TE.COM TE.CF TECF.COM TETX.COM *.M >/dev/null)
$CPMDISK list "$OUT"
