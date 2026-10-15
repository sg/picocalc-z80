/* I/O for the host build: same port map as srcsim/simio.c, console on stdio */

#include <stdio.h>
#include <unistd.h>

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"
#include "simcore.h"
#include "simio.h"
#include "rtc80.h"
#include "sd-fdc.h"
#include "modem.h"

static void p001_out(BYTE data), p255_out(BYTE data), hwctl_out(BYTE data);
static void mmu_out(BYTE data);
static BYTE p000_in(void), p001_in(void), p255_in(void), hwctl_in(void);
static BYTE mmu_in(void);
static BYTE sio2b_data_in(void), sio2b_stat_in(void), sio2_ctl_in(void);
static void sio2b_data_out(BYTE data), sio2_ignore_out(BYTE data);
static BYTE sio2a_stat_in(void), sio2a_data_in(void);

BYTE fp_value;
static BYTE hwctl_lock = 0xff;
static int ahead = -1;

BYTE (*const port_in[256])(void) = {
	[  0] = p000_in, [  1] = p001_in, [  4] = fdc_in,
	[ 34] = sio2a_data_in, [ 35] = sio2a_stat_in,
	[ 36] = sio2b_data_in, [ 37] = sio2b_stat_in, [ 40] = sio2_ctl_in,
	[ 64] = mmu_in,  [ 65] = clkc_in, [ 66] = clkd_in,
	[160] = hwctl_in, [255] = p255_in
};

void (*const port_out[256])(BYTE data) = {
	[  1] = p001_out, [  4] = fdc_out,
	[ 34] = sio2_ignore_out, [ 35] = sio2_ignore_out,
	[ 36] = sio2b_data_out, [ 37] = sio2_ignore_out, [ 40] = sio2_ignore_out,
	[ 64] = mmu_out,  [ 65] = clkc_out, [ 66] = clkd_out,
	[160] = hwctl_out, [255] = p255_out
};

void init_io(void) {}

/* the modem on IMSAI SIO-2 port B, as in srcsim/simio.c */
static BYTE sio2b_stat_in(void)
{
	return (modem_tx_ready() ? 0x01 : 0) | (modem_rx_ready() ? 0x02 : 0);
}

static BYTE sio2b_data_in(void) { return modem_getc(); }
static void sio2b_data_out(BYTE data) { modem_putc(data); }
static BYTE sio2_ctl_in(void) { return 0b10111011 | (modem_carrier() ? 0x40 : 0); }
static void sio2_ignore_out(BYTE data) { UNUSED(data); }

/* port A is the USB-C line on the Pico; here nothing is plugged in */
static BYTE sio2a_stat_in(void) { return 0x01; }
static BYTE sio2a_data_in(void) { return 0; }

static bool rx_ready(void)
{
	unsigned char c;

	if (ahead < 0) {
		if (read(0, &c, 1) == 1)
			ahead = (c == '\n') ? '\r' : c;
	}
	return ahead >= 0;
}

static BYTE p000_in(void)
{
	BYTE stat = 0b10000001;

	stat &= 0b01111111;
	if (rx_ready())
		stat &= 0b11111110;
	return stat;
}

static BYTE p001_in(void)
{
	BYTE c = 0;

	if (rx_ready()) {
		c = (BYTE) ahead;
		ahead = -1;
	}
	return c;
}

static void p001_out(BYTE data)
{
	putchar(data & 0x7f);
	fflush(stdout);
}

static BYTE hwctl_in(void) { return hwctl_lock; }
static BYTE mmu_in(void) { return selbnk; }
static void mmu_out(BYTE data) { selbnk = data; }
static BYTE p255_in(void) { return fp_value; }
static void p255_out(BYTE data) { fp_value = data; }

static void hwctl_out(BYTE data)
{
	if (hwctl_lock && (data != 0xaa))
		return;
	if (hwctl_lock && (data == 0xaa)) {
		hwctl_lock = 0;
		return;
	}
	hwctl_lock = 0xff;
	if (data & 128) {
		cpu_error = IOHALT;
		cpu_state = ST_STOPPED;
		return;
	}
	if (data & 64) {
		reset_cpu();
		reset_memory();
		PC = 0xff00;
	}
}
