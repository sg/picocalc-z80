/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * A record of how the last run ended, kept in RAM across a reset. A hard
 * fault or a panic on either core saves what it knows and restarts the
 * board through the watchdog, and a transfer that hangs core 0 is cut
 * short by the watchdog itself; the boot banner then reports which it was.
 */

#ifndef CRASH_INC
#define CRASH_INC

/* print and clear the record of the last run, if there is one */
extern void crash_report(void);

/* arm the watchdog around a network transfer; core 0 feeds it */
extern void crash_watch_begin(void);
extern void crash_watch_feed(void);
extern void crash_watch_end(void);

#endif /* !CRASH_INC */
