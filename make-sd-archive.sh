#!/bin/sh
# Pack the MicroSD card files: extract at the root of a FAT32 card to get /Z80.
#
#   ./make-sd-archive.sh [output]     default: $DEPLOY/sd_files-<version>.tar.gz
#
# Members come from git (tracked plus untracked-but-not-ignored files under
# sd_files/), so a PCALC.DAT holding a WiFi password never gets in. A
# Z80/VERSION.TXT holding $VERSION (default: git describe) is added.
set -e

DEPLOY=${DEPLOY:-/home/steve/dev/picocalc/uf2-deploy}
SRC=$(cd "$(dirname "$0")/sd_files" && pwd)
VERSION=${VERSION:-$(git -C "$SRC" describe --tags --always --dirty=+ 2>/dev/null || echo unknown)}
OUT=${1:-$DEPLOY/sd_files-$VERSION.tar.gz}

list=$(mktemp)
trap 'rm -f "$list"' EXIT
git -C "$SRC" ls-files -co --exclude-standard | sort > "$list"

if [ ! -s "$list" ]; then
    echo "*** no files found under $SRC ***" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"
python3 - "$SRC" "$list" "$OUT" "$VERSION" <<'EOF'
import io, os, sys, tarfile, time

src, listfile, out, version = sys.argv[1:]
names = [l.strip() for l in open(listfile) if l.strip()]

# the card is FAT, so local owners and permissions mean nothing there
def clean(info):
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mode = 0o755 if info.isdir() else 0o644
    return info

with tarfile.open(out, "w:gz", format=tarfile.USTAR_FORMAT) as t:
    dirs = set()
    for n in names:
        d = os.path.dirname(n)
        while d and d not in dirs:
            dirs.add(d)
            d = os.path.dirname(d)
    # the empty directories are part of the layout; .keep only holds them in git
    for d in sorted(dirs):
        t.add(os.path.join(src, d), d, recursive=False, filter=clean)
    for n in names:
        if os.path.basename(n) == ".keep":
            continue
        if os.path.basename(n).upper() == "PCALC.DAT":
            sys.exit("*** PCALC.DAT would have been archived ***")
        t.add(os.path.join(src, n), n, filter=clean)
    data = (version + "\r\n").encode()
    info = clean(tarfile.TarInfo("Z80/VERSION.TXT"))
    info.size = len(data)
    info.mtime = int(time.time())
    t.addfile(info, io.BytesIO(data))
EOF

echo "$(wc -l < "$list") files plus Z80/VERSION.TXT ($VERSION), $(wc -c < "$OUT") bytes: $OUT"
