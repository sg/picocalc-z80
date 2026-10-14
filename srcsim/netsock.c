/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The modem's TCP connection over lwIP. See netsock.h.
 *
 * The receive path follows picocalc_lua's drivers/socket.c after its data
 * loss fix: when the ring is full the pbuf is kept rather than dropped, and
 * only the bytes actually copied are acknowledged, so the TCP window holds
 * the far end back while CP/M is slow to read.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "netsock.h"

#define RX_SIZE		4096	/* powers of two */
#define TX_SIZE		2048
#define HOST_LEN	63

enum { REQ_NONE, REQ_OPEN, REQ_CLOSE, REQ_LISTEN, REQ_ANSWER };

static uint8_t rx_buf[RX_SIZE];
static uint8_t tx_buf[TX_SIZE];
static volatile uint32_t rx_head, rx_tail;	/* head: core 0, tail: core 1 */
static volatile uint32_t tx_head, tx_tail;	/* head: core 1, tail: core 0 */

static volatile int req;
static char req_host[HOST_LEN+1];
static uint16_t req_port;
static uint32_t req_timeout;

static volatile ns_state_t state = NS_IDLE;
static const char *volatile error = "";
static char peer[HOST_LEN + 8];	/* host:port */

static volatile uint16_t listen_want;	/* set by core 1 */
static volatile uint16_t listen_port;	/* the port actually listening */
static const char *volatile listen_error = "";

/* core 0 only */
static struct tcp_pcb *pcb;
static struct pbuf *pending;
static uint16_t pending_off;
static ip_addr_t addr;
static volatile bool dns_done, dns_ok;
static absolute_time_t deadline;
static struct tcp_pcb *listen_pcb;

/* ------------------------------------------------------------------ */
/* core 1 side                                                         */

static void post(int r)
{
	while (req != REQ_NONE)
		sleep_ms(1);
	__dmb();
	req = r;
}

void ns_open(const char *host, uint16_t port, uint32_t timeout_ms)
{
	while (req != REQ_NONE)
		sleep_ms(1);
	strncpy(req_host, host, HOST_LEN);
	req_host[HOST_LEN] = '\0';
	req_port = port;
	req_timeout = timeout_ms;
	snprintf(peer, sizeof(peer), "%s:%u", req_host, port);
	/* nothing is connected, so core 0 is not touching the rings */
	rx_head = rx_tail = 0;
	tx_head = tx_tail = 0;
	error = "";
	state = NS_RESOLVING;
	post(REQ_OPEN);
}

void ns_close(void)
{
	post(REQ_CLOSE);
	while (req != REQ_NONE)
		sleep_ms(1);
	/* with no pcb core 0 is done with the rings, and a caller accepted
	   later must not see this connection's leftovers */
	rx_head = rx_tail = 0;
	tx_head = tx_tail = 0;
}

void ns_listen(uint16_t port)
{
	while (req != REQ_NONE)
		sleep_ms(1);
	listen_want = port;
	post(REQ_LISTEN);
}

uint16_t ns_listen_port(void)
{
	return listen_port;
}

const char *ns_listen_error(void)
{
	return listen_error;
}

void ns_answer(void)
{
	post(REQ_ANSWER);
	while (req != REQ_NONE)
		sleep_ms(1);
}

void ns_peer(char *buf, size_t len)
{
	snprintf(buf, len, "%s", peer);
}

ns_state_t ns_state(void)
{
	return state;
}

const char *ns_error(void)
{
	return error;
}

int ns_avail(void)
{
	return (int) (rx_head - rx_tail);
}

int ns_read(uint8_t *buf, int max)
{
	uint32_t head = rx_head, tail = rx_tail;
	int n = 0;

	__dmb();
	while (n < max && tail != head)
		buf[n++] = rx_buf[tail++ & (RX_SIZE - 1)];
	__dmb();
	rx_tail = tail;
	return n;
}

int ns_tx_space(void)
{
	return TX_SIZE - (int) (tx_head - tx_tail);
}

int ns_write(const uint8_t *buf, int len)
{
	uint32_t head = tx_head;
	int n = 0, space = ns_tx_space();

	while (n < len && n < space)
		tx_buf[head++ & (TX_SIZE - 1)] = buf[n++];
	__dmb();
	tx_head = head;
	return n;
}

