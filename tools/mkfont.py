#!/usr/bin/env python3
"""
Build a 4x8 bitmap font for the PicoCalc, giving an 80x40 terminal.

320/4 = 80 columns and 320/8 = 40 rows, which is what CP/M-era full screen
software expects. The glyphs are 3 pixels wide inside the 4 pixel cell, the
fourth column being the gap between characters.

Vertical metrics inside the 8 pixel cell:

    row 0   leading
    row 1   cap height, and lowercase ascenders
    row 2   lowercase x-height starts
    row 5   baseline
    row 6   descenders
    row 7   leading

Glyphs below are rows 1 to 6, '#' set and '.' clear. Rows 0 and 7 are always
blank, so a capital occupies rows 1-5 and leaves row 6 empty.

File layout, as read by lcd_load_font() in drivers/lcd.c:

    u16  length      (skipped by the loader)
    u8   glyph_count
    u8   firstcode
    u8   glyph_width
    u8   glyph_height
    u8   bytes per glyph
    ...  glyph data, one byte per row, pixels from bit 7 down
"""

import struct
import sys

WIDTH, HEIGHT = 4, 8
FIRST, LAST = 32, 126
TOP = 1                 # first cell row a glyph may occupy

GLYPHS = {
    ' ': ".../.../.../.../.../...",
    '!': ".#./.#./.#./.../.#./...",
    '"': "#.#/#.#/.../.../.../...",
    '#': "#.#/###/#.#/###/#.#/...",
    '$': ".#./###/##./.##/###/.#.",
    '%': "#.#/..#/.#./#../#.#/...",
    '&': ".#./#.#/.#./#.#/.##/...",
    "'": ".#./.#./.../.../.../...",
    '(': "..#/.#./.#./.#./..#/...",
    ')': "#../.#./.#./.#./#../...",
    '*': ".../#.#/.#./#.#/.../...",
    '+': ".../.#./###/.#./.../...",
    ',': ".../.../.../.#./.#./#..",
    '-': ".../.../###/.../.../...",
    '.': ".../.../.../.../.#./...",
    '/': "..#/..#/.#./#../#../...",
    '0': "###/#.#/#.#/#.#/###/...",
    '1': ".#./##./.#./.#./###/...",
    '2': "###/..#/###/#../###/...",
    '3': "###/..#/.##/..#/###/...",
    '4': "#.#/#.#/###/..#/..#/...",
    '5': "###/#../###/..#/###/...",
    '6': "###/#../###/#.#/###/...",
    '7': "###/..#/..#/..#/..#/...",
    '8': "###/#.#/###/#.#/###/...",
    '9': "###/#.#/###/..#/###/...",
    ':': ".../.#./.../.#./.../...",
    ';': ".../.#./.../.#./.#./#..",
    '<': "..#/.#./#../.#./..#/...",
    '=': ".../###/.../###/.../...",
    '>': "#../.#./..#/.#./#../...",
    '?': "##./..#/.#./.../.#./...",
    '@': ".##/#.#/###/#../.##/...",
    'A': ".#./#.#/###/#.#/#.#/...",
    'B': "##./#.#/##./#.#/##./...",
    'C': ".##/#../#../#../.##/...",
    'D': "##./#.#/#.#/#.#/##./...",
    'E': "###/#../##./#../###/...",
    'F': "###/#../##./#../#../...",
    'G': ".##/#../#.#/#.#/.##/...",
    'H': "#.#/#.#/###/#.#/#.#/...",
    'I': "###/.#./.#./.#./###/...",
    'J': "..#/..#/..#/#.#/.#./...",
    'K': "#.#/#.#/##./#.#/#.#/...",
    'L': "#../#../#../#../###/...",
    'M': "#.#/###/###/#.#/#.#/...",
    'N': "#.#/###/#.#/#.#/#.#/...",
    'O': ".#./#.#/#.#/#.#/.#./...",
    'P': "##./#.#/##./#../#../...",
    'Q': ".#./#.#/#.#/###/.##/...",
    'R': "##./#.#/##./#.#/#.#/...",
    'S': ".##/#../.#./..#/##./...",
    'T': "###/.#./.#./.#./.#./...",
    'U': "#.#/#.#/#.#/#.#/###/...",
    'V': "#.#/#.#/#.#/#.#/.#./...",
    'W': "#.#/#.#/###/###/#.#/...",
    'X': "#.#/#.#/.#./#.#/#.#/...",
    'Y': "#.#/#.#/.#./.#./.#./...",
    'Z': "###/..#/.#./#../###/...",
    '[': ".##/.#./.#./.#./.##/...",
    '\\': "#../#../.#./..#/..#/...",
    ']': "##./.#./.#./.#./##./...",
    '^': ".#./#.#/.../.../.../...",
    '_': ".../.../.../.../.../###",
    '`': "#../.#./.../.../.../...",
    'a': ".../##./.##/#.#/.##/...",
    'b': "#../##./#.#/#.#/##./...",
    'c': ".../.##/#../#../.##/...",
    'd': "..#/.##/#.#/#.#/.##/...",
    'e': ".../.##/###/#../.##/...",
    'f': "..#/.#./###/.#./.#./...",
    'g': ".../.##/#.#/.##/..#/##.",
    'h': "#../##./#.#/#.#/#.#/...",
    'i': ".#./.../.#./.#./.#./...",
    'j': "..#/.../..#/..#/..#/##.",
    'k': "#../#.#/##./#.#/#.#/...",
    'l': "##./.#./.#./.#./.##/...",
    'm': ".../##./###/#.#/#.#/...",
    'n': ".../##./#.#/#.#/#.#/...",
    'o': ".../.#./#.#/#.#/.#./...",
    'p': ".../##./#.#/##./#../#..",
    'q': ".../.##/#.#/.##/..#/..#",
    'r': ".../#.#/##./#../#../...",
    's': ".../.##/##./..#/##./...",
    't': ".#./###/.#./.#./..#/...",
    'u': ".../#.#/#.#/#.#/.##/...",
    'v': ".../#.#/#.#/#.#/.#./...",
    'w': ".../#.#/#.#/###/###/...",
    'x': ".../#.#/.#./.#./#.#/...",
    'y': ".../#.#/#.#/.##/..#/##.",
    'z': ".../###/..#/#../###/...",
    '{': "..#/.#./##./.#./..#/...",
    '|': ".#./.#./.#./.#./.#./...",
    '}': "#../.#./.##/.#./#../...",
    '~': ".../.../.##/##./.../...",
}


