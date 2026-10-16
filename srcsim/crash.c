/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * How the last run ended. See crash.h.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/sio.h"

#include "crash.h"

#define MAGIC		0x5a5ac4a5
#define WATCH_MAGIC	0x77a7c4ed
#define WATCH_MS	8000
#define MSG_LEN		120

enum { KIND_FAULT = 1, KIND_PANIC };

typedef struct {
	uint32_t magic;
	uint32_t kind, core;
	uint32_t pc, lr, sp, cfsr, hfsr, bfar;
	char msg[MSG_LEN];
} record_t;

/* neither is cleared by the runtime, so both outlive a watchdog reset */
static record_t __uninitialized_ram(crash);
static uint32_t __uninitialized_ram(crash_watch);

static void __attribute__((noreturn)) restart(void)
{
	watchdog_reboot(0, 0, 0);
	for (;;)
		;
}

/* the stacked frame is r0-r3, r12, lr, pc, xpsr */
void __attribute__((used)) crash_fault(const uint32_t *frame)
{
	crash.magic = MAGIC;
	crash.kind = KIND_FAULT;
	crash.core = sio_hw->cpuid;
	crash.pc = frame[6];
	crash.lr = frame[5];
	crash.sp = (uint32_t) frame;
#if PICO_RP2040
	crash.cfsr = crash.hfsr = crash.bfar = 0;	/* the M0+ has none */
#else
	crash.cfsr = *(volatile uint32_t *) 0xe000ed28;
	crash.hfsr = *(volatile uint32_t *) 0xe000ed2c;
	crash.bfar = *(volatile uint32_t *) 0xe000ed38;
#endif
	restart();
}

/* overrides the SDK's weak breakpoint; Thumb-1 so the RP2040 can run it */
void __attribute__((naked)) isr_hardfault(void)
{
	__asm volatile(
		"movs r0, #4\n"
		"mov r1, lr\n"
		"tst r0, r1\n"
		"beq 1f\n"
		"mrs r0, psp\n"
		"b 2f\n"
		"1: mrs r0, msp\n"
		"2: bl crash_fault\n");
}

/* PICO_PANIC_FUNCTION: lwIP's and the SDK's assertions end up here */
void __attribute__((noreturn)) picocalc_panic(const char *fmt, ...)
{
	va_list ap;

	crash.magic = MAGIC;
	crash.kind = KIND_PANIC;
	crash.core = sio_hw->cpuid;
	va_start(ap, fmt);
	vsnprintf(crash.msg, sizeof(crash.msg), fmt ? fmt : "", ap);
	va_end(ap);
	restart();
}

void crash_report(void)
{
	if (crash.magic == MAGIC && crash.kind == KIND_FAULT) {
		printf("*** The last run ended in a hard fault on core %lu\n"
		       "    PC %08lx LR %08lx SP %08lx\n",
		       (unsigned long) crash.core, (unsigned long) crash.pc,
		       (unsigned long) crash.lr, (unsigned long) crash.sp);
#if !PICO_RP2040
		printf("    CFSR %08lx HFSR %08lx BFAR %08lx\n",
		       (unsigned long) crash.cfsr, (unsigned long) crash.hfsr,
		       (unsigned long) crash.bfar);
#endif
		putchar('\n');
	} else if (crash.magic == MAGIC && crash.kind == KIND_PANIC) {
		crash.msg[MSG_LEN - 1] = '\0';
		printf("*** The last run panicked on core %lu:\n    %s\n\n",
		       (unsigned long) crash.core, crash.msg);
	} else if (watchdog_enable_caused_reboot() &&
		   crash_watch == WATCH_MAGIC) {
		puts("*** The last run locked up during a network transfer\n"
		     "    and the watchdog restarted it\n");
	}
	crash.magic = 0;
	crash_watch = 0;
}

void crash_watch_begin(void)
{
	crash_watch = WATCH_MAGIC;
	watchdog_enable(WATCH_MS, true);
}

void crash_watch_feed(void)
{
	watchdog_update();
}

void crash_watch_end(void)
{
	watchdog_disable();
	crash_watch = 0;
}
