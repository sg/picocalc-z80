/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Scrollback for the PicoCalc console.
 * Copyright (C) 2026 Steve Gibson
 *
 * The panel cannot be read back, so history is kept as a shadow copy of the
 * text: drivers/term.c reports every character it draws and every erase, and
 * this records them by absolute terminal row. Scrolling back repaints the
 * text area out of that shadow, into the same window the live text occupies,
 * so the hardware scroll register is never disturbed.
 *
 * While the view is off the bottom the machine is held off by reporting its
 * console transmitter busy, which is what keeps the two from fighting over
 * the screen: no output arrives, so nothing is drawn underneath the history.
 * The machine polls that status in a loop, and srcsim/simio.c pumps the
 * keyboard from the same place, so the keys that end scrollback still get
 * through while it is stalled.
 */

#ifndef SCROLLBACK_INC
#define SCROLLBACK_INC

#include <stdbool.h>

#include "keyboard.h"

#if PICOCALC_SMALL_RAM
#define SB_LINES	256	/* RP2040 with WiFi has no room for more */
#else
#define SB_LINES	512	/* rows of history, 40 KB at SB_COLS */
#endif
#define SB_COLS		80	/* the narrowest font gives 80 columns */

extern void scrollback_init(void);
extern void scrollback_enable(bool on);
extern void scrollback_clear(void);

/* true while the view is off the bottom, so the machine must wait */
extern bool scrollback_active(void);
extern int scrollback_offset(void);
/* back to the live screen, for something that is about to draw on it */
extern void scrollback_leave(void);

/* returns true if the event was a navigation key and has been consumed */
extern bool scrollback_key(const input_event_t *ev);

#endif /* !SCROLLBACK_INC */
