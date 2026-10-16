#!/usr/bin/env python3
"""
Read and write z80pack CP/M 2.2 disk images from the host.

Used to get R.COM and W.COM onto a CP/M disk in the first place: they are what
moves files between CP/M and the card, so they cannot move themselves.

The image is a plain IBM 8" SD floppy - 77 tracks of 26 128-byte sectors - and
the CP/M directory is reached through the same sector skew the BIOS applies,
taken from Pico-Eval-Board-80/srccpm2/bios.asm. Parameters come from its DPB:
1K blocks, 64 directory entries in blocks 0 and 1, two reserved tracks.

    cpmdisk.py list    disk.dsk [-u N]
    cpmdisk.py get     disk.dsk [N:]NAME.EXT [outfile] [-u N]
    cpmdisk.py put     disk.dsk file [file ...] [-u N]
    cpmdisk.py era     disk.dsk [N:]NAME.EXT [...] [-u N]
    cpmdisk.py mkhd    disk.hdd            create a blank 4 MB hard disk
    cpmdisk.py sysgen  disk.dsk boot.bin cpm.bin bios.bin

Files live in one of 16 user areas, which CP/M keeps in the first byte of the
directory entry. Everything defaults to user 0; -u picks another, and get also
accepts the ZCPR spelling, 3:NAME.EXT. list shows every area unless told which
one. Blocks are allocated disk-wide rather than per area, so a file in one area
still consumes free space seen by all of them.

The format is chosen by file size: 256256 bytes is the floppy above, 4194304 is
the hard disk drive I:. That one has no skew and no reserved tracks, 2048 byte
blocks and 512 directory entries, so its block pointers are 16 bit - see the
disk parameter block in cpm/bios.asm.
"""

import os
import sys

SECLEN = 128
EMPTY = 0xE5
FILL = 0x1A


class Format:
    def __init__(self, name, size, spt, off, bls, dsm, drm, dirblks, trans):
        self.name = name
        self.size = size
        self.spt = spt              # 128-byte sectors per track
        self.off = off              # reserved system tracks
        self.bls = bls              # allocation block size
        self.spb = bls // SECLEN    # records per block
        self.dsm = dsm              # highest block number
        self.drm = drm              # highest directory entry number
        self.dirblks = dirblks
        self.trans = trans          # skew, or None for none
        self.wide = dsm > 255       # 16 bit block pointers
        self.ptrs = 8 if self.wide else 16
        self.rpe = self.ptrs * self.spb   # records per extent

    def rec_offset(self, lrec):
        """Byte offset in the image of logical record lrec of the data area."""
        track = self.off + lrec // self.spt
        sec = lrec % self.spt
        phys = self.trans[sec] if self.trans else sec + 1
        return (track * self.spt + phys - 1) * SECLEN


# sector translate table for IBM 8" SD disks, 1-based physical sectors
TRANS8 = [1, 7, 13, 19, 25, 5, 11, 17, 23, 3, 9, 15, 21,
          2, 8, 14, 20, 26, 6, 12, 18, 24, 4, 10, 16, 22]

FLOPPY = Format("floppy", 77 * 26 * 128, 26, 2, 1024, 242, 63, 2, TRANS8)
HARDDISK = Format("hard disk", 256 * 128 * 128, 128, 0, 2048, 2047, 511, 8, None)
FORMATS = (FLOPPY, HARDDISK)


def format_for(img):
    for f in FORMATS:
        if len(img) == f.size:
            return f
    raise SystemExit("unrecognised image size %d; expected %s"
                     % (len(img), " or ".join(str(f.size) for f in FORMATS)))


def read_rec(img, fmt, lrec):
    o = fmt.rec_offset(lrec)
    return img[o:o + SECLEN]


def write_rec(img, fmt, lrec, data):
    o = fmt.rec_offset(lrec)
    img[o:o + SECLEN] = data.ljust(SECLEN, bytes([FILL]))