/* ------------------------------------------------------------------ */
/* core 0 side; callbacks run with the lwIP lock held                  */

static void fail(const char *why)
{
	error = why;
	state = NS_FAILED;
}

/* copy what fits of p from off into the receive ring */
static uint16_t rx_put(struct pbuf *p, uint16_t off)
{
	uint32_t head = rx_head;
	uint32_t space = RX_SIZE - (head - rx_tail);
	uint32_t n = p->tot_len - off, i, chunk;

	if (n > space)
		n = space;
	for (i = 0; i < n; i += chunk) {
		chunk = RX_SIZE - ((head + i) & (RX_SIZE - 1));
		if (chunk > n - i)
			chunk = n - i;
		pbuf_copy_partial(p, &rx_buf[(head + i) & (RX_SIZE - 1)],
				  chunk, off + i);
	}
	__dmb();
	rx_head = head + n;
	return n;
}

static void detach(struct tcp_pcb *tpcb)
{
	tcp_arg(tpcb, NULL);
	tcp_recv(tpcb, NULL);
	tcp_err(tpcb, NULL);
}

static err_t on_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
		     err_t err)
{
	uint16_t n;

	(void) arg;
	(void) err;
	if (!p) {
		detach(tpcb);
		pcb = NULL;
		error = "closed by remote";
		state = NS_CLOSED;
		if (tcp_close(tpcb) != ERR_OK) {
			tcp_abort(tpcb);
			return ERR_ABRT;
		}
		return ERR_OK;
	}
	if (pending) {
		pbuf_cat(pending, p);
		return ERR_OK;
	}
	n = rx_put(p, 0);
	if (n)
		tcp_recved(tpcb, n);
	if (n < p->tot_len) {
		pending = p;
		pending_off = n;
	} else
		pbuf_free(p);
	return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
	(void) arg;
	pcb = NULL;	/* lwIP has already freed it */
	if (state == NS_CONNECTING)
		fail(err == ERR_RST ? "connection refused" : "no answer");
	else if (state == NS_CONNECTED || state == NS_RINGING) {
		error = "connection reset";
		state = NS_CLOSED;
	}
}

static err_t on_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
	(void) arg;
	(void) tpcb;
	if (err != ERR_OK)
		return err;
	state = NS_CONNECTED;
	return ERR_OK;
}

static void on_dns(const char *name, const ip_addr_t *ip, void *arg)
{
	(void) name;
	(void) arg;
	if (ip)
		addr = *ip;
	dns_ok = ip != NULL;
	dns_done = true;
}

static void start_connect(void)
{
	pcb = tcp_new_ip_type(IP_GET_TYPE(&addr));
	if (!pcb) {
		fail("out of memory");
		return;
	}
	tcp_arg(pcb, NULL);
	tcp_recv(pcb, on_recv);
	tcp_err(pcb, on_err);
	/* a terminal sends a keystroke at a time and wants it seen now */
	tcp_nagle_disable(pcb);
	state = NS_CONNECTING;
	if (tcp_connect(pcb, &addr, req_port, on_connected) != ERR_OK) {
		detach(pcb);
		tcp_abort(pcb);
		pcb = NULL;
		fail("connect failed");
	}
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	(void) arg;
	if (err != ERR_OK || !newpcb)
		return ERR_VAL;
	/* one line: a second caller gets turned away, as with a busy signal */
	if (pcb || state != NS_IDLE || req != REQ_NONE) {
		tcp_abort(newpcb);
		return ERR_ABRT;
	}
	pcb = newpcb;
	tcp_arg(pcb, NULL);
	tcp_recv(pcb, on_recv);
	tcp_err(pcb, on_err);
	tcp_nagle_disable(pcb);
	snprintf(peer, sizeof(peer), "%s:%u", ipaddr_ntoa(&pcb->remote_ip),
		 pcb->remote_port);
	error = "";
	state = NS_RINGING;
	return ERR_OK;
}

static void stop_listening(void)
{
	if (listen_pcb) {
		tcp_accept(listen_pcb, NULL);
		tcp_close(listen_pcb);
		listen_pcb = NULL;
	}
	listen_port = 0;
}

