/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024-2025 by Udo Munk & Thomas Eberhardt
 *
 * PicoCalc port Copyright (C) 2026 by Steve Gibson
 * This is the main program for a ClockworkPi PicoCalc with a
 * Pimoroni Pico Plus 2 W, substitutes z80core/simmain.c
 *
 * History:
 * 28-APR-2024 implemented first release of Z80 emulation
 * 09-MAY-2024 test 8080 emulation
 * 27-MAY-2024 add access to files on MicroSD
 * 28-MAY-2024 implemented boot from disk images with some OS
 * 31-MAY-2024 use USB UART
 * 09-JUN-2024 implemented boot ROM
 * 11-JUN-2024 ported to Pico Eval Board
 * 15-AUG-2026 ported to PicoCalc
 */

/* Raspberry SDK and FatFS includes */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#if LIB_PICO_STDIO_USB
#include <tusb.h>
#endif
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"

#include "crash.h"
#include "drivemenu.h"

#include "ff.h"

/* PicoCalc drivers */
#include "lcd.h"
#include "keyboard.h"
#include "term.h"
#include "multicore.h"

/* Project includes */
#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simcfg.h"
#include "simmem.h"
#include "simcore.h"
#include "simport.h"
#include "simio.h"
#ifdef WANT_ICE
#include "simice.h"
#endif
#include "disks.h"
#include "rtc_compat.h"
#include "keymap.h"
#include "uartcon.h"
#ifdef PICOCALC_WIFI
#include "wifi.h"
#include "modem.h"
#endif
#include "status.h"
#include "scrollback.h"
#include "dazzler.h"

#ifdef WANT_ICE
static void picosim_ice_cmd(char *cmd, WORD *wrk_addr);
static void picosim_ice_help(void);
#endif

static void z80_main(void);

/*
 * Core 1's default stack is SCRATCH_X, which is only 4K on RP2350 and has to
 * cover the config dialog and printf as well as the CPU loop. SRAM is not
 * scarce here, so give it a real one.
 */
static uint32_t core1_stack[8192 / sizeof(uint32_t)];

#define BS  0x08 /* ASCII backspace */
#define DEL 0x7f /* ASCII delete */

/* CPU speed */
int speed = CPU_SPEED;

/*
 * callback for TinyUSB when terminal sends a break
 * stops CPU
 */
#if LIB_PICO_STDIO_USB
void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms)
{
	UNUSED(itf);
	UNUSED(duration_ms);

	cpu_error = USERINT;
	cpu_state = ST_STOPPED;
}
#endif

/*
 * callback for the PicoCalc BREAK key, replaces the user key on the
 * Waveshare board. Runs on core 0 from the keyboard poll timer while the
 * machine is running on core 1.
 */
static void break_key(void)
{
	cpu_error = USERINT;
	cpu_state = ST_STOPPED;
}

/*
 * Ctrl+Alt+D, from the keyboard timer on core 0: stop the CPU loop with no
 * error set, so that z80_main() runs the drive menu on core 1 and then starts
 * the loop again where it left off.
 */
static volatile bool drive_menu_pending;

static void chord_key(unsigned char code)
{
	/* the Dazzler's screen is not text, so there is nothing to put back */
	if (tolower(code) == 'd' && !dazzler_state) {
		drive_menu_pending = true;
		cpu_state = ST_STOPPED;
	}
}

/*
 * Core 0 owns the LCD: drawing calls made on core 1 are forwarded here over
 * the inter-core FIFO and serviced by the IRQ handler multicore_init()
 * installs. So core 0 sets the hardware up and then just stays available,
 * and the machine itself gets core 1.
 */
#ifdef PICOCALC_STACK_WATERMARK
extern char __StackBottom;

/* fill the stack below this frame with a pattern, once, at boot */
static void __attribute__((noinline)) stack_paint(void)
{
	char here, *p = &__StackBottom;
	char *top = (char *) ((uintptr_t) &here - 256);

	while (p < top)
		*p++ = 0x5a;
}

unsigned stack_headroom(void)
{
	char *p = &__StackBottom;

	while (*p == 0x5a)
		p++;
	return p - &__StackBottom;
}
#endif

