# PicoCalc-Z80 

Turn the PicoCalc into a pocket Z80 machine with its own screen, keyboard, network stack (on W boards), and disk images allowing CP/M and other operating systems to run faithfully. It uses Udo Munk's fantastic [z80pack](https://github.com/udo-munk/z80pack) Z80/8080 simulator and his [Pico-Eval-Board-80](https://github.com/udo-munk/Pico-Eval-Board-80),
with the Waveshare-specific hardware layer replaced with PicoCalc equivalents.

## Status

| Feature | State |
| ------- | ----- |
| CP/M on the LCD and keyboard, status block, scrollback | working |
| Drive menu over the running machine, Ctrl+Alt+D | working |
| WiFi menu, `AT+WIFI`, NTP clock, IP on the status line | working on the Plus 2 W |
| WiFi menu over the running machine, Ctrl+Alt+W | working on the Plus 2 W |
| AT modem dialling out, telnet, `+++` / `ATH` | working on the Plus 2 W (telehack.com) |
| AT modem answering calls | working on the Plus 2W |
| Disk track cache in the Plus 2 W's PSRAM | working on the Plus 2 W |
| USB-C serial console mirror, buffered paste | working |
| Update menu: version check, firmware and card files from the release site, HTTPS | working on the Plus 2 W over HTTP and HTTPS |
| Crash record on the banner after a hard fault, panic or network hang | built, not yet seen on hardware |
| IOBYTE device routing, comms disk (Kermit, QTerm, XMODEM over the modem) | tested in hostsim, not yet on hardware |
| XMODEM over USB-C | built, not yet tried |
| Dazzler, WordStar keys, font switching | working on the Plus 2W |
| Pico, Pico W, Pico 2, Pico 2 W builds | build cleanly, only tested on [picocalc-sim](https://github.com/sg/picocalc-sim) |

Not built (yet): sound on an I/O port.

## Status block

The bottom three rows are the terminal's: a blank separator, a machine line in
phosphor green, and a drives line in bright cyan.

```
Z80 4MHz  192.168.1.42  A:R                            14:22:07   87%
A:NZCOM.DSK    B:XYBASIC.DSK  C:[EMPTY]      D:[EMPTY]      I:HD0.HDD
```

The first line displays the CPU and its clockspeed, the IP address when WiFi is up, a
drive activity indicator (drive and `R` or `W`, for a quarter second), the time,
and the battery percentage with `+` while charging.
The second line lists all five drives. Five fields do not fit a 64 column font, so below 75 columns the
extensions are dropped since they are all `.DSK` or `.HDD` anyway.

## Scrollback

Ctrl+Up and Ctrl+Down scroll a line at a time, Ctrl+Shift+Up and
Ctrl+Shift+Down (PgUp and PgDn on this keyboard, see [Keys](#keys)) a screen
at a time, and any ordinary key returns to the prompt. They repeat while
held. 

Output to the LCD panel is normally lost once it scrolls off screen, so history 
is maintained using a shadow copy of the text in RAM. The following lists how 
much scrollback is kept for each board:

Pico: XXX rows, XX KB (~ XX screens).
Pico2: XXX rows, XX KB (~ XX screens).
Pimoroni (uses PSRAM): 16,384 rows, 1.25 MB (~ 500 screens).

## Drive menu

`D - Disk setup` in the config dialog, and Ctrl+Alt+D while the machine
runs, open a box over the screen for swapping disks, the way a real machine
lets you open a drive door at any time:

```
+----------------- Drives -----------------+
| A: CPM22.DSK                             |
| B: TE.DSK                                |
| C: (empty)                               |
| D: COMMS.DSK                             |
| I: (empty)                               |
| Up, Down, A-D, I: choose a drive         |
| Enter: insert a disk    Del: eject       |
| Esc: close                               |
| CP/M sees a changed disk after ^C        |
+------------------------------------------+
```

Enter, or a drive letter, lists the `.DSK` images in `/Z80/disks` for that
drive (`.HDD` for I:), in name order with the one currently in it
highlighted; an image already in another drive is shown dimmed with the
drive it is in and cannot be chosen twice. Del ejects. Esc closes the box
and writes the drives to `PCALC.DAT` if anything changed, so the next boot
starts with the same disks.

CP/M 2.2 does not notice a swap by itself. Its directory check vectors catch
the change on the next access and mark the drive read-only until a warm
boot, exactly as on real hardware. After a disk swap, once back in CP/M typing 
Ctrl-C will cause CP/M to see the disk swap.

## WiFi menu

`N - Network setup` in the config dialog, and Ctrl+Alt+W while the machine
runs, open a box over the screen with the network, its live state and every
network setting, for getting back on the air after a drop without a reboot,
for moving to another network, or for setting the machine up:

```
+------------------- WiFi -------------------+
| Network:  MyNet                            |
| Status:   MyNet 192.168.1.123 -58dBm      |
| C: connect          D: disconnect          |
| S: scan for a network                      |
| N: network name     P: password            |
| A: connect at boot: yes                    |
| Y: country: US      U: UTC offset: -7      |
| T: NTP server: pool.ntp.org                |
| M: modem init:                             |
| Esc: close                                 |
+--------------------------------------------+
```

`C` joins the network shown, and the status row follows the join as it
happens, since the radio keeps running on core 0 while the box waits. `S`
scans and lists what it found by signal strength; Enter on a network asks
for its password when it has one, Enter alone keeping the current one, and
joins. `N` and `P` edit the name and the password in place, `P` with nothing
typed clearing it for an open network. `A` toggles connecting at boot, `Y`
takes a two letter country code or nothing for worldwide, which the radio
reads when it starts, `U` the UTC offset from -12 to 14, `T` the NTP server
with nothing meaning the default, and `M` the modem init string. Esc closes,
and anything changed is written to `PCALC.DAT`.

It shares its box with the drive menu, `srcsim/overlay.c`, and is paused and
resumed the same way.

### Keys

`srcsim/keymap.c` turns the keyboard controller's codes into what a terminal
would send: ESC as `0x1b` rather than `0xb1`, Enter as CR, Ctrl+letter as a
control code, Ctrl+`[` as ESC, Alt as a meta prefix, and cursor sequences for
the arrows, Home/End, paging, Insert and the function keys.

CP/M software very rarely reads ANSI cursor sequences, though — WordStar, Turbo
Pascal and most of their descendants listen for the control-key diamond
instead. So `K` in the config dialog switches the arrow and paging keys between
the two:

| Key | ANSI | WordStar |
| --- | ---- | -------- |
| Up / Down | `ESC [ A` / `ESC [ B` | `^E` / `^X` |
| Left / Right | `ESC [ D` / `ESC [ C` | `^S` / `^D` |
| PgUp / PgDn | `ESC [ 5~` / `ESC [ 6~` | `^R` / `^C` |
| Home / End | `ESC [ H` / `ESC [ F` | `^QS` / `^QD` |
| Del / Ins | `0x7f` / `ESC [ 2~` | `^G` / `^V` |

Shift+Up sends PgUp.
Shift+Down sends PgDn.
Shift+Tab sends Home.
Shift+Del sends End.
Shift+Enter sends Insert.

The BREAK key stops the machine.
Ctrl-Alt-Del reboots.
Ctrl-Alt-F1 reboots to BOOTSEL.

## Memory footprint

| Board | Flash | RAM |
| ----- | ----- | --- |
| `pico` | 314 KB of 2 MB | 246 KB of 256 KB |
| `picow` | 684 KB of 2 MB | 248 KB of 256 KB |
| `pico2` | 300 KB of 4 MB | 246 KB of 512 KB |
| `pico2w` | 768 KB of 4 MB | 394 KB of 512 KB |
| `pim2w` | 770 KB of 16 MB | 358 KB of 512 KB |

RAM includes a 32 KB heap floor (16 KB on the Pico W). WiFi, the modem and the
update client account for about 370 KB of the flash on the Pico W and 460 KB
on the RP2350 boards, mostly the CYW43 firmware and then mbedtls; build with
`-DPICOCALC_WIFI=OFF` to leave them out. The TLS builds also carry a 64 KB
lwIP heap, which serves mbedtls as well, and twice the pbufs, because a 16 KB
TLS record has to sit in the TCP window before it can be decrypted and the
RAM is there to spend. The update's own buffers borrow the Z80's memory and
add nothing. The Pimoroni build is 35 KB lighter than the Pico 2 W's because
its scrollback history lives in the PSRAM.

112 KB of that RAM is the emulated machine itself — `bnk0[65536]` plus
`bnk1[49152]` in `srcsim/simmem.h` — and 8 KB is core 1's stack. Core 1's
default stack is SCRATCH_X, which is capped at 4 KB and which has to cover
the config dialog and `printf` as well as the CPU loop, so `srcsim/picosim.c`
hands it one in main SRAM instead. Core 0's default stack is SCRATCH_Y, 4 KB
as well. The TLS handshake runs on it inside lwIP's callbacks, and the stack
watermark put core 0's whole use at 2.5 KB during one, too close for comfort,
so the TLS builds give core 0 a 16 KB stack at the top of main RAM. The SDK
places that stack in the scratch Y region, which holds nothing else here, so
three linker script variables in `CMakeLists.txt` move the region to the last
16 KB of main RAM and shorten main RAM to match; the 394 KB above includes
it.

## SD card

Everything lives under the /Z80 directory on the SD card. Copy `sd_files/Z80` to the root of a
FAT-formatted card, or extract a release's
`picocalc-z80-sd-files-<version>.tar.gz` there:

| Path | Contents |
| ---- | -------- |
| `/Z80/disks/` | `CPM22.DSK` ships ready to use, `COMMS.DSK` holds the modem programs, `NZCOM.DSK` boots into Z-System, `XYBASIC.DSK` (not bootable) holds Udo's builds of Mark Williams' XYBASIC, `TE.DSK` (not bootable) the TE text editor, `DAZZLER.DSK` (not bootable) Udo's Cromemco Dazzler demos |
| `/Z80/code/`  | Udo's bare machine programs from `submodules/z80pack/picosim/src-examples`: the 8080 CPU test, Tiny BASIC, Microchess, the Altair BASICs; `L` loads one at address 0 and `R` runs it |
| `/Z80/xfer/`  | files to move in and out of CP/M, see below; the update menu stages its archive here |
| `/Z80/fonts/` | `4X8.FNT`, `5X8.FNT`, `5X9.FNT`, `5X10.FNT`, `6X8.FNT` and any other fonts |
| `/Z80/`       | `PCALC.DAT` is written here; `VERSION.TXT` identifies the release the card files came from |

Case does not matter anywhere: FatFs matches names without regard to it, so
`/z80/disks/cpm22.dsk` and `/Z80/DISKS/CPM22.DSK` are the same file. Names are
always *shown* in upper case, which is how they look from CP/M.

`DAZZLER.DSK` holds the graphics demos: put it in B: with `CPM22.DSK` in A:,
change to B: and run one, `KSCOPE` for instance; the `.SUB` files show how
the others are built or started.

### The NZ-COM disk

`sd_files/Z80/disks/NZCOM.DSK` boots into Z-System. It is `CPM22.DSK` with
the assemblers, debuggers and BIOS sources taken off to make room, the NZ-COM
1.2 files added, and a CCP whose cold boot command is `SUBMIT PROFILE`, with
`PROFILE.SUB` running `NZCOM` — the mechanism Udo's `AUTO.SUB` sets up, which
`CPM22.DSK` carries the files for but does not have switched on. `EXIT`, `CPM`
or `NZCPM` drops back to CP/M. `tools/make-nzcom-disk.sh` builds it from
`cpm-files/nzcom`, which is not in the repository.

NZ-COM came configured for z80pack's cpmsim, where drives J: and P: exist,
and its startup alias fetched files from J: — the three `ZRDOS Err 2 on J:`
after loading. The script rewrites `NZCOM.ENV` and `NZCOM.ZCM` for this
machine: I: is the highest drive, the drive vector allows A: to D: and I:
(bit 0 is A:), and the screen is the built-in font's 64x32. A drive outside
the vector now gets `E:?` from the command processor instead of a DOS error.
`MKZCM` changes these settings and `SALIAS STARTZCM` edits the startup alias,
which is empty, since the path NZCOM installs already reaches A0:. The named
directories in `NZCOM.LBR` call A0: `COMMANDS` and B0: `WORK`, hence the
`A0:COMMANDS>` prompt.

### The TE editor

`TE.DSK` carries Miguel Garcia's [TE](https://github.com/MiguelVis/te), a
small full-screen text editor for CP/M, built for this machine. It edits in
memory, shows line numbers and a ruler, and has a clipboard, find, go to
line, blocks of lines and macros; saving keeps the previous version as
`TE.BKP`. Mount the disk as B: and run `B:TE FILE.TXT`, or copy `TE.COM` to
the disk you work from.

The build is `cpm/te/tepcalc.c`, TE's adaptation layer for this console: 64
columns by 32 rows, which is the 5x9 font with the status block on, ANSI
escapes, and console I/O straight to the BIOS. It binds the WordStar keys of
TE's VT100 variant and turns the ANSI sequences the keyboard sends in its
default mode into them, so the arrows, Home, End, PgUp, PgDn and Del (the
Shift chords of Tab, Del, Up and Down on this keyboard) do the same whichever
way `K - Arrow keys` is set. `ESC` opens the menu, whose
`Help` lists the rest:

```
Up ^E       Down ^X      Left ^S       Right ^D      PgUp ^R       PgDown ^C
Begin ^QS   End ^QD      Top ^QR       Bottom ^QC    WordLeft ^A   WordRight ^F
DelLeft ^H  DelRight ^G  Cut ^Y        Copy ^O       Paste ^W      Delete ^KY
Find ^QF    FindNext ^L  GoLine ^J     ClearClip ^T  Macro ^QM
BlockStart ^KB           BlockEnd ^KK  BlockUnset ^KU
```

`TECF.COM` and `TE.CF` are on the disk as well. Edit `TE.CF`, with TE, and
run `TECF PATCH` to change the screen size for another font, the tab size,
the line limit (1024 by default, two bytes a line) or the keys; `TECF DUMP`
shows what is patched in. `TETX.COM` is TE's text tool and the `.M` files
are its macro examples.

`tools/make-te-disk.sh` rebuilds the disk. TE is written in MESCC, a C
compiler that itself runs under CP/M, so the script builds z80pack's cpmsim
from the submodule, puts MESCC, TE's sources and the adaptation on a work
disk with cpmtools, runs TE's own build steps under CP/M 2.2 there, and
packs the result with `cpmdisk.py`. TE and MESCC are GPL and come in as
`submodules/te` and `submodules/mescc`.

## The hard disk, drive I:

Four 250 KB floppies is not much room, so there is a 4 MB fixed disk on drive
I:. Create the image once and mount it with `I` in the config dialog:

```sh
python3 tools/cpmdisk.py mkhd sd_files/Z80/disks/HD0.HDD
```

```
A>STAT I:
Bytes Remaining On I: 4080k
A>PIP I:=*.COM
```

It needs no new hardware. The disk controller already carries a 4 bit drive
number and a byte each of track and sector, so drive 8 is simply a different
geometry — 256 tracks of 128 sectors, which is 4 MB exactly and keeps both
inside a byte. What it does need is a CP/M that knows about it, so `cpm/`
holds a BIOS with a ninth disk parameter header and a second disk parameter
block: 2048 byte blocks, 2048 of them, 512 directory entries, no skew and no
reserved tracks. `CPM22.DSK` ships with that BIOS already on its system tracks
and announces itself as `VERS B01+HD` at boot. The same BIOS routes the
character devices by IOBYTE, see the comms disk under AT modem.

To rebuild it, assemble with z80pack's z80asm in 8080 mode and write the
system tracks with `tools/cpmdisk.py`:

```sh
z80asm -8 -l -T -sn -p0 -fb -x bios.asm        # in cpm/
python3 tools/cpmdisk.py sysgen sd_files/Z80/disks/CPM22.DSK cpm/boot.bin cpm/cpm.bin cpm/bios.bin
```

The boot loader reads 51 sectors, which leaves 7 (896 bytes) for the BIOS
code. The `BIOS.ASM` and `SYSGEN64.SUB` on the disk are Udo's stock ones;
running that SUB file rebuilds the stock BIOS without drive I: or the device
routing.

Two details of that BIOS are worth knowing if you change it. Block pointers
are 16 bit because there are more than 256 blocks, which halves them to 8 per
directory entry — 2048 byte blocks keep the extent mask at 0 so an entry is
still one extent, the same as the floppies. And `SECTRAN` now returns the
sector unchanged when a drive has no translate table, because the hard disk
has none, adding one to match the controller's 1-based sectors.

Drive I: is decided by the BIOS on the disk you boot, not by the firmware, so
a disk image with the stock BIOS answers `Bdos Err On I: Select` however the
image is mounted. `tools/cpmdisk.py list` reports which one an image has:

```
system: 64K CP/M 2.2 VERS B01+HD, drive I: available
system: 64K CP/M 2.2 VERS B01, no drive I: - sysgen cpm/bios.bin to add it
```

To put that BIOS on another image:

```sh
python3 tools/cpmdisk.py sysgen some.dsk cpm/boot.bin cpm/cpm.bin cpm/bios.bin
```

That rewrites the two reserved tracks only and leaves the files alone.

## PSRAM

The Pimoroni Pico Plus 2 W has 8 MB of QSPI PSRAM on the RP2350's second
QMI chip select, and this is being used as a disk track cache. On startup 
the boot banner shows the PSRAM size and with a sequential read and write 
throughput measured over 512 KB. For boards with no PSRAM chip it shows
a size of 0.

The memory sits behind the 16 KB XIP cache that the flash code also uses, 
so it is fast for bulk copies and slow for scattered accesses. That rules 
it out as the C heap and as emulated Z80 memory, and makes it right for 
using as a disk track cache. On the development board it reads at 26 MB/s 
and writes at 9 MB/s.

### The PSRAM disk track cache

Every FDC transfer used to be an `f_lseek` and a 128 byte `f_read` or
`f_write`, which costs a card sector read about every fourth record and
makes directory listings and program loads visibly card-bound. With the
cache enabled (`M` in the config dialog, on by default when the PSRAM is there,
kept in `PCALC.DAT`), the first sector read from a track pulls the whole
track from the card into that drive's slot of the PSRAM, 3,328 bytes for a
floppy and 16 KB for the hard disk. Writes go to the card first, 
including the `f_sync`, and then into the cached track if it is
there, so the card stays the truth and a reset or a pulled card loses
nothing. Closing a drive, which a swap, an eject or an I/O error does,
forgets its tracks. A track the card will not give whole is read sector by
sector.

The slots are fixed: four 256 KB slots for A: to D: and 4 MB for I:, 5 MB of
the 8. The scrollback history takes 1.25 MB after them, which leaves about
1.75 MB; `srcsim/psram.h` holds the map.

## Cromemco Dazzler

Udo's `dazzler.c` retargeted from the Waveshare drawing calls. The pixel
decoding is his, unchanged. What changed is where the pixels go. Ports 14 and
15 are wired up, so Udo's demo disk, `DAZZLER.DSK`, works as it does on his
machines.

The 128x128 image goes up at exactly 2x on the PicoCalc's square panel, centred with a 32
pixel margin. Frames are composed into a 32 KB buffer and blitted a row at a
time rather than sent pixel by pixel: the Dazzler's scan order walks display
memory in quadrants rather than in scanlines, so every pixel would otherwise
cost its own column and row address command.

The terminal and the Dazzler share the screen, so a program that prints while 
graphics are up will draw over them. Leaving graphics mode clears back to a
blank terminal.

## Console

The screen and keyboard are registered as a pico-sdk stdio driver, so the
emulated serial terminal, the configuration dialog and `printf` all go through
one path. Output is `drivers/term.c`. Input is `srcsim/keymap.c`.

The console is also mirrored on the mainboard's USB-C port. That port is a
CH340 USB-serial bridge wired to the Pico's UART0 on GP0/GP1, so a plain USB-C
data cable is all a PC needs — no adapter. The bridge is powered from the
PicoCalc's own 3.3 V rail, so it only appears while the unit is switched on. The
DIP switch must be off; its two poles move together, and the on position halts
the keyboard controller for reflashing and routes the USB lines away from the
bridge, so nothing enumerates.

| Host | Device | Connect with |
| ---- | ------ | ------------ |
| Linux | `/dev/ttyUSB0` | `picocom -b 115200 -f x /dev/ttyUSB0` |
| macOS | `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*` | `picocom -b 115200 -f x /dev/cu.usbserial-XXXX` |
| Windows | `COMn` (WCH CH340 driver) | PuTTY, Tera Term |

A working connection shows `1a86:7523 QinHeng Electronics CH340` in `lsusb`. If
`dmesg -w` shows nothing on plug-in, suspect a charge-only cable or the DIP
switch. If the device attaches and `/dev/ttyUSB0` vanishes with a message about
`brltty`, the Ubuntu braille daemon took it: `sudo apt remove brltty`.

Set the terminal to VT100/ANSI, which is what the screen emulates too. Both
keyboards work at once and the same session appears on both. A PC terminal is
usually 80x24 against the screen's 80x37, so full screen software will look
right on whichever size it was configured for. Scrollback and the status block
are on the screen only.

Received characters go into a 4 KB buffer by interrupt, so pasting several
lines works: each waits there until CP/M reads it. The CH340's RTS/CTS lines are
not wired, so the PicoCalc sends XOFF (Ctrl-S) when the buffer is three quarters
full and XON (Ctrl-Q) once it has drained to a quarter. Turn on software flow
control in the terminal program for very long pastes — `picocom -f x`, or
`screen /dev/cu.usbserial-XXXX 115200,ixon`. Without it the first 4 KB still arrive intact.
`srcsim/uartcon.c` replaces the SDK's `stdio_uart` to do this.

### XMODEM over the USB-C port

The console path is not binary safe — it strips bit 7, turns LF into CR and
CRLF, and draws everything on the screen — so file transfers use a second
device on the same line: IMSAI SIO-2 port A at ports 22h/23h, as on z80pack's
IMSAI. It passes all 8 bits through untouched. While a program is using it
the console stops talking to the host (output still appears on the screen),
and the line returns to the console a second after the program stops polling.
Only a write or sustained polling (100 reads with no gap over 10 ms) takes the
line, so a port scan such as `SURVEY.COM` leaves the console alone.
Anything still buffered from the console is discarded when it takes over.

`sd_files/Z80/xfer/` holds Martin Eberhard's `XMODEM.COM` and an `XMODEM.CFG`
for this port, both also on `sd_files/Z80/disks/CPM22.DSK`. The CFG must be on the default
drive. On the host, install lrzsz (`brew install lrzsz`, `apt install lrzsz`)
and start picocom with the XMODEM commands and without `-f x`, since the host
would otherwise swallow 11h and 13h bytes inside the packets:

    picocom -b 115200 --send-cmd "sx -vv" --receive-cmd "rx -vv" /dev/cu.usbserial-XXXX

To send a file to the PicoCalc, type `XMODEM NAME.EXT /R`, then Ctrl-A Ctrl-S
in picocom and type the file name . To fetch one, type
`XMODEM NAME.EXT /S`, then Ctrl-A Ctrl-R and give the name to save it under.
Start the host side within a few seconds of XMODEM, as the emulated CPU runs
its timeouts fast. XMODEM pads files to a multiple of 128 bytes with Ctrl-Z,
as CP/M itself does.

Output is paced to the line rate whether or not anything is attached — about
11,500 characters a second at 115200 baud, which is not noticeable on the
screen. `-DPICOCALC_UART_BAUD=921600` raises it (the CH340 manages 2 Mbaud), and
`-DPICOCALC_CONSOLE_UART=OFF` removes the mirror.

Build with `-DPICOCALC_CONSOLE_USB=ON` to bring up a USB CDC console on the Pico
board's own USB port as well, which is what you want when the display
itself is being debugged.

### Terminal size

The 320x320 panel divides by the font. These are on the card in `/Z80/fonts`;
pick one with `V` in the config dialog by name, or enter nothing for the
built-in:

| Font       | Terminal | Usable rows | Source |
| ---------- | -------- | ----------- | ------ |
| **built-in** | 64x35  | 32          | `5X9`, compiled in |
| `4X8`      | 80x40    | 37          | `tools/mkfont.py` |
| `5X8`      | 64x40    | 37          | X11 5x8, `tools/mkfont5.py` |
| `5X9`      | 64x35    | 32          | X11 5x7 with a row of leading, `tools/mkfont5.py` |
| `5X10`     | 64x32    | 29          | X11 5x8 with a row of leading, `tools/mkfont5.py` |
| `6X8`      | 53x40    | 37          | the original built-in, from upstream |

A fresh config uses the built-in font, and a missing file falls back to it; a
saved `PCALC.DAT` keeps whatever it specifies. The status block at the bottom of the screen takes 3 rows of
each.

The built-in is generated from a font file, so switching it is one command and
a rebuild:

```sh
python3 tools/fnt2h.py sd_files/Z80/fonts/5X9.FNT drivers/default_font.h
```

`6X8` keeps the upstream glyphs for codes 0 to 254, line drawing and symbols
included; the count is a byte in the file header, so code 255 did not fit. The
other fonts hold only 32 to 126, and anything outside a font's range draws its
first glyph, which for those is a space.

`4X8` fits what is written for an 80 column screen, but its glyphs are 3
pixels wide in a 4 pixel cell — `m` against `n`, and `g` against `y`, are the
pairs that suffer. The 64 column fonts are the X11 misc-fixed glyphs, public
domain and 4 pixels wide; the few that used the fifth column (`# $ +`, and in
5x8 also `@ Y m w`) are redrawn in `mkfont5.py` so nothing touches its
neighbour. Rebuilding them needs the X11 fonts installed:

```sh
python3 tools/mkfont.py sd_files/Z80/fonts/4X8.FNT
python3 tools/mkfont5.py sd_files/Z80/fonts
```


## WiFi and network

The Pico Plus 2 W's radio is used when the build has `PICOCALC_WIFI`, which is
on automatically for boards with a CYW43. `N - Network setup` in the config
dialog shows the current state and opens the [WiFi menu](#wifi-menu), the
same box Ctrl+Alt+W opens while the machine runs, which holds every network
setting: the network and its password, a scan to choose one from, connect
at boot, the country code, the NTP server, the UTC offset and the modem init
string. Settings are saved in `PCALC.DAT` as they change. With connect at
boot on, the join runs in the background while the banner and the config
dialog are shown. The status block shows `WiFi...` while joining, then the
address, or `WiFi fail`; the WiFi menu says why.

Every time WiFi comes up, the clock is set from the NTP server plus the UTC
offset. There is no daylight saving rule, so change the offset twice a year.
If the link drops, the machine rejoins; a failed join is retried every 30
seconds, except after a wrong password, and that includes a join the driver
refused outright, which it does while the old link is still being torn
down. Ctrl+Alt+W while the machine runs opens the [WiFi menu](#wifi-menu)
for the cases that does not cover.

The country code is applied when the radio starts, so a change takes effect
from the next boot if the radio is already running. Passwords are stored in
plain text on the card, as in Udo's version, and connections are WPA2 or open.

`PCALC.DAT` now starts with a version header, because the password field grew
from 33 to 63 characters. Files without one are read in the old layout and
rewritten in the new one.

## AT modem

With WiFi built in, the machine has a Hayes style modem that dials TCP hosts
instead of phone numbers. It is the IMSAI SIO-2's second channel, data on port
24h and status on 25h (bit 0 transmitter ready, bit 1 receiver ready), with
carrier detect on port 28h bit 6 — where z80pack's IMSAI has it, so the
communication programs from its disks work. `QT.COM` (QTerm) on
`sd_files/Z80/disks/COMMS.DSK` starts on the modem, so run it and type
commands; its escape key is Ctrl-^. `QT-IMSAI.COM`, also on `CPM22.DSK`, starts
on the USB-C port instead, and Ctrl-^ U then `B` switches it to the modem.

```
ATDT bbs.example.com          dial port 23
ATDT bbs.example.com:6400     or another port
+++                           (a second's pause either side) command mode
ATH                           hang up
ATO                           back online
```

Dialling never stalls the CPU: `ATD` answers `CONNECT` when the connection is
made, `BUSY` if it is refused, `NO ANSWER` if the host does not exist or does
not respond within S7 seconds, and `NO DIALTONE` with no WiFi. Any key while
dialling gives up. When the far end hangs up, waiting data is delivered and
then `NO CARRIER`. `T` or `P` straight after `D` is taken as the dial
modifier, as on a Hayes, so use `ATDT` before a host that starts with either.

Telnet negotiation is on by default (S15=1): when the host asks, the modem
reports the screen size and terminal type `ANSI` and agrees to binary mode, so
XMODEM works through it.
`ATS15=0` gives a plain TCP connection. While connected, an asterisk after the
IP address in the status block shows carrier.

| Command | |
| --- | --- |
| `ATDhost[:port]` `ATH` `ATO` `ATZ` `AT&F` `A/` | dial, hang up, online, reset, defaults, repeat |
| `ATE0/1` `ATQ0/1` `ATV0/1` | echo, quiet, words or numeric codes |
| `ATSn=v` `ATSn?` | S2 escape `+`, S7 connect wait 30, S12 guard 50 (1/50 s), S15 telnet, S16-S19/S22 telnet options, S20/S21 window size |
| `ATI0` `ATI1` | version, network state |
| `AT+T="name"` `AT+T?` | telnet terminal type |
| `AT&A` / `AT&A1` | listen on port S14, `1` also quietly, see below |
| `ATA` `ATS0=n` | answer, or answer on ring n |
| `AT$` | help |
| `AT+WIFI?` | WiFi state and saved network |
| `AT+WIFI=SCAN` | list networks, numbered |
| `AT+WIFI=n,"password"` | join network n from the scan |
| `AT+WIFI="ssid","password"` | join by name |
| `AT+WIFI=UP` / `DOWN` | rejoin the saved network, or leave |
| `AT+WIFI=AUTO,1` / `0` | connect at boot on or off |

`AT+WIFI` commands answer `OK` at once and report `SCAN DONE`,
`WIFI CONNECTED address` or `WIFI FAILED: reason` when they finish; a
successful join is saved to `PCALC.DAT`. Quoted arguments keep their case. `X`,
`M`, `L`, `&C`, `&D`, `&K` and similar are accepted and ignored, so common init
strings work.

### Answering calls

`AT&A` listens for callers on TCP port S14 (8023 by default; `ATS14=n&A`
for another). As in z80pack, any number after it listens too, and `AT&A1`
also turns echo and result codes off and ignores `+++` while online, for a
BBS that owns the line. The status block then shows the port after the
address — `192.168.1.42:8023` — which is what a caller types into telnet,
or `ATDT 192.168.1.42:8023` on another of these machines.

A caller produces `RING` every three seconds. `ATA` answers with `CONNECT`;
`ATS0=n` answers by itself on the nth ring; `ATH` turns the caller away, and
so does ringing unanswered fifteen times. While a call is up, other callers
are refused as if the line were busy. Carrier detect, port 28h bit 6, is set
only once the call is answered, so BBS software that waits for it works.

With telnet on, the answering modem offers to echo and to suppress go-ahead,
which puts the caller's telnet into character mode with CP/M doing the
echoing, and a caller's Enter (CR LF or CR NUL) arrives as a single CR.
`ATI1` shows who is calling or connected.

The modem init string runs whenever the machine starts and on every `ATZ`,
after the modem has dropped any call and stopped listening. The firmware's
is empty, so a plain boot neither listens nor answers; BBS software sets
that up itself, as RBBS's BYE does with `ATZ` and then `S0=1` and `&A0`. For
a machine that answers at boot without such a program, put `S0=1&A0` in the
modem init string in the WiFi menu, with connect at boot on; listening
starts as soon as WiFi does. There is no command to stop listening short of
an init string without `&A` and `ATZ`. Callers on the same network can reach it directly; from the
internet the router has to forward the port.

### The comms disk and the CP/M devices

`sd_files/Z80/disks/COMMS.DSK` is Udo's IMSAI comms disk, rebuilt by
`tools/make-comms-disk.sh` with `XMODEM.ASM` dropped for room and the files in
`cpm/comms/` added: a `README.TXT` for the PicoCalc and an `XMODEM.CFG` that
uses the modem. `QT.COM` is `QT-IMSAI.COM` with its four port operands changed
from port A to port B.

The BIOS in `cpm/` follows the IOBYTE with the same assignments as z80pack's
IMSAI BIOS, which is what Kermit's `SET PORT UC1` and XMODEM's `/X1` expect:

| Device | TTY: CRT: | | |
| --- | --- | --- | --- |
| `CON:` | console | `BAT:` RDR: in, LST: out | `UC1:` modem |
| `RDR:` | console (also `UR2:`) | `PTR:` USB-C, the default | `UR1:` modem |
| `PUN:` | console (also `UP2:`) | `PTP:` USB-C, the default | `UP1:` modem |
| `LST:` | console | `LPT:` none, the default | `UL1:` USB-C |

`STAT RDR:=UR1:` and `STAT PUN:=UP1:` put the reader and punch on the modem
for `PIP` or `XMODEM /X1`. USB-C here is SIO-2 port A, the raw 8 bit device,
not the console mirror. On boards without WiFi, or with the console UART
built out, the missing ports read as a line with nothing attached rather than
as 0FFh, so nothing hangs waiting for a character that never stops coming.

In hostsim, Kermit dialled out through `SET PORT UC1`, QTerm dialled, hung up
with Ctrl-^ `,` and went back to command mode, and `XMODEM /R` received a
3000 byte random file over the modem byte for byte. QTerm's hang-up delays
are timing loops calibrated for the 4 MHz default, so `HOSTSIM_MHZ=4` paces
hostsim the same way; at unlimited speed `+++` loses its guard time.

XMODEM 2.7 accepts `/Z1` to `/Z6` only, so both configs use `/Z6`.

`tools/hostsim` has the same modem over the PC's own network, and
`tools/hostsim/modemtest` is a bare terminal onto it for trying AT commands
without a Z80.

## Updating from the device

`U - Update PicoCalc-Z80` on the main menu of the WiFi builds downloads a
release from the site the firmware came from.

```
u - set update URL     c - check for update     x - exit
```

The URL is the repository page, (e.g., `https://github.com/sg/picocalc-z80`) and is configurable, and the
value is saved in `PCALC.DAT`.

`C` prints the installed firmware (`USR_REL`) and the card's `/Z80/VERSION.TXT`,
fetches the latest tag and compares the three with `srcsim/version.c`: a
development build counts as its base release plus the commit count, so
`v0.1.6-3-g93260a6+` is older than `v0.1.7`, and anything that is not a version
is older than anything that is. If the firmware or the card is behind,
`U - Update to version` is offered, behind a warning that files under `/Z80`
are overwritten and that changed disk images are backed up first. Then, in
order:

1. `SHA256SUMS.txt` for the tag is fetched into memory.
2. `picocalc-z80-<board>-<tag>.uf2` is streamed into `/pico2-apps/`
   (`/pico1-apps/` on an RP2040) under a temporary name, hashed as it is
   written, checked against the sums file and only then renamed. That is the
   directory pelrun's uf2loader reads: power on holding the Up Arrow and pick
   the file. Nothing is flashed from inside, since both firmwares link at the
   same address. Older `picocalc-z80-<board>-*.uf2` files there are offered
   for deletion one at a time, default keep, so a rollback stays one boot away.
3. `picocalc-z80-sd-files-<tag>.tar.gz` is staged in `/Z80/xfer/`, hashed and
   checked the same way, then extracted by `srcsim/untar.c` through uzlib.
   Every entry has to be under `Z80/`, a `PCALC.DAT` would be skipped if an
   archive ever carried one, and plain files are overwritten. A disk image that
   is already on the card is written to a temporary name and compared as it is
   inflated: identical means the user never wrote to it and the copy is
   dropped; different means the old image is renamed to its first five
   characters plus `-NN` and the extension (`CPM22-01.DSK`, `XYBAS-01.DSK`,
   counting past earlier backups) and the release's copy takes the name.
   Choosing a drive in the config dialog lists the backups with the rest, and
   the drive menu offers them too. The archive is deleted on success and
   left in `/Z80/xfer/` on failure.
4. A failed firmware download stops before the card is touched. A failed
   extraction still prints where the firmware went, because that part is good.

While a transfer runs the status block shows the byte count after the address,
`192.168.1.123  319488/1265152`, or the count alone when the line is narrow.

The client, `srcsim/http.c`, follows netsock.c's split: core 0 owns the altcp
connection and copies what arrives into a ring, holding a pbuf back while the
ring is full so that TCP flow control follows the SD card; core 1 sends the
request, parses the reply and feeds a sink. It follows redirects across hosts,
decodes chunked bodies and gives up after 15 s without a connection or 30 s
without data. Its buffers, and the extractor's 32 KB inflate window, come out
of `bnk0` and `bnk1`, which are idle while the config dialog runs; leaving the
update menu runs `init_memory()` again, so a program loaded with `L` before an
update is gone rather than corrupt.

HTTPS is built on the RP2350 boards (`PICOCALC_TLS`): mbedtls under lwIP's
altcp layer, TLS 1.2 with SNI, no certificate verification yet, with the
SHA-256 sums standing in for integrity. A GitHub redirect reply carries 5 KB of
headers and a 900-byte signed location, which is what sizes the header and URL
buffers. The Pico W refuses an `https://` URL with `HTTPS needs a Pico 2
board`, since mbedtls does not fit beside the machine; its HTTP path is the
same code with a smaller pbuf pool, reviewed and built but not yet run on a
board.

## When something crashes

`srcsim/crash.c` keeps a record in RAM that the runtime does not clear. A hard
fault on either core saves the faulting PC, LR and SP and, on the M33, the
fault status registers; a panic, which is where lwIP's and the SDK's
assertions end up (`PICO_PANIC_FUNCTION`), saves its message; both restart the
board through the watchdog. The HTTP client also arms the watchdog for 8 s
around every transfer, fed from core 0's idle loop, so a hang inside lwIP's
callbacks ends in a restart rather than a frozen screen. The next boot prints
a line starting with `***` under the banner saying which it was. A PC maps to
a source line with `arm-none-eabi-addr2line -e build-<board>/picocalc-z80.elf
<pc>` from the same build.

`-DPICOCALC_STACK_WATERMARK=ON` paints core 0's stack at boot and prints how
much of it was never touched after an update check; see [Footprint](#footprint)
for what it found.

## Building

Ready-made UF2s for each board and the SD card archive are on the
[releases page](https://github.com/sg/picocalc-z80/releases). To build your
own you need an `arm-none-eabi` toolchain, CMake and the Pico SDK 2.3.1 or
later (`PICO_SDK_PATH`, default `../pico-sdk`); CMake stops on
an older one. z80pack (dev branch),
pico_fatfs and uzlib are git submodules:

```sh
git clone --recurse-submodules https://github.com/sg/picocalc-z80.git
# or, in an existing clone: git submodule update --init
```

`build.sh` builds one board or all of them, each in its own `build-<board>`
directory, and copies `picocalc-z80-<board>-<version>.uf2` to `$DEPLOY`
(default `dist/` in the checkout):

```sh
./build.sh pim2w             # one board
./build.sh all               # every board
./build.sh pico2w v0.2.0     # version for the file name and banner
./bld-picocalc.sh            # shortcut for: ./build.sh pim2w dev
```

| Board | `PICO_BOARD` | WiFi and modem | Notes |
| ----- | ------------ | -------------- | ----- |
| `pico` | `pico` | no | RP2040 |
| `picow` | `pico_w` | yes, no HTTPS | RP2040: half the scrollback (256 lines) and fewer lwIP buffers, to fit 264 KB |
| `pico2` | `pico2` | no | RP2350 |
| `pico2w` | `pico2_w` | yes | RP2350 |
| `pim2w` | `pimoroni_pico_plus2_w_rp2350` | yes | RP2350B, 16 MB flash, 8 MB PSRAM |

The version defaults to `git describe`, so a tagged checkout reads `v0.2.0`,
later commits `v0.2.0-5-g9e11580`, and uncommitted changes add a `+`. The boot
banner shows it. WiFi is on automatically for boards with a CYW43 radio; the
submodule paths are CMake cache variables (`-DZ80PACK=`, `-DPICO_FATFS=`) if
you want other checkouts.

`PICOCALC_TLS` is on for the RP2350 WiFi boards. It links the SDK's mbedtls
with `mbedtls_config.h` from the project root, and moves core 0's stack from
the SDK's 4 KB scratch bank into main RAM, where it can be 16 KB. `build.sh` also passes the short board
name in, which the update client needs to name its own release asset.

## Releases

Pushing a version tag builds every board with GitHub Actions and stages a
draft release:

```sh
git tag v0.2.0
git push origin v0.2.0
```

`.github/workflows/release.yml` checks out the tag with its submodules on
Ubuntu 24.04, with Ubuntu's Arm GCC and Pico SDK 2.3.1 with picotool
(`.github/actions/pico-toolchain`, cached between runs), runs
`./build.sh all <tag>` and `make-sd-archive.sh`, and attaches the five `.uf2`
files, the SD card archive and `SHA256SUMS.txt` to a draft whose notes list
the commits since the previous tag. Review it under Releases and publish it
by hand; the update menu only sees published releases. An existing tag can
be rebuilt from the Actions tab (Release, Run workflow), optionally as a
pre-release; delete the old draft first.

Every push to `main` and every pull request also builds the five boards
(`.github/workflows/build.yml`) and keeps the UF2s as the run's artifacts.
`ci/builder` is a Docker image with the same toolchain, for a self-hosted
runner.

## Moving files in and out of CP/M

`/Z80/xfer/` is a shared directory that CP/M can read and write, using
z80pack's `R.COM` and `W.COM` against a host-BDOS hook on port 161
(`srcsim/hostbdos.c`, Mike Douglas's `simbdos.c` ported to FatFs):

```
A>R HELLO.TXT          copy /Z80/xfer/HELLO.TXT onto the current drive
A>R B:HELLO.TXT        ... onto drive B instead
A>W HELLO.TXT          copy HELLO.TXT from CP/M out to /Z80/xfer/
A>W B:HELLO.TXT        ... from drive B
```

Files are copied verbatim unless the extension is one of the text types
(`ASM`, `TXT`, `MAC`, `SUB`, `HEX` and friends), in which case the copy stops
at the first ctrl-Z, the way CP/M marks the end of a text file. Names are 8.3
and upper case on both sides.

CP/M reaches nothing outside `xfer`, so a program cannot open the disk images
it is running from.

### Getting R.COM and W.COM onto a disk

`sd_files/Z80/disks/CPM22.DSK` already has both, so copying `sd_files/Z80` to
the card is enough to start. Udo's original stays untouched in
`submodules/z80pack/picosim/disks/cpm22.dsk`.

For any other image it is a one-time step on a PC, because the two programs
are what moves files and so cannot move themselves:

```sh
./tools/install-xfer.sh some.dsk
```

That assembles both out of `submodules/z80pack/cpmtools` and writes them into the image
with `tools/cpmdisk.py`, which also does `list`, `get` and `era` if you want to
look inside an image, pull files out or make room without a running machine.
`get` takes names or quoted patterns and writes the files under their disk
names, into `-o DIR` if given. It follows the same IBM 8" sector skew the BIOS
uses, so images stay readable by CP/M.

### User areas

CP/M keeps a file's user area in the first byte of its directory entry, and
Z-System leans on that heavily — the `A0:` in the NZ-COM prompt is drive A,
user 0. Everything defaults to user 0; `-u N` picks another, and `get` also
takes the ZCPR spelling:

```sh
python3 tools/cpmdisk.py put  disk.dsk WORK.TXT -u 3
python3 tools/cpmdisk.py get  disk.dsk 3:WORK.TXT
python3 tools/cpmdisk.py get  disk.dsk '*.COM' 3:'*.TXT' -o pulled
python3 tools/cpmdisk.py list disk.dsk            # every area, with a column
python3 tools/cpmdisk.py list disk.dsk -u 3       # just that one
```

The same name in two areas is two different files, and `put` only replaces
within the area it is writing to. Free space is not divided up that way,
though: blocks are allocated across the whole disk, so a file in user 3 takes
room away from user 0 as well, which is why `list` reports one figure.

The card is read over SPI0 (MISO 16, CS 17, SCK 18, MOSI 19, card-detect 22).
A missing card is not fatal — the configuration dialog still runs, without disks.

Disk images are held open across sector accesses. The FDC moves one 128 byte
sector at a time, and reopening the image for each of those meant walking
`/Z80/disks` from the root every time, which costs several sector reads of its
own. A failed access closes the drive so the next one reopens it, and writes
are followed by `f_sync()` so that a card pulled mid-session loses at most the
sector in flight.

The card runs at 25 MHz, the most SPI mode is specified for, with CRC checking
on in both directions (`sdcard/`, pico_fatfs's driver with the checks added).
pico_fatfs's default asks for 50 MHz, which comes out at 37.5 MHz on an RP2350,
and its driver discarded the card's CRCs.
Here a misread would not stay a misread: a 128 byte CP/M sector write makes FatFs read the
512 byte card sector around it and write all of it back, CP/M rewrites the
directory sectors it reads, and the track cache keeps a bad track for the
session. A block that fails its CRC is read or written again, up to three
times in a row, before the FDC sees an error. A card that refuses CRC checking
says so at boot.

## What came from where

Files under `srcsim/` are the platform layer. These are compiled straight out
of the `submodules/z80pack` checkout and never modified:

- `z80core/` — both CPU cores, disassembler, ICE
- `iodevices/sd-fdc.c` — the disk controller, needs only `read_sec()`,
  `write_sec()` and `get_fdccmd()` from `srcsim/disks.c`
- `iodevices/rtc80.c` — the emulated machine's clock
- `iodevices/libtelnet.c` — telnet negotiation for the modem

So is `submodules/uzlib`, Paul Sokolovsky's uzlib (zlib licence): `tinflate.c`,
`tinfgzip.c`, `crc32.c` and `adler32.c` inflate the update's card archive.
`submodules/te`
and `submodules/mescc` are Miguel Garcia's TE editor and the MESCC compiler
it is built with, both GPL; only `cpm/te/tepcalc.c` is ours.

Borrowed and reworked:

- `drivers/` — LCD, keyboard and terminal from picocalc_lua, see `drivers/README.md`
- `srcsim/wifi.c`, `srcsim/netsock.c` — from `wifi.c` and `socket.c` in my other projects, 
made non-blocking and moved to core 0, and NTP after Udo's `net_ntp.c`
- `srcsim/modem.c` — after David McNaughton's `generic-at-modem.c` in z80pack
- `srcsim/hostbdos.c` — Mike Douglas's `simbdos.c` ported to FatFs
- `sdcard/` — pico_fatfs's SPI card driver with CRC checks and retries, see
  `sdcard/README.md`
- `mbedtls_config.h` — my code, based on the SDK's kitchen_sink example, with 
the record sizes and the allocator changed

Written here: `srcsim/http.c`, `update.c`, `untar.c`, `sha256.c`,
`version.c` and `crash.c`.

### Local changes worth knowing about

- **`srcsim/rtc_compat.c`** — RP2350 dropped the RP2040 calendar RTC block, so
  pico-sdk gates `hardware_rtc` behind `HAS_RP2040_RTC`. On RP2350 this
  reimplements `rtc_set_datetime()` / `rtc_get_datetime()` over
  `pico_aon_timer`; on both parts it overrides the SDK's weak
  `_gettimeofday()` so that `rtc80.c`, which reads the clock through `time()`,
  agrees with what the config dialog shows.
- **`fatfs_conf/ffconf.h`** — a copy of pico_fatfs's FatFs config placed ahead of
  it on the include path, so the submodule stays pristine. Enables `FF_USE_FIND`
  (needed by the `/Z80/code` and `/Z80/disks` listings) and swaps code page 932
  for 437.
- **`srcsim/simio.c`** — port 0 out drove the Waveshare RGB LED; the PicoCalc has
  none, so the port is unhandled and writes are ignored. Console status and
  input go through `getchar_timeout_us(0)` rather than TinyUSB or the UART
  directly, so the same code serves the screen, a serial terminal, or both.
  Enter arrives from the PicoCalc keyboard as LF and is mapped to CR, which is
  what CP/M and `get_cmdline()` expect.
- **`srcsim/simcfg.c`** — `V` selects the console font, `K` switches the arrow
  keys between ANSI and WordStar, `N` opens the network menu and `U` the update
  menu. `PCALC.DAT`
  starts with a `PC80` magic and version; a file without one is read in Udo's
  layout and rewritten.
- **`srcsim/simcfg.c`** — the LCD brightness menu entry is gone until it can be
  wired to the keyboard controller's backlight register. The field is still
  written to `PCALC.DAT` so the format does not have to change later.

## Licence

MIT, see `LICENSE`. The parts that come from elsewhere keep their own
licences: the z80pack sources and the files adapted from them Udo Munk's MIT
licence, uzlib the zlib licence and pico_fatfs its BSD-style licence. TE and
MESCC are GPLv2, and so is the TE on `TE.DSK`, whose source is
`submodules/te` and `cpm/te`. The software on the disk images in
`sd_files/Z80/disks` is distributed under the terms its authors released it
with.
