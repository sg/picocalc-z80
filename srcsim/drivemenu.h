/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The drive menu: a box over the running machine's screen, opened with
 * Ctrl+Alt+D, for changing the disk images in the drives the way disks are
 * swapped on a real machine.
 *
 * It runs on core 1 with the CPU loop stopped, so FatFs and the drive tables
 * are only ever touched from the core that owns them and nothing prints while
 * the box is up; term.c puts the text underneath back from its shadow when
 * the box closes, and picosim.c starts the CPU again where it stopped.
 */

#ifndef DRIVEMENU_INC
#define DRIVEMENU_INC

/* show the menu until it is closed; the CPU loop must be stopped */
extern void drive_menu(void);

#endif /* !DRIVEMENU_INC */
