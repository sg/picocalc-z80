/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * USB-C serial console with a buffered, flow controlled input. See uartcon.h.
 */

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#include "uartcon.h"

#define UARTCON		uart0
#define UARTCON_TX	0
#define UARTCON_RX	1

#define XON		0x11
#define XOFF		0x13

#define RX_SIZE		4096	/* power of two */
#define RX_STOP		(RX_SIZE * 3 / 4)
#define RX_START	(RX_SIZE / 4)

static char rx_buf[RX_SIZE];
static volatile uint32_t rx_head;	/* written only by the IRQ */
static volatile uint32_t rx_tail;	/* written only by the reader */
static volatile bool rx_stopped;

/* time_us_32() when a program last touched the raw port, 0 for never */
static volatile uint32_t raw_last;

static void (*chars_available_callback)(void *);
static void *chars_available_param;

/*
 * A transfer program polls the raw port continuously, so a second of silence
 * means it has finished and the line can go back to being the console.
 */
static bool raw_active(void)
{
	uint32_t last = raw_last;

	return last && time_us_32() - last < 1000000;
}

static void send_flow(char c);

static void raw_claim(void)
{
	/* console typing still queued would reach the transfer program first */
	if (!raw_active()) {
		rx_tail = rx_head;
		if (rx_stopped) {
			rx_stopped = false;
			send_flow(XON);
		}
	}
	raw_last = time_us_32() | 1;
}

static void send_flow(char c)
{
	/*
	 * Waiting for an empty FIFO rather than a free slot is what makes this
	 * safe against the other core's uart_putc, which may slip one byte in
	 * between its own readiness check and the write.
	 */
	while (!(uart_get_hw(UARTCON)->fr & UART_UARTFR_TXFE_BITS))
		tight_loop_contents();
	uart_get_hw(UARTCON)->dr = c;
}

static void uartcon_irq(void)
{
	uart_hw_t *hw = uart_get_hw(UARTCON);
	bool got = false;

	while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
		uint32_t dr = hw->dr;

		if (dr & (UART_UARTDR_FE_BITS | UART_UARTDR_BE_BITS))
			continue;	/* line noise or a cable being plugged in */
		if (rx_head - rx_tail < RX_SIZE) {
			rx_buf[rx_head & (RX_SIZE - 1)] = (char) dr;
			rx_head++;
			got = true;
		}
	}

	/* XON/XOFF bytes in the middle of a binary transfer would corrupt it */
	if (!rx_stopped && rx_head - rx_tail >= RX_STOP && !raw_active()) {
		rx_stopped = true;
		send_flow(XOFF);
	}

	if (got && chars_available_callback)
		chars_available_callback(chars_available_param);
}

static int uartcon_in_chars(char *buf, int len)
{
	int n = 0;

	if (raw_active())
		return PICO_ERROR_NO_DATA;

	while (n < len && rx_tail != rx_head) {
		buf[n++] = rx_buf[rx_tail & (RX_SIZE - 1)];
		rx_tail++;
	}

	if (rx_stopped && rx_head - rx_tail <= RX_START) {
		rx_stopped = false;
		send_flow(XON);
	}

	return n ? n : PICO_ERROR_NO_DATA;
}

static void uartcon_out_chars(const char *buf, int len)
{
	if (raw_active())
		return;
	uart_write_blocking(UARTCON, (const uint8_t *) buf, len);
}

static void uartcon_out_flush(void)
{
	uart_tx_wait_blocking(UARTCON);
}

static void uartcon_set_chars_available_callback(void (*fn)(void *),
						 void *param)
{
	chars_available_callback = fn;
	chars_available_param = param;
}

static stdio_driver_t stdio_uartcon = {
	.out_chars = uartcon_out_chars,
	.out_flush = uartcon_out_flush,
	.in_chars = uartcon_in_chars,
	.set_chars_available_callback = uartcon_set_chars_available_callback,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
	.crlf_enabled = true,
#endif
};

bool uartcon_raw_rx_ready(void)
{
	raw_claim();
	return rx_tail != rx_head;
}

bool uartcon_raw_tx_ready(void)
{
	raw_claim();
	return uart_is_writable(UARTCON);
}

uint8_t uartcon_raw_getc(void)
{
	static uint8_t last;

	raw_claim();
	if (rx_tail != rx_head) {
		last = (uint8_t) rx_buf[rx_tail & (RX_SIZE - 1)];
		rx_tail++;
	}
	if (rx_stopped && rx_head - rx_tail <= RX_START) {
		rx_stopped = false;
		send_flow(XON);
	}
	return last;
}

void uartcon_raw_putc(uint8_t c)
{
	raw_claim();
	uart_putc_raw(UARTCON, (char) c);
}

void uartcon_init(void)
{
	gpio_set_function(UARTCON_TX, UART_FUNCSEL_NUM(UARTCON, UARTCON_TX));
	gpio_set_function(UARTCON_RX, UART_FUNCSEL_NUM(UARTCON, UARTCON_RX));
	uart_init(UARTCON, PICOCALC_UART_BAUD);

	irq_set_exclusive_handler(UART_IRQ_NUM(UARTCON), uartcon_irq);
	irq_set_enabled(UART_IRQ_NUM(UARTCON), true);
	uart_set_irqs_enabled(UARTCON, true, false);

	/* a host left paused by a previous run would otherwise never resume */
	send_flow(XON);

	stdio_set_driver_enabled(&stdio_uartcon, true);
}
