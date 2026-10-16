/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * A small HTTP client. See http.h.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"

#include "sim.h"
#include "http.h"

#define HOST_LEN	63
#define CONNECT_MS	15000
#define STALL_MS	30000
#define REDIRECTS	5

enum { REQ_NONE, REQ_OPEN, REQ_CLOSE };

typedef enum {
	HS_IDLE,
	HS_RESOLVING,
	HS_CONNECTING,
	HS_CONNECTED,
	HS_CLOSED,	/* the far end is done; unread data remains */
	HS_FAILED
} hstate_t;

/* the lent work area */
static uint8_t *rx_buf;		/* HTTP_RING_SIZE */
static char *hdr;		/* HTTP_HDR_SIZE */
static char *next_url;		/* HTTP_URL_MAX */
static char *req_buf;		/* HTTP_REQ_SIZE */

static volatile uint32_t rx_head, rx_tail;	/* head: core 0, tail: core 1 */
static volatile int req;
static char req_host[HOST_LEN+1];
static uint16_t req_port;
static bool req_tls;
static int req_len;
static volatile hstate_t state = HS_IDLE;
static const char *volatile error = "";

volatile uint32_t http_bytes, http_total;
volatile bool http_active;

/* core 0 only */
static struct altcp_pcb *pcb;
static struct pbuf *pending;
static uint16_t pending_off;
static ip_addr_t addr;
static volatile bool dns_done, dns_ok;
static absolute_time_t deadline;

/* ------------------------------------------------------------------ */
/* core 0 side                                                         */

static void fail(const char *why)
{
	error = why;
	state = HS_FAILED;
}

/* copy what fits of p from off into the ring */
static uint16_t rx_put(struct pbuf *p, uint16_t off)
{
	uint32_t head = rx_head;
	uint32_t space = HTTP_RING_SIZE - (head - rx_tail);
	uint32_t n = p->tot_len - off, i, chunk;

	if (n > space)
		n = space;
	for (i = 0; i < n; i += chunk) {
		chunk = HTTP_RING_SIZE - ((head + i) & (HTTP_RING_SIZE - 1));
		if (chunk > n - i)
			chunk = n - i;
		pbuf_copy_partial(p, &rx_buf[(head + i) & (HTTP_RING_SIZE - 1)],
				  chunk, off + i);
	}
	__dmb();
	rx_head = head + n;
	return n;
}

static void detach(struct altcp_pcb *c)
{
	altcp_arg(c, NULL);
	altcp_recv(c, NULL);
	altcp_err(c, NULL);
}

