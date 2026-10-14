/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk
 *
 * I/O simulation for the PicoCalc
 *
 * History:
 * 28-APR-2024 all I/O implemented for a first release
 * 09-MAY-2024 improved so that it can run some MITS Altair software
 * 11-MAY-2024 allow us to configure the port 255 value
 * 27-MAY-2024 moved io_in & io_out to simcore
 * 31-MAY-2024 added hardware control port for z80pack machines
 * 08-JUN-2024 implemented system reset
 * 09-JUN-2024 implemented boot ROM
 * 29-JUN-2024 implemented banked memory
 * 15-AUG-2026 derived from the Pico-Eval-Board port for the PicoCalc
 */

/* Raspberry SDK includes */
#include <stdio.h>
#include "pico/stdlib.h"

/* Project includes */
#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"
#include "simcore.h"
#include "simio.h"
#include "scrollback.h"

#include "dazzler.h"
#include "hostbdos.h"
#include "rtc80.h"
#include "sd-fdc.h"
#if PICOCALC_CONSOLE_UART
#include "uartcon.h"
#endif
#ifdef PICOCALC_WIFI
#include "modem.h"
#endif

/*
 *	Forward declarations of the I/O functions
 *	for all port addresses.
 */
static void p001_out(BYTE data), p255_out(BYTE data);
static void hwctl_out(BYTE data);
static BYTE p000_in(void), p001_in(void), p255_in(void), hwctl_in(void);
static void mmu_out(BYTE data);
static BYTE mmu_in(void);
#ifdef PICOCALC_WIFI
static BYTE sio2b_data_in(void), sio2b_stat_in(void), sio2_ctl_in(void);
static void sio2b_data_out(BYTE data), sio2_ignore_out(BYTE data);
#endif
#if PICOCALC_CONSOLE_UART
static BYTE sio2a_data_in(void), sio2a_stat_in(void);
static void sio2a_data_out(BYTE data), sio2a_ctl_out(BYTE data);
#endif

static BYTE sio_last;	/* last character received */
static int sio_ahead = -1; /* one character of lookahead, -1 when empty */
       BYTE fp_value;	/* port 255 value, can be set from ICE or config() */
static BYTE hwctl_lock = 0xff; /* lock status hardware control port */

/*
 *	This array contains function pointers for every input
 *	I/O port (0 - 255), to do the required I/O.
 */
BYTE (*const port_in[256])(void) = {
	[  0] = p000_in,	/* SIO status */
	[  1] = p001_in,	/* SIO data */
	[  4] = fdc_in,		/* FDC status */
	[ 14] = dazzler_flags_in, /* Cromemco Dazzler flags */
#if PICOCALC_CONSOLE_UART
	[ 34] = sio2a_data_in,	/* IMSAI SIO-2 port A data, raw USB-C UART */
	[ 35] = sio2a_stat_in,	/* IMSAI SIO-2 port A status */
#endif
#ifdef PICOCALC_WIFI
	[ 36] = sio2b_data_in,	/* IMSAI SIO-2 port B data, the modem */
	[ 37] = sio2b_stat_in,	/* IMSAI SIO-2 port B status */
	[ 40] = sio2_ctl_in,	/* IMSAI SIO-2 control, carrier detect */
#endif
	[ 64] = mmu_in,		/* MMU */
	[ 65] = clkc_in,	/* RTC read clock command */
	[ 66] = clkd_in,	/* RTC read clock data */
	[160] = hwctl_in,	/* virtual hardware control */
	[255] = p255_in		/* for frontpanel */
};

/*
 *	This array contains function pointers for every output
 *	I/O port (0 - 255), to do the required I/O.
 *
 *	Port 0 out drove the RGB LED on the Waveshare board; the PicoCalc has
 *	none, so the port is left unhandled and writes to it are ignored.
 */
void (*const port_out[256])(BYTE data) = {
	[  1] = p001_out,	/* SIO data */
	[  4] = fdc_out,	/* FDC command */
	[ 14] = dazzler_ctl_out, /* Cromemco Dazzler control */
	[ 15] = dazzler_format_out, /* Cromemco Dazzler format */
#if PICOCALC_CONSOLE_UART
	[ 34] = sio2a_data_out,	/* IMSAI SIO-2 port A data */
	[ 35] = sio2a_ctl_out,	/* IMSAI SIO-2 port A control */
#endif
#ifdef PICOCALC_WIFI
	[ 36] = sio2b_data_out,	/* IMSAI SIO-2 port B data */
	[ 37] = sio2_ignore_out, /* IMSAI SIO-2 port B control */
	[ 40] = sio2_ignore_out, /* IMSAI SIO-2 control */
#endif
	[ 64] = mmu_out,	/* MMU */
	[ 65] = clkc_out,	/* RTC write clock command */
	[ 66] = clkd_out,	/* RTC write clock data */
	[160] = hwctl_out,	/* virtual hardware control */
	[161] = host_bdos_out,	/* host file I/O hook, R.COM and W.COM */
	[255] = p255_out	/* for frontpanel */
};

/*
 *	This function is to initiate the I/O devices.
 *	It will be called from the CPU simulation before
 *	any operation with the CPU is possible.
 */
void init_io(void)
{
}

/*
 *	Poll every enabled stdio driver for a character without blocking.
 *	Going through stdio rather than the UART or TinyUSB directly is what
 *	lets the console be the PicoCalc screen, a serial terminal, or both.
 */