int main(void)
{
#ifdef PICOCALC_STACK_WATERMARK
	stack_paint();
#endif
	lcd_init();
	keyboard_init();
	keymap_init();
	multicore_init();
	keyboard_set_interrupt_callback(break_key);
	keyboard_set_chord_callback(chord_key);

	rtc_compat_init();	/* start the calendar clock */

#if LIB_PICO_STDIO_USB
	stdio_init_all();	/* optional USB console alongside the LCD */
#endif
#if PICOCALC_CONSOLE_UART
	uartcon_init();
#endif

	scrollback_init();
	status_init();

	multicore_launch_core1_with_stack(z80_main, core1_stack,
					  sizeof(core1_stack));

	/*
	 * Core 0 has nothing else to do while the machine runs, so the
	 * Dazzler draws its frames here and the battery is read here too.
	 * Both are main context: the repaint timer and core 1's drawing
	 * arrive as interrupts, which is why the blit masks them per row.
	 */
	while (true) {
		dazzler_poll();
		status_poll();
#ifdef PICOCALC_WIFI
		wifi_service();
#endif
		tight_loop_contents();
	}
}

#ifdef PICOCALC_WIFI
uint64_t modem_time_us(void)
{
	return time_us_64();
}

/* AT+WIFI keeps a working network, as the config dialog does */
void modem_save_settings(void)
{
	save_config();
}

void modem_term_size(int *cols, int *rows)
{
	*cols = term_get_width();
	*rows = term_get_height();
}
#endif

static void z80_main(void)
{
	char s[2];

	init_cpu();		/* initialize CPU */
	PC = 0xff00;		/* power on jump into the boot ROM */
	init_disks();		/* initialize disk drives */
	init_memory();		/* initialize memory configuration */
	init_io();		/* initialize I/O devices */

	read_config();		/* read configuration from MicroSD */
	apply_font();		/* the font decides how big the terminal is */
#ifdef PICOCALC_WIFI
	net_autoconnect();
#endif

	/* print banner */
	printf("\f" STATUS_GREEN_SGR "%s\033[0m release " STATUS_GREEN_SGR "%s\033[0m\n",
	       USR_COM, USR_REL);
	printf("running on ARM %s cores at %i MHz\n",
#if PICO_RP2040
	       "Cortex-M0+",
#else
	       "Cortex-M33",
#endif
	       SYS_CLK_MHZ);
	printf("%s\n", USR_CPR);
	printf("<steve@stevegibson.com>\n\n");
	printf("Based on:\n  Z80pack release %s\n", RELEASE);
	printf("  and the Pico-Eval-Board-80 project,\n");
	printf("  %s\n\n", COPYR);
	crash_report();

#ifdef WANT_ICE
	/* if ICE compiled in print some hints */
	puts("ICE is compiled in and starts with g command");
	puts("For help type ? at the ICE prompt\n");
#endif

	term_set_blinking_cursor(true);
	status_paint();

	config();		/* configure the machine */
	save_config();		/* save configuration on MicroSD */
#ifdef PICOCALC_WIFI
	modem_init();		/* the init string may have just changed */
#endif

	f_value = speed;	/* setup speed of the CPU */
	if (f_value)
		tmax = speed * 10000;	/* theoretically */
	else
		tmax = 100000;	/* for periodic CPU accounting updates */

	/* history is only useful once the machine owns the console */
	scrollback_enable(true);

	/* run the CPU with whatever is in memory */
#ifdef WANT_ICE
	ice_cust_cmd = picosim_ice_cmd;
	ice_cust_help = picosim_ice_help;
	ice_cmd_loop(0);
#else
	for (;;) {
		drive_menu_pending = false;
		run_cpu();
		if (!drive_menu_pending || cpu_error != NONE)
			break;
		drive_menu();
	}
#endif

	scrollback_enable(false);
	exit_disks();		/* stop disk drives */

#ifndef WANT_ICE
	putchar('\n');
	report_cpu_error();	/* check for CPU emulation errors and report */
	report_cpu_stats();	/* print some execution statistics */
#endif
	puts("\nPress any key to restart CPU");
	get_cmdline(s, 2);

	/* reset machine */
	watchdog_reboot(0, 0, 0);
	while (true) {
		__nop();
	}
}

/*
 * Read an ICE or config command line of maximum length len - 1
 * from the terminal. For single character requests (len == 2),
 * returns immediately after input is received.
 */
