/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * QSPI PSRAM on the RP2350's second QMI chip select. See psram.h.
 *
 * The setup is the one pico-sdk 2.3's hardware_psram use for the
 * APS6404L: reset it over the QMI's direct mode, switch it to QPI, program
 * memory window 1 with the quad read and write commands and the chip's
 * timing, and open the window for writes. It runs from SRAM with interrupts
 * off, because the flash is unreachable while the QMI is in direct mode.
 */

#include <string.h>

#include "pico/stdlib.h"

#include "psram.h"

#if PICO_RP2350 && defined(PIMORONI_PICO_PLUS2_W_PSRAM_CS_PIN)

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/regs/addressmap.h"

#define CS_PIN		PIMORONI_PICO_PLUS2_W_PSRAM_CS_PIN
#define MAX_FREQ	133000000	/* the APS6404L's clock limit */
#define MAX_SELECT_NS	8000		/* tCEM: how long CS may stay low */
#define MIN_DESELECT_NS	18		/* tCPH: how long it must stay high */
#define WINDOW		(16u * 1024 * 1024)	/* each chip select's span */

/* the same memory with the cache out of the way, for sizing the chip */
#define PSRAM_NOCACHE	(XIP_NOCACHE_NOALLOC_BASE + WINDOW)

static size_t chip_size;

/*
 * Nothing in here may reach into flash: while the QMI is in direct mode the
 * flash is not mapped, so a call out, even to a delay routine, stalls the
 * core for good. The timing word is worked out beforehand and the wait is
 * an inline loop.
 */
static void __no_inline_not_in_flash_func(qmi_setup)(uint32_t timing,
						      uint32_t reset_cycles)
{
	/* direct mode at a tenth of the clock: reset enable, reset, enter QPI */
	qmi_hw->direct_csr = (10u << QMI_DIRECT_CSR_CLKDIV_LSB) |
			     QMI_DIRECT_CSR_EN_BITS | QMI_DIRECT_CSR_AUTO_CS1N_BITS;
	while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
		;
	qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | 0x66;
	while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
		;
	qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | 0x99;
	while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
		;
	busy_wait_at_least_cycles(reset_cycles);
	qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | 0x35;
	while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS)
		;
	qmi_hw->direct_csr = 0;

	qmi_hw->m[1].timing = timing;

	/* quad everything: fast read 0xEB with six dummy cycles, write 0x38 */
	qmi_hw->m[1].rfmt =
		(QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_PREFIX_WIDTH_LSB) |
		(QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_RFMT_ADDR_WIDTH_LSB) |
		(QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_SUFFIX_WIDTH_LSB) |
		(QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_RFMT_DUMMY_WIDTH_LSB) |
		(QMI_M1_RFMT_DATA_WIDTH_VALUE_Q << QMI_M1_RFMT_DATA_WIDTH_LSB) |
		(QMI_M1_RFMT_PREFIX_LEN_VALUE_8 << QMI_M1_RFMT_PREFIX_LEN_LSB) |
		(6u << QMI_M1_RFMT_DUMMY_LEN_LSB);
	qmi_hw->m[1].rcmd = 0xEB;
	qmi_hw->m[1].wfmt =
		(QMI_M1_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_PREFIX_WIDTH_LSB) |
		(QMI_M1_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_WFMT_ADDR_WIDTH_LSB) |
		(QMI_M1_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_SUFFIX_WIDTH_LSB) |
		(QMI_M1_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_WFMT_DUMMY_WIDTH_LSB) |
		(QMI_M1_WFMT_DATA_WIDTH_VALUE_Q << QMI_M1_WFMT_DATA_WIDTH_LSB) |
		(QMI_M1_WFMT_PREFIX_LEN_VALUE_8 << QMI_M1_WFMT_PREFIX_LEN_LSB);
	qmi_hw->m[1].wcmd = 0x38;

	hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
}

/* the window 1 timing word for the system clock: the fastest QSPI clock
   the chip takes, a receive delay that grows with it, and the chip's
   select and deselect limits in the QMI's units */