def dir_entry(img, fmt, i):
    rec = read_rec(img, fmt, i // 4)
    return rec[(i % 4) * 32:(i % 4) * 32 + 32]


def put_dir_entry(img, fmt, i, entry):
    lrec = i // 4
    rec = bytearray(read_rec(img, fmt, lrec))
    rec[(i % 4) * 32:(i % 4) * 32 + 32] = entry
    write_rec(img, fmt, lrec, bytes(rec))


def entry_name(e):
    name = bytes(b & 0x7f for b in e[1:9]).decode('ascii', 'replace').rstrip()
    ext = bytes(b & 0x7f for b in e[9:12]).decode('ascii', 'replace').rstrip()
    return name + ('.' + ext if ext else '')


def entry_blocks(e, fmt):
    if fmt.wide:
        raw = [e[16 + 2 * n] | (e[17 + 2 * n] << 8) for n in range(8)]
    else:
        raw = list(e[16:32])
    return [b for b in raw if b != 0]


def pack_blocks(alloc, fmt):
    out = bytearray(16)
    for n, b in enumerate(alloc):
        if fmt.wide:
            out[2 * n] = b & 0xff
            out[2 * n + 1] = b >> 8
        else:
            out[n] = b
    return bytes(out)


def scan(img, fmt):
    """
    Live directory entries as (index, entry), and the blocks in use.

    Both cover every user area: allocation is a property of the disk, not of
    an area, so a file in user 3 takes space away from user 0 as well.
    """
    live = []
    used = set(range(fmt.dirblks))
    for i in range(fmt.drm + 1):
        e = dir_entry(img, fmt, i)
        if e[0] > 15:
            continue
        live.append((i, e))
        used.update(entry_blocks(e, fmt))
    return live, used


def system_banner(img, fmt):
    """
    The signon the BIOS prints at boot, recovered from the reserved tracks.

    Whether a disk offers drive I: is decided by the BIOS on the disk, not by
    the emulator, so this is the quickest way to tell a bootable image that
    knows about the hard disk from one that does not.
    """
    if fmt.off == 0:
        return None                     # no reserved tracks to hold a system
    area = bytes(img[:fmt.off * fmt.spt * SECLEN])
    i = area.find(b'CP/M')
    if i < 0:
        return None

    def printable(c):
        return 32 <= c < 127

    start = i
    while start > 0 and printable(area[start - 1]) and i - start < 16:
        start -= 1
    end = i
    while end < len(area) and printable(area[end]):
        end += 1
    banner = area[start:end].decode('ascii').strip()
    return banner if len(banner) <= 56 else banner[:53].rstrip() + '...'


def split_user(name, user):
    """Accept the ZCPR spelling 3:NAME.EXT as well as a -u option."""
    if ':' in name:
        head, _, tail = name.partition(':')
        if not head.isdigit():
            raise SystemExit("%s: expected a user area before the colon" % name)
        return tail, int(head)
    return name, user


def check_user(user):
    if not 0 <= user <= 15:
        raise SystemExit("user area %d is outside 0-15" % user)
    return user


def cmd_list(path, user=None):
    img = bytearray(open(path, 'rb').read())
    fmt = format_for(img)
    live, used = scan(img, fmt)
    files = {}
    for _, e in live:
        if user is not None and e[0] != user:
            continue
        key = (e[0], entry_name(e))
        files[key] = files.get(key, 0) + e[15]
    for u, name in sorted(files):
        print("%2d: %-14s %5d records" % (u, name, files[(u, name)]))
    print("\n%s: %d/%d blocks used (%d KB free), %d/%d directory entries used"
          % (fmt.name, len(used), fmt.dsm + 1,
             (fmt.dsm + 1 - len(used)) * fmt.bls // 1024,
             len(live), fmt.drm + 1))

    banner = system_banner(img, fmt)
    if banner is None:
        if fmt.off:
            print("system: none, this image is not bootable")
    elif '+HD' in banner:
        print("system: %s, drive I: available" % banner)
    else:
        print("system: %s, no drive I: - sysgen cpm/bios.bin to add it"
              % banner)
    if user is None:
        areas = sorted({e[0] for _, e in live})
        if len(areas) > 1:
            print("user areas in use: %s"
                  % ", ".join(str(a) for a in areas))


def cmd_get(path, name, out=None, user=0):
    img = bytearray(open(path, 'rb').read())
    fmt = format_for(img)
    live, _ = scan(img, fmt)
    name, user = split_user(name, user)
    check_user(user)
    want = name.upper()
    exts = sorted((e[12] | (e[14] << 5), e) for i, e in live
                  if entry_name(e) == want and e[0] == user)
    if not exts:
        elsewhere = sorted({e[0] for _, e in live if entry_name(e) == want})
        if elsewhere:
            raise SystemExit("%s: not in user %d, but is in user %s"
                             % (want, user,
                                ", ".join(str(u) for u in elsewhere)))
        raise SystemExit("%s: not on the disk" % name)
    data = bytearray()
    for _, e in exts:
        rc = e[15]
        for n, blk in enumerate(entry_blocks(e, fmt)):
            for s in range(fmt.spb):
                if n * fmt.spb + s >= rc:
                    break
                data += read_rec(img, fmt, blk * fmt.spb + s)
    out = out or want
    open(out, 'wb').write(data)
    print("%d:%s -> %s, %d bytes" % (user, want, out, len(data)))


def make_name(fname):
    base = os.path.basename(fname).upper()
    stem, _, ext = base.partition('.')
    if len(stem) > 8 or len(ext) > 3 or not stem:
        raise SystemExit("%s: not an 8.3 name" % base)
    return stem.ljust(8).encode('ascii'), ext.ljust(3).encode('ascii')


def cmd_put(path, files, user=0):
    img = bytearray(open(path, 'rb').read())
    fmt = format_for(img)
    check_user(user)

    for fname in files:
        name, ext = make_name(fname)
        data = open(fname, 'rb').read()

        live, used = scan(img, fmt)

        # replace rather than duplicate, but only inside the target area:
        # the same name in another user area is a different file
        for i, e in live:
            if (e[0] == user and e[1:9] == name
                    and bytes(b & 0x7f for b in e[9:12]) == ext):
                put_dir_entry(img, fmt, i, bytes([EMPTY]) + e[1:])
        live, used = scan(img, fmt)

        free_blocks = [b for b in range(fmt.dirblks, fmt.dsm + 1)
                       if b not in used]
        free_dirs = [i for i in range(fmt.drm + 1)
                     if dir_entry(img, fmt, i)[0] > 15]

        records = [data[o:o + SECLEN] for o in range(0, len(data), SECLEN)]
        if not records:
            records = [b'']
        need_blocks = (len(records) + fmt.spb - 1) // fmt.spb
        need_dirs = (len(records) + fmt.rpe - 1) // fmt.rpe
        if need_blocks > len(free_blocks):
            raise SystemExit("%s: only %d blocks free" % (fname, len(free_blocks)))
        if need_dirs > len(free_dirs):
            raise SystemExit("%s: only %d directory entries free" % (fname, len(free_dirs)))

        for ex in range(need_dirs):
            chunk = records[ex * fmt.rpe:(ex + 1) * fmt.rpe]
            alloc = []
            for b in range(0, len(chunk), fmt.spb):
                blk = free_blocks.pop(0)
                alloc.append(blk)
                for s, r in enumerate(chunk[b:b + fmt.spb]):
                    write_rec(img, fmt, blk * fmt.spb + s, r)

            entry = bytearray(32)
            entry[0] = user
            entry[1:9] = name
            entry[9:12] = ext
            entry[12] = ex & 0x1f             # EX
            entry[14] = ex >> 5               # S2
            entry[15] = len(chunk)            # RC
            entry[16:32] = pack_blocks(alloc, fmt)
            put_dir_entry(img, fmt, free_dirs.pop(0), bytes(entry))

        print("%s -> %d:%s, %d bytes, %d record(s), %d block(s)"
              % (fname, user, (name + ext).decode().strip(), len(data),
                 len(records), need_blocks))

    open(path, 'wb').write(img)


def cmd_era(path, names, user=0):
    img = bytearray(open(path, 'rb').read())
    fmt = format_for(img)
    check_user(user)
    live, _ = scan(img, fmt)
    for name in names:
        name, u = split_user(name, user)
        want = name.upper()
        hits = [i for i, e in live if e[0] == u and entry_name(e) == want]
        if not hits:
            raise SystemExit("%s: not in user %d" % (want, u))
        for i in hits:
            e = dir_entry(img, fmt, i)
            put_dir_entry(img, fmt, i, bytes([EMPTY]) + e[1:])
        print("%d:%s erased, %d extent(s)" % (u, want, len(hits)))
    open(path, 'wb').write(img)


def cmd_mkhd(path):
    """A blank hard disk: everything 0xE5, which is what CP/M calls empty."""
    fmt = HARDDISK
    img = bytearray([EMPTY]) * fmt.size
    open(path, 'wb').write(img)
    print("%s: %d bytes, %s, %d directory entries, %d KB free"
          % (path, fmt.size, fmt.name, fmt.drm + 1,
             (fmt.dsm + 1 - fmt.dirblks) * fmt.bls // 1024))


def cmd_sysgen(path, boot, cpm, bios):
    """
    Write the system tracks, the way srccpm2/putsys.c does: the boot sector
    first, then CCP and BDOS, then the BIOS, laid down in physical sector
    order because the boot loader reads them without the skew table.
    """
    img = bytearray(open(path, 'rb').read())
    fmt = format_for(img)
    if fmt is not FLOPPY:
        raise SystemExit("only a floppy image is bootable")

    # cpm.bin was produced with SAVE, so the CCP starts 17 records in, and
    # only CCP+BDOS go on the disk - 44 records, up to where the BIOS lives
    CCP_OFF, CCP_RECS = 17 * SECLEN, 44

    parts = [open(f, 'rb').read() for f in (boot, cpm, bios)]
    sysbytes = fmt.off * fmt.spt * SECLEN
    ccp = parts[1][CCP_OFF:CCP_OFF + CCP_RECS * SECLEN]
    if len(ccp) != CCP_RECS * SECLEN:
        raise SystemExit("%s: too short for a CCP+BDOS image" % cpm)
    blob = parts[0][:SECLEN].ljust(SECLEN, b'\0')
    blob += ccp
    blob += parts[2]
    if len(blob) > sysbytes:
        raise SystemExit("system is %d bytes, only %d fit in %d reserved tracks"
                         % (len(blob), sysbytes, fmt.off))
    img[0:len(blob)] = blob
    open(path, 'wb').write(img)
    print("%s: boot %d, cpm %d, bios %d = %d of %d system bytes (%d sectors)"
          % (path, len(parts[0]), len(ccp), len(parts[2]),
             len(blob), sysbytes, (len(blob) + SECLEN - 1) // SECLEN))


def take_user(args):
    """Pull -u N out of the argument list, returning it and the remainder."""
    user = None
    out = []
    i = 0
    while i < len(args):
        if args[i] == '-u':
            if i + 1 >= len(args) or not args[i + 1].isdigit():
                raise SystemExit("-u needs a user area, 0 to 15")
            user = check_user(int(args[i + 1]))
            i += 2
        else:
            out.append(args[i])
            i += 1
    return user, out


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__.strip())
    user, args = take_user(argv[1:])
    cmd, path, rest = args[0], args[1], args[2:]
    if cmd == 'list':
        cmd_list(path, user)
    elif cmd == 'get':
        if not rest:
            raise SystemExit("get needs a file name")
        cmd_get(path, rest[0], rest[1] if len(rest) > 1 else None,
                0 if user is None else user)
    elif cmd == 'put':
        if not rest:
            raise SystemExit("put needs at least one file")
        cmd_put(path, rest, 0 if user is None else user)
    elif cmd == 'era':
        if not rest:
            raise SystemExit("era needs at least one file name")
        cmd_era(path, rest, 0 if user is None else user)
    elif cmd == 'mkhd':
        cmd_mkhd(path)
    elif cmd == 'sysgen':
        cmd_sysgen(path, rest[0], rest[1], rest[2])
    else:
        raise SystemExit(__doc__.strip())


if __name__ == '__main__':
    main(sys.argv)