static void start_listening(void)
{
	struct tcp_pcb *p = tcp_new_ip_type(IPADDR_TYPE_ANY);

	if (!p) {
		listen_error = "out of memory";
		listen_want = 0;
		return;
	}
	if (tcp_bind(p, IP_ANY_TYPE, listen_want) != ERR_OK) {
		tcp_close(p);
		listen_error = "port in use";
		listen_want = 0;
		return;
	}
	listen_pcb = tcp_listen_with_backlog(p, 1);
	if (!listen_pcb) {
		tcp_close(p);
		listen_error = "out of memory";
		listen_want = 0;
		return;
	}
	tcp_accept(listen_pcb, on_accept);
	listen_error = "";
	listen_port = listen_want;
}

static void do_close(void)
{
	if (pcb) {
		detach(pcb);
		if (tcp_close(pcb) != ERR_OK)
			tcp_abort(pcb);
		pcb = NULL;
	}
	if (pending) {
		pbuf_free(pending);
		pending = NULL;
	}
}

static void do_open(void)
{
	err_t e;

	do_close();
	dns_done = false;
	deadline = make_timeout_time_ms(req_timeout);
	e = dns_gethostbyname(req_host, &addr, on_dns, NULL);
	if (e == ERR_OK)
		start_connect();
	else if (e != ERR_INPROGRESS)
		fail("bad host name");
}

static void drain_pending(void)
{
	uint16_t n = rx_put(pending, pending_off);

	pending_off += n;
	if (n && pcb)
		tcp_recved(pcb, n);
	if (pending_off >= pending->tot_len) {
		pbuf_free(pending);
		pending = NULL;
	}
}

static void push_tx(void)
{
	uint32_t tail = tx_tail, head = tx_head;
	uint32_t n, chunk;
	bool wrote = false;

	__dmb();
	while (tail != head) {
		chunk = TX_SIZE - (tail & (TX_SIZE - 1));
		n = head - tail;
		if (n > chunk)
			n = chunk;
		if (n > tcp_sndbuf(pcb))
			n = tcp_sndbuf(pcb);
		if (!n || tcp_write(pcb, &tx_buf[tail & (TX_SIZE - 1)], n,
				    TCP_WRITE_FLAG_COPY) != ERR_OK)
			break;
		tail += n;
		wrote = true;
	}
	if (wrote) {
		tx_tail = tail;
		tcp_output(pcb);
	}
}

void ns_service(bool lwip_up)
{
	int r = req;

	if (r != REQ_NONE) {
		if (!lwip_up) {
			if (r == REQ_OPEN)
				fail("no network");
			else if (r == REQ_CLOSE)
				state = NS_IDLE;
		} else {
			cyw43_arch_lwip_begin();
			switch (r) {
			case REQ_OPEN:
				do_open();
				break;
			case REQ_CLOSE:
				do_close();
				state = NS_IDLE;
				break;
			case REQ_LISTEN:
				if (listen_port != listen_want)
					stop_listening();
				break;
			case REQ_ANSWER:
				if (state == NS_RINGING)
					state = NS_CONNECTED;
				break;
			}
			cyw43_arch_lwip_end();
		}
		__dmb();
		req = REQ_NONE;
	}

	if (!lwip_up)
		return;

	/* also picks the listener up once the radio starts */
	if (listen_want && !listen_pcb) {
		cyw43_arch_lwip_begin();
		start_listening();
		cyw43_arch_lwip_end();
	}

	switch (state) {
	case NS_RESOLVING:
		if (dns_done) {
			cyw43_arch_lwip_begin();
			if (dns_ok)
				start_connect();
			else
				fail("host not found");
			cyw43_arch_lwip_end();
		} else if (time_reached(deadline))
			fail("host not found");
		break;

	case NS_CONNECTING:
		if (time_reached(deadline)) {
			cyw43_arch_lwip_begin();
			if (pcb) {
				detach(pcb);
				tcp_abort(pcb);
				pcb = NULL;
			}
			cyw43_arch_lwip_end();
			fail("no answer");
		}
		break;

	case NS_RINGING:
	case NS_CONNECTED:
	case NS_CLOSED:
		if (!pending && (!pcb || tx_head == tx_tail))
			break;
		cyw43_arch_lwip_begin();
		if (pending)
			drain_pending();
		if (pcb && state == NS_CONNECTED)
			push_tx();
		cyw43_arch_lwip_end();
		break;

	default:
		break;
	}
}
