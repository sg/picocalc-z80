/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The WiFi menu: Ctrl+Alt+W over the running machine, for getting back on
 * the network without a reboot when the link has dropped, or for moving to
 * another network. It runs on core 1 with the CPU stopped, like the drive
 * menu, while the radio and lwIP carry on on core 0, so the status in the
 * box follows a join as it happens.
 */

#ifndef WIFIMENU_INC
#define WIFIMENU_INC

/* show the menu until it is closed; the CPU loop must be stopped */
extern void wifi_menu(void);

#endif /* !WIFIMENU_INC */
