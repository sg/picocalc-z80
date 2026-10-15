/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Console mirror on the mainboard's USB-C port (CH340 on UART0, GP0/GP1).
 *
 * Replaces the SDK's stdio_uart, whose input is only the 32 byte hardware
 * FIFO: anything pasted while CP/M is busy elsewhere overflows it. Received
 * bytes are moved by interrupt into a ring buffer instead, and the sender is
 * paced with XON/XOFF, since the CH340's RTS/CTS are not wired.
 *
 * The raw functions give a CP/M transfer program the same line with no
 * translation in either direction. Writing to them, or polling them in a
 * loop, takes the line away from the console - its output stops going to the
 * host and its input stops reading the buffer - until they have gone unused
 * for a second. A few stray reads, as from a port scan, do not.
 */

#ifndef UARTCON_INC
#define UARTCON_INC

#include <stdbool.h>
#include <stdint.h>

extern void uartcon_init(void);

extern bool uartcon_raw_rx_ready(void);
extern bool uartcon_raw_tx_ready(void);
extern uint8_t uartcon_raw_getc(void);
extern void uartcon_raw_putc(uint8_t c);

#endif /* !UARTCON_INC */
