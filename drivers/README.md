# Vendored PicoCalc drivers

Copied from [Lana-chan/picocalc_lua](https://github.com/Lana-chan/picocalc_lua)
at `b0849f7`, which in turn credits
[hisptoot/picocalc_luckfox_lyra](https://github.com/hisptoot/picocalc_luckfox_lyra)
for the LCD and keyboard work and
[BlairLeduc/picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter)
for the keyboard ring buffer.

Not copied: `fs.c`/`fs.h` (this project mounts the card in `srcsim/disks.c`),
`sound.c`, `wifi.c`, `socket.c`, `tls.c`.

## Local changes

Kept deliberately small so upstream fixes stay easy to merge.

### `lcd.c`

- The PSRAM framebuffer mode is behind `PICOCALC_LCD_PSRAM_FB`, undefined by
  default. Upstream initialises polpo/rp2040-psram unconditionally in
  `lcd_reset_pio()`, which claims PIO0 SM0 plus four GPIOs for the PicoCalc
  mainboard's own PSRAM, a chip on ordinary SPI pins that is only reachable
  by explicit transfers and slow; this project does not use it. Defining the
  mode again would need that submodule back. The memory this project does
  use is the Pimoroni board's QSPI PSRAM, see `srcsim/psram.c`.
- Dropped `#include "fs.h"`; it was unused, and this project must not have a
  second owner of the SD card.
- `#include "../pico_fatfs/fatfs/ff.h"` is now `"ff.h"`. The relative path
  only resolved through pico_fatfs's root on the include path, which the
  build no longer adds now that the card driver lives in `sdcard/`; for the
  same reason `drivers` no longer links `pico_fatfs`.

### `keyboard.c`

- `on_keyboard_timer()` held the key code in a plain `char`. Key codes run past
  0x7f (ESC 0xb1, the arrows 0xb4-0xb7, the modifiers 0xa1-0xa5), so whether
  `keystates[]` is indexed out of bounds and whether `KEY_*` comparisons match
  hinges on `char` being unsigned. It is on ARM EABI, so this was latent rather
  than live, but the types are now explicitly `unsigned char`.
- `keyboard_init()` is declared `int` and fell off the end without returning.
- Added `keyboard_set_chord_callback()`. The timer already acts on Ctrl+Alt+F1
  and Ctrl+Alt+Del itself; any other key released with exactly Ctrl and Alt
  held now goes to that callback, and while the two are held the key is not
  queued for whatever is reading the keyboard, so the machine never sees the
  chord typed. `srcsim/picosim.c` uses it for Ctrl+Alt+D, the drive menu.

### `term.c`

- `stdio_picocalc_in_chars()` tested `event.code < 'z'` when folding Ctrl+letter
  to a control code, so Ctrl-Z arrived as a literal `z`. CP/M uses 0x1a as its
  end-of-file marker, so this mattered. Now `<= 'z'`.
- `out_char()` treats form feed as clear-screen. It is what the boot banner and
  a lot of CP/M-era software send for that.
- Bare carriage return returns to column 0; upstream has that case commented
  out. `\n` already implied CR here so ordinary output was fine either way, but
  a lone `\r` is how software overwrites a line in place, and full screen
  editors do it constantly.

- Added `term_get_scroll()`.
- Added `lcd_scroll_area()`, which sets the controller's vertical scroll
  definition so only the top `height` lines scroll and the rest of the panel
  stays fixed; `srcsim/status.c` keeps the status block there. The terminal
  wraps its rows through `lcd_scroll_height` rather than the full 480 lines,
  clears only that much on a clear screen, and a scroll now erases the rows it
  brings into view instead of the one after them, since with the scroll area
  exactly one screen tall those are the rows just leaving the top.

- Added `term_set_observer()`, reported from the four places that change what
  is on screen: a character drawn, the screen cleared, a row cleared, and a row
  cleared from the cursor rightwards. `srcsim/scrollback.c` keeps its shadow
  copy of the text from these, because the panel cannot be read back.
- `term_set_pos()` set `ansi.y` to the row it was given, but `ansi.y` is an
  absolute row in the 480 line ring everywhere else, while a cursor address
  from the machine is relative to the top of the screen. The two only agreed
  before the screen had scrolled, so `ESC [ 10 ; 5 H` landed ten rows down the
  ring rather than ten rows down the screen — full screen software drew in the
  wrong place as soon as anything had scrolled past. It now adds `ansi.scroll`.

- The blinking cursor is a block: the cell under it drawn in inverse video,
  put back as it was when it blinks off. The panel cannot be read back, so
  `term.c` keeps a shadow of every cell in the scroll ring, three bytes each
  (the character and two indices into a colour table), allocated to fit the
  font and the scroll area whenever `term_reset()` runs. A character drawn or
  erased over the cursor marks it hidden, so the next blink-off does not paint
  the old cell back over the new one. `term_hold_cursor()` keeps it off the
  screen while scrollback shows history in its place.
- `term_clear()` no longer restarts the row numbering. It moves the view down
  by a whole screen, blanking the rows that come in as a scroll does, so the
  scrollback keeps what a program cleared away; `term_reset()` is the restart,
  for a change of geometry, and the first clear before any reset goes there.
- Added the overlay: `term_overlay_open()`, `_fill()`, `_text()` and
  `_close()`. A box in screen cells drawn straight onto the panel for
  something that is not the machine's output, the drive menu. It leaves the
  shadow alone, so closing it is a repaint of the box from the shadow and the
  text underneath comes back exactly as it was, colours included. It holds the
  cursor while open and clips anything outside the box. The caller has to
  keep the machine quiet meanwhile, which `srcsim/drivemenu.c` does by
  running with the CPU loop stopped.

## Input translation lives elsewhere

`term.c` still registers `stdio_picocalc` with both an output and an input
half, but this project only uses the output half. `srcsim/keymap.c` registers
its own stdio driver that pairs `stdio_picocalc_out_chars()` with a translating
reader, because the emulated machine wants a terminal's byte stream — ESC as
`0x1b`, cursor sequences, control codes — while the keyboard controller reports
its own codes and the Lua REPL upstream wants those raw.

- SGR accepts the 24 bit forms `38;2;r;g;b` and `48;2;r;g;b`, so text can
  match a colour drawn directly, such as the status block's green. The
  parameter buffer grew from 16 to 32 bytes to hold them, and no longer writes
  its terminator one past the end when full.
