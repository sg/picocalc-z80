/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Hayes compatible modem that dials TCP hosts, with telnet negotiation.
 *
 * The command set and S-registers follow David McNaughton's
 * generic-at-modem.c from z80pack (MIT licence), reworked so that nothing
 * blocks the emulated CPU: dialling is a state that ends in CONNECT or a
 * failure code, and all network traffic goes through netsock.h. It is
 * IMSAI SIO-2 channel B (ports 24h/25h) with carrier on port 28h bit 6, as
 * in z80pack's IMSAI.
 */

#ifndef MODEM_INC
#define MODEM_INC

#include <stdbool.h>
#include <stdint.h>

extern void modem_init(void);
extern bool modem_rx_ready(void);
extern uint8_t modem_getc(void);
extern bool modem_tx_ready(void);
extern void modem_putc(uint8_t c);
extern bool modem_carrier(void);

/* supplied by the platform: picosim.c, or tools/hostsim */
extern uint64_t modem_time_us(void);
extern void modem_save_settings(void);
extern void modem_term_size(int *cols, int *rows);

#endif /* !MODEM_INC */