static uint32_t timing_word(uint32_t clk)
{
	uint32_t period_fs = 1000000000000000ull / clk;
	uint32_t divisor = (clk + MAX_FREQ - 1) / MAX_FREQ, rxdelay;
	uint32_t max_select, min_deselect;

	if (divisor == 1 && clk > 100000000)
		divisor = 2;
	rxdelay = divisor;
	if (clk / divisor > 100000000)
		rxdelay++;
	max_select = ((uint64_t) MAX_SELECT_NS * 1000000) / (64 * period_fs);
	min_deselect = ((uint64_t) MIN_DESELECT_NS * 1000000 + period_fs - 1)
		       / period_fs;
	min_deselect = (min_deselect > (divisor + 1) / 2) ?
		       min_deselect - (divisor + 1) / 2 : 1;
	return (1u << QMI_M1_TIMING_COOLDOWN_LSB) |
	       (QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB) |
	       (max_select << QMI_M1_TIMING_MAX_SELECT_LSB) |
	       (min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB) |
	       (rxdelay << QMI_M1_TIMING_RXDELAY_LSB) |
	       (divisor << QMI_M1_TIMING_CLKDIV_LSB);
}

/*
 * Size the chip by where its addressing wraps: a chip ignores the address
 * bits above its size, so a write at offset S lands on offset 0 when the
 * chip is S bytes. Through the uncached alias, so that every access really
 * reaches the chip.
 */
static size_t __no_inline_not_in_flash_func(detect)(void)
{
	volatile uint32_t *base = (volatile uint32_t *) PSRAM_NOCACHE;
	volatile uint32_t *probe;
	size_t size;

	base[0] = 0x5a5aa5a5;
	base[1] = 0x0f0ff0f0;
	if (base[0] != 0x5a5aa5a5 || base[1] != 0x0f0ff0f0)
		return 0;
	for (size = 1024 * 1024; size < WINDOW; size <<= 1) {
		probe = (volatile uint32_t *) (PSRAM_NOCACHE + size);
		probe[0] = 0xc3c33c3c;
		if (base[0] == 0xc3c33c3c)
			return size;
		if (probe[0] != 0xc3c33c3c)
			return 0;	/* memory that does not hold a value */
	}
	return WINDOW;
}

size_t psram_init(void)
{
	uint32_t clk = clock_get_hz(clk_sys), timing = timing_word(clk), save;

	gpio_set_function(CS_PIN, GPIO_FUNC_XIP_CS1);
	save = save_and_disable_interrupts();
	qmi_setup(timing, clk / 10000);	/* 100 us for the reset to settle */
	chip_size = detect();
	restore_interrupts(save);
	return chip_size;
}

size_t psram_size(void)
{
	return chip_size;
}

void psram_measure(void *scratch, size_t len, unsigned *read_kbs,
		   unsigned *write_kbs)
{
	uint8_t *mem = (uint8_t *) PSRAM_BASE;
	size_t total = 512 * 1024, done;
	uint64_t t0, t1, t2;

	*read_kbs = *write_kbs = 0;
	if (!chip_size || !len || len > total)
		return;
	t0 = time_us_64();
	for (done = 0; done < total; done += len)
		memcpy(mem + done, scratch, len);
	t1 = time_us_64();
	for (done = 0; done < total; done += len)
		memcpy(scratch, mem + done, len);
	t2 = time_us_64();
	if (t1 > t0)
		*write_kbs = (unsigned) ((uint64_t) total * 1000000 / 1024 / (t1 - t0));
	if (t2 > t1)
		*read_kbs = (unsigned) ((uint64_t) total * 1000000 / 1024 / (t2 - t1));
}

#else /* no QSPI PSRAM on this board */

size_t psram_init(void)
{
	return 0;
}

size_t psram_size(void)
{
	return 0;
}

void psram_measure(void *scratch, size_t len, unsigned *read_kbs,
		   unsigned *write_kbs)
{
	(void) scratch;
	(void) len;
	*read_kbs = *write_kbs = 0;
}

#endif
