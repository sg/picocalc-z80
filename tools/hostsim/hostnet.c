/*
 * netsock.h over POSIX sockets, and stand-ins for wifi.h, so the modem in
 * srcsim/modem.c runs unchanged on a PC. The host's own network plays the
 * part of a WiFi link that is always up.
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "modem.h"
#include "netsock.h"
#include "wifi.h"

#define RX_SIZE 4096

static int fd = -1;
static ns_state_t state = NS_IDLE;
static const char *error = "";
static uint8_t rx_buf[RX_SIZE];
static int rx_len;
static uint64_t deadline;
static int lfd = -1;
static uint16_t lport;
static const char *lerror = "";
static char peer[80];

static void fail(const char *why)
{
	if (fd >= 0)
		close(fd);
	fd = -1;
	error = why;
	state = NS_FAILED;
}

static void take_calls(void)
{
	struct sockaddr_storage sa;
	socklen_t sl = sizeof(sa);
	char ip[64] = "?";
	int c, one = 1;

	if (lfd < 0 || (c = accept(lfd, (struct sockaddr *) &sa, &sl)) < 0)
		return;
	if (fd >= 0 || state != NS_IDLE) {
		close(c);	/* busy */
		return;
	}
	if (sa.ss_family == AF_INET)
		inet_ntop(AF_INET, &((struct sockaddr_in *) &sa)->sin_addr,
			  ip, sizeof(ip));
	snprintf(peer, sizeof(peer), "%s:%u", ip,
		 ntohs(((struct sockaddr_in *) &sa)->sin_port));
	fcntl(c, F_SETFL, fcntl(c, F_GETFL, 0) | O_NONBLOCK);
	setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	fd = c;
	rx_len = 0;
	error = "";
	state = NS_RINGING;
}

static void pump(void)
{
	struct pollfd p = { .fd = fd, .events = POLLOUT };
	int e = 0, n;
	socklen_t len = sizeof(e);

	take_calls();
	if (fd < 0)
		return;
	if (state == NS_CONNECTING) {
		if (poll(&p, 1, 0) > 0) {
			getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &len);
			if (e)
				fail(e == ECONNREFUSED ? "connection refused"
						       : "no answer");
			else
				state = NS_CONNECTED;
		} else if (modem_time_us() > deadline)
			fail("no answer");
		return;
	}
	if ((state == NS_CONNECTED || state == NS_RINGING) &&
	    rx_len < RX_SIZE) {
		n = (int) recv(fd, rx_buf + rx_len, RX_SIZE - rx_len, 0);
		if (n > 0)
			rx_len += n;
		else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
			close(fd);
			fd = -1;
			error = "closed by remote";
			state = NS_CLOSED;
		}
	}
}

void ns_service(bool lwip_up)
{
	(void) lwip_up;
	pump();
}

void ns_open(const char *host, uint16_t port, uint32_t timeout_ms)
{
	struct addrinfo hints = { .ai_family = AF_UNSPEC,
				  .ai_socktype = SOCK_STREAM }, *res;
	char service[8];
	int one = 1;

	ns_close();
	rx_len = 0;
	error = "";
	snprintf(service, sizeof(service), "%u", port);
	snprintf(peer, sizeof(peer), "%s:%u", host, port);
	/* a PC can afford to block on DNS; the Pico's version does not */
	if (getaddrinfo(host, service, &hints, &res)) {
		fail("host not found");
		return;
	}
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		freeaddrinfo(res);
		fail("out of sockets");
		return;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	deadline = modem_time_us() + (uint64_t) timeout_ms * 1000;
	state = NS_CONNECTING;
	if (connect(fd, res->ai_addr, res->ai_addrlen) && errno != EINPROGRESS)
		fail(errno == ECONNREFUSED ? "connection refused"
					   : "connect failed");
	freeaddrinfo(res);
}

void ns_close(void)
{
	if (fd >= 0)
		close(fd);
	fd = -1;
	rx_len = 0;
	state = NS_IDLE;
}

ns_state_t ns_state(void)
{
	pump();
	return state;
}

const char *ns_error(void)
{
	return error;
}

int ns_avail(void)
{
	pump();
	return rx_len;
}

int ns_read(uint8_t *buf, int max)
{
	int n;

	pump();
	n = rx_len < max ? rx_len : max;
	memcpy(buf, rx_buf, n);
	memmove(rx_buf, rx_buf + n, rx_len - n);
	rx_len -= n;
	return n;
}

int ns_write(const uint8_t *buf, int len)
{
	ssize_t n;

	if (fd < 0 || state != NS_CONNECTED)
		return 0;
	n = send(fd, buf, len, MSG_NOSIGNAL);
	return n > 0 ? (int) n : 0;
}

int ns_tx_space(void)
{
	return 2048;
}

void ns_listen(uint16_t port)
{
	struct sockaddr_in sa = { .sin_family = AF_INET,
				  .sin_addr.s_addr = htonl(INADDR_ANY) };
	int one = 1;

	if (port == lport)
		return;
	if (lfd >= 0)
		close(lfd);
	lfd = -1;
	lport = 0;
	if (!port)
		return;
	lfd = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	sa.sin_port = htons(port);
	if (bind(lfd, (struct sockaddr *) &sa, sizeof(sa)) || listen(lfd, 1)) {
		close(lfd);
		lfd = -1;
		lerror = "port in use";
		return;
	}
	fcntl(lfd, F_SETFL, fcntl(lfd, F_GETFL, 0) | O_NONBLOCK);
	lerror = "";
	lport = port;
}

uint16_t ns_listen_port(void) { return lport; }
const char *ns_listen_error(void) { return lerror; }

void ns_answer(void)
{
	if (state == NS_RINGING)
		state = NS_CONNECTED;
}

void ns_peer(char *buf, size_t len)
{
	snprintf(buf, len, "%s", peer);
}

/* WiFi: always up, nothing to scan */

void wifi_service(void) { }
void wifi_start(const char *country) { (void) country; }
void wifi_connect(const char *ssid, const char *pw) { (void) ssid; (void) pw; }
void wifi_disconnect(void) { }
void wifi_scan(void) { }
bool wifi_busy(void) { return false; }
bool wifi_wait(uint32_t ms) { (void) ms; return true; }
wifi_state_t wifi_get_state(void) { return WIFI_UP; }
const char *wifi_fail_reason(void) { return ""; }
const char *wifi_get_ssid(void) { return "host"; }
int wifi_get_rssi(void) { return 0; }
bool wifi_scan_busy(void) { return false; }
int wifi_scan_count(void) { return 0; }
const wifi_scan_result_t *wifi_scan_result(int i) { (void) i; return NULL; }
bool wifi_clock_synced(void) { return true; }

bool wifi_get_ip(char *buf, size_t len)
{
	snprintf(buf, len, "127.0.0.1");
	return true;
}

/* modem platform hooks */

uint64_t modem_time_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void modem_save_settings(void) { }

void modem_term_size(int *cols, int *rows)
{
	*cols = 80;
	*rows = 24;
}
