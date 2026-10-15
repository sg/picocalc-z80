/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Status block along the bottom of the PicoCalc screen: a blank separator
 * row, then a line about the machine, then a line about the drives.
 *
 * The rows are taken off the bottom of the terminal, and the panel's scroll
 * area is set to end where they begin, so the controller keeps them fixed on
 * screen and a scroll never has to redraw them.
 */

#ifndef STATUS_INC
#define STATUS_INC

#include <stdbool.h>

/* P1 phosphor, the green of the terminals this machine is pretending to be */
#define STATUS_GREEN		51, 255, 51
#define STATUS_GREEN_SGR	"\033[38;2;51;255;51m"

/* separator plus two lines, taken off the bottom of the terminal */
#define STATUS_ROWS 3

extern bool status_enabled;

extern void status_init(void);
extern void status_paint(void);
/* called from the core 0 idle loop, where a slow I2C read is harmless */
extern void status_poll(void);

#endif /* !STATUS_INC */