int get_cmdline(char *buf, int len)
{
	int i = 0;
	char c;

	for (;;) {
		c = getchar();
		if ((c == BS) || (c == DEL)) {
			if (i >= 1) {
				putchar(BS);
				putchar(' ');
				putchar(BS);
				i--;
			}
		} else if ((c != '\r') && (c != '\n')) {
			/* a serial terminal ends a line with CR, the PicoCalc
			   keyboard reports LF, so accept either */
			if (i < len - 1) {
				buf[i++] = c;
				putchar(c);
				if (len == 2)
					break;
			}
		} else {
			break;
		}
	}
	buf[i] = '\0';
	putchar('\n');
	return 0;
}

#ifdef WANT_ICE

/*
 *	This function is the callback for the alarm.
 *	The CPU emulation is stopped here.
 */
static int64_t timeout(alarm_id_t id, void *user_data)
{
	UNUSED(id);
	UNUSED(user_data);

	cpu_state = ST_STOPPED;
	return 0;
}

static void picosim_ice_cmd(char *cmd, WORD *wrk_addr)
{
	char *s;
	BYTE save[3];
	WORD save_PC;
	Tstates_t T0;
	unsigned freq;
#ifdef WANT_HB
	bool save_hb_flag;
#endif

	switch (tolower((unsigned char) *cmd)) {
	case 'c':
		/*
		 *	Calculate the clock frequency of the emulated CPU:
		 *	into memory locations 0000H to 0002H the following
		 *	code will be stored:
		 *		LOOP: JP LOOP
		 *	It uses 10 T states for each execution. A 3 second
		 *	alarm is set and then the CPU started. For every JP
		 *	the T states counter is incremented by 10 and after
		 *	the timer is down and stops the emulation, the clock
		 *	speed of the CPU in MHz is calculated with:
		 *		f = (T - T0) / 3000000
		 */

#ifdef WANT_HB
		save_hb_flag = hb_flag;
		hb_flag = false;
#endif
		save[0] = getmem(0x0000); /* save memory locations */
		save[1] = getmem(0x0001); /* 0000H - 0002H */
		save[2] = getmem(0x0002);
		putmem(0x0000, 0xc3);	/* store opcode JP 0000H at address */
		putmem(0x0001, 0x00);	/* 0000H */
		putmem(0x0002, 0x00);
		save_PC = PC;		/* save PC */
		PC = 0;			/* set PC to this code */
		T0 = T;			/* remember start clock counter */
		add_alarm_in_ms(3000, timeout, /* set 3 second alarm */
				NULL, true);
		run_cpu();		/* start CPU */
		PC = save_PC;		/* restore PC */
		putmem(0x0000, save[0]); /* restore memory locations */
		putmem(0x0001, save[1]); /* 0000H - 0002H */
		putmem(0x0002, save[2]);
#ifdef WANT_HB
		hb_flag = save_hb_flag;
#endif
#ifndef EXCLUDE_Z80
		if (cpu == Z80)
			s = "JP";
#endif
#ifndef EXCLUDE_I8080
		if (cpu == I8080)
			s = "JMP";
#endif
		if (cpu_error == NONE) {
			freq = (unsigned) ((T - T0) / 30000);
			printf("CPU executed %" PRIu64 " %s instructions "
			       "in 3 seconds\n", (T - T0) / 10, s);
			printf("clock frequency = %u.%02u MHz\n",
			       freq / 100, freq % 100);
		} else
			puts("Interrupted by user");
		break;

	case 'r':
		cmd++;
		while (isspace((unsigned char) *cmd))
			cmd++;
		for (s = cmd; *s; s++)
			*s = toupper((unsigned char) *s);
		if (load_file(cmd))
			*wrk_addr = PC = 0;
		break;

	case '!':
		cmd++;
		while (isspace((unsigned char) *cmd))
			cmd++;
		if (strcasecmp(cmd, "ls") == 0)
			list_files(CODE_DIR, "*.BIN");
		else
			puts("what??");
		break;

	default:
		puts("what??");
		break;
	}
}

static void picosim_ice_help(void)
{
	puts("c                         measure clock frequency");
	puts("r filename                read file (without .BIN) into memory");
	puts("! ls                      list files");
}

#endif