static err_t on_recv(void *arg, struct altcp_pcb *c, struct pbuf *p,
		     err_t err)
{
	uint16_t n;

	(void) arg;
	(void) err;
	if (!p) {
		detach(c);
		pcb = NULL;
		state = HS_CLOSED;
		if (altcp_close(c) != ERR_OK) {
			altcp_abort(c);
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
		altcp_recved(c, n);
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
	if (state == HS_CONNECTING)
		fail(err == ERR_RST ? "connection refused" : "no answer");
	else if (state == HS_CONNECTED) {
		error = "connection reset";
		state = HS_CLOSED;
	}
}

static err_t on_connected(void *arg, struct altcp_pcb *c, err_t err)
{
	(void) arg;
	if (err != ERR_OK)
		return err;
	/* the request was built before the connection was asked for */
	if (altcp_write(c, req_buf, req_len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
		fail("send failed");
		return ERR_OK;
	}
	altcp_output(c);
	state = HS_CONNECTED;
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
	pcb = altcp_tcp_new_ip_type(IP_GET_TYPE(&addr));
	if (!pcb) {
		fail("out of memory");
		return;
	}
	altcp_arg(pcb, NULL);
	altcp_recv(pcb, on_recv);
	altcp_err(pcb, on_err);
	state = HS_CONNECTING;
	if (altcp_connect(pcb, &addr, req_port, on_connected) != ERR_OK) {
		detach(pcb);
		altcp_abort(pcb);
		pcb = NULL;
		fail("connect failed");
	}
}

static void do_close(void)
{
	if (pcb) {
		detach(pcb);
		if (altcp_close(pcb) != ERR_OK)
			altcp_abort(pcb);
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
	deadline = make_timeout_time_ms(CONNECT_MS);
	state = HS_RESOLVING;
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
		altcp_recved(pcb, n);
	if (pending_off >= pending->tot_len) {
		pbuf_free(pending);
		pending = NULL;
	}
}

void http_service(bool lwip_up)
{
	int r = req;

	if (r != REQ_NONE) {
		if (!lwip_up) {
			if (r == REQ_OPEN)
				fail("no network");
		} else {
			cyw43_arch_lwip_begin();
			if (r == REQ_OPEN)
				do_open();
			else
				do_close();
			cyw43_arch_lwip_end();
		}
		__dmb();
		req = REQ_NONE;
	}

	if (!lwip_up)
		return;

	switch (state) {
	case HS_RESOLVING:
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

	case HS_CONNECTING:
		if (time_reached(deadline)) {
			cyw43_arch_lwip_begin();
			if (pcb) {
				detach(pcb);
				altcp_abort(pcb);
				pcb = NULL;
			}
			cyw43_arch_lwip_end();
			fail("no answer");
		}
		break;

	case HS_CONNECTED:
	case HS_CLOSED:
		if (pending) {
			cyw43_arch_lwip_begin();
			drain_pending();
			cyw43_arch_lwip_end();
		}
		break;

	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* core 1 side                                                         */

typedef struct {
	bool tls;
	char host[HOST_LEN+1];
	uint16_t port;
	const char *path;	/* into the URL string */
} url_t;

void http_init(uint8_t *work)
{
	rx_buf = work;
	hdr = (char *) work + HTTP_RING_SIZE;
	next_url = hdr + HTTP_HDR_SIZE;
	req_buf = next_url + HTTP_URL_MAX;
}

static void post(int r)
{
	while (req != REQ_NONE)
		sleep_ms(1);
	__dmb();
	req = r;
	/* core 0 sets the state while it takes the request */
	while (req != REQ_NONE)
		sleep_ms(1);
}

static bool url_parse(const char *url, url_t *u, const char **err)
{
	const char *p, *h;
	size_t n;

	if (!strncasecmp(url, "http://", 7)) {
		u->tls = false;
		p = url + 7;
	} else if (!strncasecmp(url, "https://", 8)) {
		u->tls = true;
		p = url + 8;
	} else {
		*err = "the URL must start with http:// or https://";
		return false;
	}
	u->port = u->tls ? 443 : 80;
	h = p;
	while (*p && *p != '/' && *p != ':' && *p != '?')
		p++;
	n = p - h;
	if (!n || n > HOST_LEN) {
		*err = "bad host name in the URL";
		return false;
	}
	memcpy(u->host, h, n);
	u->host[n] = '\0';
	if (*p == ':') {
		u->port = (uint16_t) atoi(p + 1);
		p++;
		while (isdigit((unsigned char) *p))
			p++;
	}
	u->path = (*p == '/') ? p : "/";
	return true;
}

static bool send_request(const url_t *u, const char **err)
{
	char hostport[HOST_LEN + 8];
	int n;

	if (u->port == (u->tls ? 443 : 80))
		strcpy(hostport, u->host);
	else
		snprintf(hostport, sizeof(hostport), "%s:%u", u->host, u->port);
	n = snprintf(req_buf, HTTP_REQ_SIZE,
		     "GET %s HTTP/1.1\r\n"
		     "Host: %s\r\n"
		     "User-Agent: picocalc-z80/%s\r\n"
		     "Accept: */*\r\n"
		     "Connection: close\r\n\r\n",
		     u->path, hostport, USR_REL);
	if (n >= HTTP_REQ_SIZE) {
		*err = "URL too long";
		return false;
	}
	req_len = n;
#ifndef PICOCALC_TLS
	if (u->tls) {
		*err = "https is not supported on this board";
		return false;
	}
#endif
	strcpy(req_host, u->host);
	req_port = u->port;
	req_tls = u->tls;
	rx_head = rx_tail = 0;
	http_bytes = http_total = 0;
	error = "";
	post(REQ_OPEN);
	while (state == HS_RESOLVING || state == HS_CONNECTING)
		sleep_ms(1);
	if (state == HS_FAILED) {
		*err = error;
		return false;
	}
	return true;
}

/* >0 bytes read, 0 at the end of the reply, -1 when nothing came in time */
static int read_some(uint8_t *buf, int max, uint32_t timeout_ms)
{
	absolute_time_t t = make_timeout_time_ms(timeout_ms);
	uint32_t avail, n, i, chunk;

	for (;;) {
		avail = rx_head - rx_tail;
		if (avail) {
			n = avail < (uint32_t) max ? avail : (uint32_t) max;
			for (i = 0; i < n; i += chunk) {
				chunk = HTTP_RING_SIZE -
					((rx_tail + i) & (HTTP_RING_SIZE - 1));
				if (chunk > n - i)
					chunk = n - i;
				memcpy(buf + i,
				       &rx_buf[(rx_tail + i) & (HTTP_RING_SIZE - 1)],
				       chunk);
			}
			__dmb();
			rx_tail += n;
			return n;
		}
		if (state == HS_CLOSED || state == HS_FAILED)
			return 0;
		if (time_reached(t))
			return -1;
		sleep_ms(1);
	}
}

/* a line without its CR LF; false when the reply ended first */
static bool read_line(char *line, int max)
{
	int n = 0;
	uint8_t c;

	for (;;) {
		if (read_some(&c, 1, STALL_MS) <= 0)
			return false;
		if (c == '\n')
			break;
		if (c != '\r' && n < max - 1)
			line[n++] = c;
	}
	line[n] = '\0';
	return true;
}

static const char *value_of(const char *line, const char *name)
{
	size_t n = strlen(name);

	if (strncasecmp(line, name, n) || line[n] != ':')
		return NULL;
	line += n + 1;
	while (*line == ' ' || *line == '\t')
		line++;
	return line;
}

static bool read_headers(http_reply_t *r, bool *chunked, const char **err)
{
	char *line, *end;
	const char *v;
	int n = 0, got;

	/* everything up to the blank line, byte by byte so the body stays
	   in the ring */
	while (n < 4 || memcmp(hdr + n - 4, "\r\n\r\n", 4)) {
		if (n >= HTTP_HDR_SIZE - 1) {
			*err = "reply headers too long";
			return false;
		}
		got = read_some((uint8_t *) hdr + n, 1, STALL_MS);
		if (got < 0) {
			*err = "no reply";
			return false;
		}
		if (got == 0) {
			*err = "closed before replying";
			return false;
		}
		n++;
	}
	hdr[n] = '\0';

	r->length = -1;
	r->received = 0;
	r->location = NULL;
	*chunked = false;
	if (sscanf(hdr, "HTTP/%*d.%*d %d", &r->status) != 1) {
		*err = "not an HTTP reply";
		return false;
	}
	for (line = strstr(hdr, "\r\n") + 2; (end = strstr(line, "\r\n")) &&
	     end > line; line = end + 2) {
		*end = '\0';
		if ((v = value_of(line, "Content-Length")))
			r->length = atol(v);
		else if ((v = value_of(line, "Location"))) {
			strncpy(next_url, v, HTTP_URL_MAX - 1);
			next_url[HTTP_URL_MAX - 1] = '\0';
			r->location = next_url;
		} else if ((v = value_of(line, "Transfer-Encoding")))
			*chunked = !strncasecmp(v, "chunked", 7);
	}
	return true;
}

static bool deliver(http_reply_t *r, http_sink_t sink, void *arg,
		    const uint8_t *buf, int n, const char **err)
{
	if (sink && !sink(buf, n, arg)) {
		*err = "could not write the file";
		return false;
	}
	r->received += n;
	http_bytes = r->received;
	return true;
}

/* left < 0 reads to the end of the reply */
static bool read_bytes(http_reply_t *r, long left, http_sink_t sink,
		       void *arg, const char **err)
{
	uint8_t *buf = (uint8_t *) hdr;	/* the headers are done with */
	int want, got;

	while (left != 0) {
		want = HTTP_HDR_SIZE;
		if (left > 0 && left < want)
			want = left;
		got = read_some(buf, want, STALL_MS);
		if (got < 0) {
			*err = "the download stalled";
			return false;
		}
		if (got == 0) {
			if (left < 0)
				return true;
			*err = "the connection closed early";
			return false;
		}
		if (!deliver(r, sink, arg, buf, got, err))
			return false;
		if (left > 0)
			left -= got;
	}
	return true;
}

static bool read_body(http_reply_t *r, bool chunked, http_sink_t sink,
		      void *arg, const char **err)
{
	char line[20];
	long size;

	http_total = (r->length > 0) ? r->length : 0;
	if (!chunked)
		return read_bytes(r, r->length, sink, arg, err);

	/* a hex length on a line, that many bytes, CR LF, until a zero */
	for (;;) {
		if (!read_line(line, sizeof(line))) {
			*err = "the connection closed early";
			return false;
		}
		size = strtol(line, NULL, 16);
		if (size <= 0)
			return true;
		if (!read_bytes(r, size, sink, arg, err))
			return false;
		read_line(line, sizeof(line));
	}
}

/* a relative Location becomes absolute on the host it came from */
static bool resolve_location(const url_t *u, const char **err)
{
	char prefix[HOST_LEN + 16];
	size_t plen, llen;

	if (!strncasecmp(next_url, "http://", 7) ||
	    !strncasecmp(next_url, "https://", 8))
		return true;
	if (next_url[0] != '/') {
		*err = "unsupported redirect";
		return false;
	}
	if (u->port == (u->tls ? 443 : 80))
		plen = snprintf(prefix, sizeof(prefix), "http%s://%s",
				u->tls ? "s" : "", u->host);
	else
		plen = snprintf(prefix, sizeof(prefix), "http%s://%s:%u",
				u->tls ? "s" : "", u->host, u->port);
	llen = strlen(next_url);
	if (plen + llen >= HTTP_URL_MAX) {
		*err = "redirect URL too long";
		return false;
	}
	memmove(next_url + plen, next_url, llen + 1);
	memcpy(next_url, prefix, plen);
	return true;
}

bool http_get(const char *url, http_sink_t sink, void *arg,
	      http_reply_t *reply, const char **err)
{
	static char status_err[32];
	url_t u;
	bool chunked, ok = false;
	int hops = 0;

	http_active = true;
	reply->status = 0;
	for (;;) {
		if (!url_parse(url, &u, err) || !send_request(&u, err) ||
		    !read_headers(reply, &chunked, err))
			break;
		if (reply->status / 100 == 3 && reply->location) {
			if (++hops > REDIRECTS) {
				*err = "too many redirects";
				break;
			}
			if (!resolve_location(&u, err))
				break;
			post(REQ_CLOSE);
			url = next_url;
			continue;
		}
		if (reply->status != 200) {
			snprintf(status_err, sizeof(status_err),
				 "the server answered %d", reply->status);
			*err = status_err;
			break;
		}
		ok = read_body(reply, chunked, sink, arg, err);
		break;
	}
	post(REQ_CLOSE);
	http_active = false;
	return ok;
}

bool http_probe(const char *url, http_reply_t *reply, const char **err)
{
	url_t u;
	bool chunked, ok;

	http_active = true;
	reply->status = 0;
	ok = url_parse(url, &u, err) && send_request(&u, err) &&
	     read_headers(reply, &chunked, err);
	post(REQ_CLOSE);
	http_active = false;
	return ok;
}
