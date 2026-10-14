/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Translates PicoCalc key codes into the byte stream a terminal would send.
 *
 * The keyboard controller reports its own codes: ESC is 0xb1 rather than 0x1b,
 * the arrows are 0xb4-0xb7 rather than cursor sequences, and Enter is LF. This
 * registers an stdio driver that keeps drivers/term.c for output but replaces
 * its input half, so everything reading the console - the machine's SIO, the
 * config dialog, get_cmdline() - sees the same corrected stream.
 */

#ifndef KEYMAP_INC
#define KEYMAP_INC

/*
 * What the arrow and paging keys send. Almost no CP/M software reads ANSI
 * cursor sequences, but the WordStar diamond is what its editors, Turbo
 * Pascal and most of their descendants actually listen for.
 */
#define KEYMAP_ARROWS_ANSI	0
#define KEYMAP_ARROWS_WORDSTAR	1

extern int keymap_arrows;

extern void keymap_init(void);

#endif /* !KEYMAP_INC */