def rows_for(ch):
    """Return HEIGHT byte values, pixels packed from bit 7 down."""
    art = GLYPHS[ch].split('/')
    if len(art) != 6:
        raise ValueError("%r has %d rows, want 6" % (ch, len(art)))
    cells = [0] * HEIGHT
    for i, line in enumerate(art):
        if len(line) != 3:
            raise ValueError("%r row %d is %r, want 3 columns" % (ch, i, line))
        bits = 0
        for col, pixel in enumerate(line):
            if pixel == '#':
                bits |= 1 << (7 - col)
            elif pixel != '.':
                raise ValueError("%r row %d has %r" % (ch, i, pixel))
        cells[TOP + i] = bits
    return cells


def main(path):
    missing = [chr(c) for c in range(FIRST, LAST + 1) if chr(c) not in GLYPHS]
    if missing:
        raise SystemExit("no glyph for: %s" % " ".join(missing))

    count = LAST - FIRST + 1
    data = bytearray()
    for code in range(FIRST, LAST + 1):
        data.extend(rows_for(chr(code)))

    with open(path, 'wb') as f:
        f.write(struct.pack('<H', len(data)))
        f.write(bytes([count, FIRST, WIDTH, HEIGHT, HEIGHT]))
        f.write(data)

    print("%s: %d glyphs, %dx%d, %d bytes"
          % (path, count, WIDTH, HEIGHT, 7 + len(data)))
    print("terminal: %d columns x %d rows" % (320 // WIDTH, 320 // HEIGHT))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '4X8.FNT')
