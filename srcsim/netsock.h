/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * One TCP connection for the modem, without blocking the caller.
 *
 * On the Pico every lwIP call happens on core 0 inside wifi_service(), and
 * core 1 only moves bytes through two single producer, single consumer rings
 * and posts open/close requests, the same split as wifi.c. tools/hostsim
 * implements the same interface with POSIX sockets, so the modem can be
 * tried against real hosts on a PC.
 */

#ifndef NETSOCK_INC
#define NETSOCK_INC

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	NS_IDLE,	/* no connection */
	NS_RESOLVING,	/* waiting for DNS */
	NS_CONNECTING,	/* waiting for the handshake */
	NS_RINGING,	/* a caller is connected, waiting for ns_answer() */
	NS_CONNECTED,
	NS_CLOSED,	/* the far end closed or reset; unread data remains */
	NS_FAILED	/* never connected, see ns_error() */
} ns_state_t;

/* core 0, from wifi_service(); lwip_up is false until the radio started */
extern void ns_service(bool lwip_up);

/* core 1 */
extern void ns_open(const char *host, uint16_t port, uint32_t timeout_ms);
extern void ns_close(void);
extern ns_state_t ns_state(void);
extern const char *ns_error(void);
extern int ns_read(uint8_t *buf, int max);
extern int ns_avail(void);
extern int ns_write(const uint8_t *buf, int len);
extern int ns_tx_space(void);

/*
 * Incoming calls. While listening, a caller is accepted only when there is
 * no other connection, and arrives as NS_RINGING; its data is buffered
 * until ns_answer(), and ns_close() turns it away. Port 0 stops listening.
 * The listener is kept across WiFi coming and going.
 */
extern void ns_listen(uint16_t port);
extern uint16_t ns_listen_port(void);
extern const char *ns_listen_error(void);
extern void ns_answer(void);
extern void ns_peer(char *buf, size_t len);

#endif /* !NETSOCK_INC */
