/*
 * Host build of the PicoCalc machine.
 *
 * The point of this is the BIOS: a bad disk parameter block or a bad SELDSK
 * does not fail at build time, it fails as a machine that will not boot. This
 * runs the same z80pack core, the same memory map and the same disk geometry
 * on a PC, so a change can be booted and tried before it reaches a card.
 *
 *   hostsim disk-a.dsk [disk-b.dsk ...] [-hd disk.hdd]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"
#include "simcore.h"
#include "simio.h"
#include "hostdisks.h"
#include "modem.h"

static struct termios saved_tty;

static void tty_restore(void)
{
	tcsetattr(0, TCSANOW, &saved_tty);
}

static void tty_raw(void)
{
	struct termios t;

	/* the console poll must never block, whether stdin is a tty or a pipe */
	fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);

	if (tcgetattr(0, &saved_tty) != 0)
		return;
	atexit(tty_restore);
	t = saved_tty;
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &t);
}

int get_cmdline(char *buf, int len)
{
	int i = 0;
	int c;

	for (;;) {
		while (read(0, &c, 1) != 1)
			;
		c &= 0xff;
		if ((c == '\r') || (c == '\n'))
			break;
		if (i < len - 1) {
			buf[i++] = c;
			putchar(c);
			fflush(stdout);
			if (len == 2)
				break;
		}
	}
	buf[i] = '\0';
	putchar('\n');
	return 0;
}

int main(int argc, char **argv)
{
	int i, drive = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-hd") == 0 && i + 1 < argc)
			strncpy(disks[HD_SLOT], argv[++i], 255);
		else if (drive < NUMDISK)
			strncpy(disks[drive++], argv[i], 255);
	}
	if (!disks[0][0]) {
		fputs("usage: hostsim disk-a.dsk [...] [-hd disk.hdd]\n", stderr);
		return 1;
	}

	tty_raw();

	init_cpu();
	PC = 0xff00;		/* power on jump into the boot ROM */
	init_memory();
	init_io();
	modem_init();

	f_value = 0;		/* run flat out */
	tmax = 100000;

	run_cpu();

	putchar('\n');
	report_cpu_error();
	return 0;
}
