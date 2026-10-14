/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Status block along the bottom of the PicoCalc screen: a blank separator
 * row, then a line about the machine, then a line about the drives.
 *
 * The row is taken from the terminal rather than from the panel's hardware
 * scroll regions: drivers/term.c writes into a 480 line ring and picks the
 * visible 320 line window with the scroll register, so a hardware fixed area
 * would mean reworking that arithmetic. Instead the terminal is told it has
 * one row fewer, and this repaints the row it no longer uses.
 */

#ifndef STATUS_INC
#define STATUS_INC

#include <stdbool.h>

/* separator plus two lines, taken off the bottom of the terminal */
#define STATUS_ROWS 3

extern bool status_enabled;

extern void status_init(void);
extern void status_paint(void);
/* called from the core 0 idle loop, where a slow I2C read is harmless */
extern void status_poll(void);

#endif /* !STATUS_INC */