static bool sio_rx_ready(void)
{
	int c;

	if (sio_ahead < 0) {
		c = getchar_timeout_us(0);
		if (c != PICO_ERROR_TIMEOUT) {
			/* the PicoCalc keyboard reports LF for Enter, but CP/M
			   and everything else from the era wants CR */
			if (c == '\n')
				c = '\r';
			sio_ahead = c & 0xff;
		}
	}
	return sio_ahead >= 0;
}

/*
 *	I/O function port 0 read:
 *	read status of the console and return:
 *	bit 0 = 0, character available for input from tty
 *	bit 7 = 0, transmitter ready to write character to tty
 */
static BYTE p000_in(void)
{
	register BYTE stat = 0b10000001; /* initially not ready */

	/*
	 * Holding the transmitter busy while the user is reading back
	 * through the history is what stops new output being drawn
	 * underneath it. sio_rx_ready() still runs, and it is what pumps
	 * the keyboard, so the keys that end scrollback get through.
	 */
	if (!scrollback_active())
		stat &= 0b01111111;
	if (sio_rx_ready())
		stat &= 0b11111110;

	return stat;
}

/*
 *	I/O function port 1 read:
 *	Read byte from the console.
 */
static BYTE p001_in(void)
{
	if (sio_rx_ready()) {
		sio_last = (BYTE) sio_ahead;
		sio_ahead = -1;
	}

	return sio_last;
}

#if PICOCALC_CONSOLE_UART
/*
 *	IMSAI SIO-2 port A, the second serial port of z80pack's IMSAI, so
 *	XMODEM.CFG and the other communication programs from its disks work
 *	unchanged. It shares the USB-C line with the console, but passes all
 *	8 bits through untranslated, which binary transfers need.
 *
 *	8251 status: bit 0 = transmitter ready, bit 1 = receiver ready
 */
static BYTE sio2a_stat_in(void)
{
	return (uartcon_raw_tx_ready() ? 0x01 : 0) |
	       (uartcon_raw_rx_ready() ? 0x02 : 0);
}

static BYTE sio2a_data_in(void)
{
	return uartcon_raw_getc();
}

static void sio2a_data_out(BYTE data)
{
	uartcon_raw_putc(data);
}

/* the 8251 mode and command bytes have nothing to set up on a PL011 */
static void sio2a_ctl_out(BYTE data)
{
	UNUSED(data);
}
#endif

#ifdef PICOCALC_WIFI
/*
 *	IMSAI SIO-2 port B, wired to the AT modem as in z80pack's IMSAI.
 *	Same 8251 status bits as port A.
 */
static BYTE sio2b_stat_in(void)
{
	return (modem_tx_ready() ? 0x01 : 0) | (modem_rx_ready() ? 0x02 : 0);
}

static BYTE sio2b_data_in(void)
{
	return modem_getc();
}

static void sio2b_data_out(BYTE data)
{
	modem_putc(data);
}

/* z80pack's layout: unused bits high, bit 6 is port B carrier */
static BYTE sio2_ctl_in(void)
{
	return 0b10111011 | (modem_carrier() ? 0x40 : 0);
}

/* 8251 mode and command bytes, and the control latch, set nothing here */
static void sio2_ignore_out(BYTE data)
{
	UNUSED(data);
}
#endif

/*
 *	Input from virtual hardware control port
 *	returns lock status of the port
 */
static BYTE hwctl_in(void)
{
	return hwctl_lock;
}

/*
 *	read MMU register
 */
static BYTE mmu_in(void)
{
	return selbnk;
}

/*
 *	I/O function port 255 read:
 *	used by frontpanel machines
 */
static BYTE p255_in(void)
{
	return fp_value;
}

/*
 *	I/O function port 1 write:
 *	Write byte to the console.
 */
static void p001_out(BYTE data)
{
	putchar_raw((int) data & 0x7f); /* strip parity, some software won't */
}

/*
 *	Port is locked until magic number 0xaa is received!
 *
 *	Virtual hardware control output.
 *	Used to shutdown and switch CPU's.
 *
 *	bit 4 = 1	switch CPU model to 8080
 *	bit 5 = 1	switch CPU model to Z80
 *	bit 6 = 1	reset system
 *	bit 7 = 1	halt emulation via I/O
 */
static void hwctl_out(BYTE data)
{
	/* if port is locked do nothing */
	if (hwctl_lock && (data != 0xaa))
		return;

	/* unlock port ? */
	if (hwctl_lock && (data == 0xaa)) {
		hwctl_lock = 0;
		return;
	}

	/* process output to unlocked port */
	/* but first lock port again */
	hwctl_lock = 0xff;

	if (data & 128) {
		cpu_error = IOHALT;
		cpu_state = ST_STOPPED;
		return;
	}

	if (data & 64) {
		reset_cpu();		/* reset CPU */
		reset_memory();		/* reset memory */
		PC = 0xff00;		/* power on jump to boot ROM */
		return;
	}

#if !defined (EXCLUDE_I8080) && !defined(EXCLUDE_Z80)
	if (data & 32) {	/* switch cpu model to Z80 */
		switch_cpu(Z80);
		return;
	}

	if (data & 16) {	/* switch cpu model to 8080 */
		switch_cpu(I8080);
		return;
	}
#endif
}

/*
 *	write MMU register
 */
static void mmu_out(BYTE data)
{
	selbnk = data;
}

/*
 *	This allows to set the frontpanel port with ICE p command
 */
static void p255_out(BYTE data)
{
	fp_value = data;
}
