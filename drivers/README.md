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
  `lcd_reset_pio()`, which claims PIO0 SM0 plus four GPIOs for a chip this
  board does not carry. Defining it again would need that submodule back.
- Dropped `#include "fs.h"`; it was unused, and this project must not have a
  second owner of the SD card.

### `keyboard.c`

- `on_keyboard_timer()` held the key code in a plain `char`. Key codes run past
  0x7f (ESC 0xb1, the arrows 0xb4-0xb7, the modifiers 0xa1-0xa5), so whether
  `keystates[]` is indexed out of bounds and whether `KEY_*` comparisons match
  hinges on `char` being unsigned. It is on ARM EABI, so this was latent rather
  than live, but the types are now explicitly `unsigned char`.
- `keyboard_init()` is declared `int` and fell off the end without returning.

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

- Added `term_get_scroll()` and `term_reserve_bottom()`. A scroll erases the
  row below the last text row, which is where `srcsim/status.c` keeps the
  status block; once reserved, that row is left to its owner.
- Added `lcd_set_scroll_hook()` and `lcd_get_scroll()`. The hook runs on core 0
  either side of the scroll register write, so the status block can be redrawn
  in step with the scroll rather than racing it from the other core.

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

- The blinking cursor is a one pixel bar on the left edge of its cell, and
  blinking off fills that column at the remembered position. A character typed
  into the cell overwrote the bar, but the cursor stayed marked visible, so the
  next blink-off erased the left column of the new glyph. `out_char()` now marks
  the cursor hidden when it draws over it.

## Input translation lives elsewhere

`term.c` still registers `stdio_picocalc` with both an output and an input
half, but this project only uses the output half. `srcsim/keymap.c` registers
its own stdio driver that pairs `stdio_picocalc_out_chars()` with a translating
reader, because the emulated machine wants a terminal's byte stream — ESC as
`0x1b`, cursor sequences, control codes — while the keyboard controller reports
its own codes and the Lua REPL upstream wants those raw.
