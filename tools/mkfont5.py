#!/usr/bin/env python3
"""
Build the 5 pixel wide fonts for the PicoCalc, giving a 64 column terminal.

320/5 = 64 columns. The glyphs come from the X11 misc-fixed 5x7 and 5x8 fonts,
which are public domain and drawn 4 pixels wide with the fifth column as the
gap between characters. A few glyphs use that fifth column and so touch the
next character; they are redrawn here to fit in 4.

    5X8   5x8 glyphs, no leading                 64x40
    5X9   5x7 glyphs, one row of leading above   64x35
    5X10  5x8 glyphs, one row of leading above   64x32

The file layout is the one described in mkfont.py.

    python3 tools/mkfont5.py sd_files/Z80/fonts
"""

import gzip
import os
import struct
import sys

X11 = '/usr/share/fonts/X11/misc/%s.pcf.gz'
FIRST, LAST = 32, 126

# name, X11 source, cell height, blank rows above the glyph
FONTS = [
    ('5X8', '5x8', 8, 0),
    ('5X9', '5x7', 9, 1),
    ('5X10', '5x8', 10, 1),
]

# Replacements for the source glyphs that touch their neighbour, every row
# of the source cell given, '#' set and '.' clear.
FIXES = {
    '5x7': {
        '#': "..../.#.#/####/.#.#/####/.#.#/....",
        '$': "..#./.###/#.#./.##./..##/###./..#.",
        '+': "..../..../.#../###./.#../..../....",
    },
    '5x8': {
        '#': ".#.#/.#.#/####/.#.#/####/.#.#/.#.#/....",
        '$': "..#./.###/#.#./.##./..##/###./..#./....",
        '+': "..../..../..../.#../###./.#../..../....",
        '@': "..../.##./#..#/#.##/#.##/#.../.##./....",
        'Y': "..../.#.#/.#.#/.#.#/..#./..#./..#./....",
        'm': "..../..../..../#.#./####/#..#/#..#/....",
        'w': "..../..../..../#..#/#..#/####/####/....",
    },
}

PCF_METRICS, PCF_BITMAPS, PCF_ENCODINGS = 4, 8, 32
PCF_ACCEL, PCF_BDF_ACCEL = 2, 256


def load_pcf(path):
    """Return ({code: [row bytes, bit 7 = column 0]}, ascent, descent)."""
    raw = gzip.open(path).read() if path.endswith('.gz') else open(path, 'rb').read()
    if raw[:4] != b'\x01fcp':
        sys.exit('%s: not a PCF font' % path)
    n, = struct.unpack_from('<I', raw, 4)
    tables = {}
    for i in range(n):
        t, _, _, off = struct.unpack_from('<IIII', raw, 8 + 16 * i)
        tables[t] = off

    def header(t):
        off = tables[t]
        fmt, = struct.unpack_from('<I', raw, off)
        return off + 4, fmt, ('>' if fmt & 4 else '<')

    off, fmt, e = header(PCF_BDF_ACCEL if PCF_BDF_ACCEL in tables else PCF_ACCEL)
    asc, desc = struct.unpack_from(e + 'ii', raw, off + 8)

    off, fmt, e = header(PCF_METRICS)
    metrics = []
    if fmt & 0x100:
        cnt, = struct.unpack_from(e + 'H', raw, off)
        off += 2
        for i in range(cnt):
            metrics.append([b - 0x80 for b in raw[off:off + 5]])
            off += 5
    else:
        cnt, = struct.unpack_from(e + 'I', raw, off)
        off += 4
        for i in range(cnt):
            metrics.append(struct.unpack_from(e + 'hhhhh', raw, off))
            off += 12

    off, fmt, e = header(PCF_BITMAPS)
    cnt, = struct.unpack_from(e + 'I', raw, off)
    off += 4
    offsets = struct.unpack_from(e + '%dI' % cnt, raw, off)
    data = raw[off + 4 * cnt + 16:]
    pad = 1 << (fmt & 3)
    if not fmt & 8 or (fmt >> 4) & 3:
        sys.exit('%s: unhandled bitmap format %x' % (path, fmt))

    off, fmt, e = header(PCF_ENCODINGS)
    min2, max2, min1, max1, _ = struct.unpack_from(e + 'HHHHH', raw, off)
    span = max2 - min2 + 1
    index = struct.unpack_from(e + '%dH' % (span * (max1 - min1 + 1)), raw, off + 10)

    glyphs = {}
    for code in range(FIRST, LAST + 1):
        g = index[(0 - min1) * span + code - min2]
        left, right, _, a, d = metrics[g]
        rowbytes = ((right - left + 7) // 8 + pad - 1) // pad * pad
        rows = [0] * (asc + desc)
        for y in range(a + d):
            b = data[offsets[g] + y * rowbytes]
            rows[asc - a + y] = (b >> left if left >= 0 else b << -left) & 0xFF
        glyphs[code] = rows
    return glyphs, asc, desc


def build(src, height, lead):
    glyphs, asc, desc = load_pcf(X11 % src)
    for ch, art in FIXES[src].items():
        rows = art.split('/')
        assert len(rows) == asc + desc, ch
        glyphs[ord(ch)] = [int(r.replace('#', '1').replace('.', '0'), 2) << 4
                           for r in rows]
    for code, rows in glyphs.items():
        assert not any(r & 0x0F for r in rows), '%s %r touches' % (src, chr(code))
    data = bytearray()
    for code in range(FIRST, LAST + 1):
        data.extend([0] * lead + glyphs[code] + [0] * (height - lead - asc - desc))
    return data


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: mkfont5.py <output directory>')
    for name, src, height, lead in FONTS:
        data = build(src, height, lead)
        with open(os.path.join(sys.argv[1], name + '.FNT'), 'wb') as f:
            f.write(struct.pack('<H', len(data)))
            f.write(bytes([LAST - FIRST + 1, FIRST, 5, height, height]))
            f.write(data)


if __name__ == '__main__':
    main()
