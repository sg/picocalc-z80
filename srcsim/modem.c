/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Hayes compatible modem over TCP. See modem.h.
 *
 * Based on generic-at-modem.c from z80pack, Copyright (C) 2019-2021 by
 * David McNaughton, MIT licence. Telnet negotiation is libtelnet by Sean
 * Middleditch, public domain.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libtelnet.h"

#include "modem.h"
#include "netsock.h"
#include "net_vars.h"
#include "wifi.h"

#define CMD_MAX		160	/* AT+WIFI="32 chars","63 chars" */
#define OUT_SIZE	1024	/* power of two */
#define TX_MARGIN	8	/* room for one byte after telnet IAC doubling */
#define DEFAULT_PORT	23
#define NREGS		50
#define RING_US		3000000		/* z80pack's ring cadence */
#define RING_MAX	15		/* then the caller is turned away */

/* S-registers, numbered as in z80pack where they exist there */
enum {
	S_RINGS = 0,		/* answer after this many rings, 0 never */
	S_RINGCNT = 1,
	S_ESC = 2,		/* escape character */
	S_CR = 3,
	S_LF = 4,
	S_BS = 5,
	S_WAITCD = 7,		/* seconds to wait for a connection */
	S_GUARD = 12,		/* escape guard time in 1/50 s */
	S_LISTEN = 14,		/* port AT&A listens on */
	S_TELNET = 15,		/* 0 raw TCP, 1 telnet */
	S_TN_SGA = 16,		/* telnet options: bit 0 WILL, bit 1 DO */
	S_TN_ECHO = 17,
	S_TN_BIN = 18,
	S_TN_NAWS = 19,
	S_COLS = 20,		/* window size reported by NAWS */
	S_ROWS = 21,
	S_TN_TTYPE = 22
};

/* numeric result codes are the Hayes ones */
enum { R_OK = 0, R_CONNECT = 1, R_RING = 2, R_NOCARRIER = 3, R_ERROR = 4,
       R_NODIALTONE = 6, R_BUSY = 7, R_NOANSWER = 8, R_NONE = -1 };
static const char *const result_text[] = {
	"OK", "CONNECT", "RING", "NO CARRIER", "ERROR", "", "NO DIALTONE",
	"BUSY", "NO ANSWER"
};

static int sreg[NREGS];
static int sreg_sel;
static bool echo, quiet, verbose;
static bool daemon_mode;		/* AT&A1: a BBS owns the line, no +++ escape */
static char ttype[33];

static enum { M_CMD, M_DIAL, M_ONLINE } mode;
static bool connected;		/* a connection exists, online or not */
static bool answered;		/* and we are the called end */
static bool rx_cr;		/* last byte from a caller was CR */
static uint64_t next_ring_us;

/* bytes for the Z80; only core 1 touches them */
static uint8_t out_buf[OUT_SIZE];
static unsigned out_head, out_tail;
static uint8_t last_in;

/* command line collection */
static enum { P_IDLE, P_A, P_AT } pstate;
static char cmd[CMD_MAX + 1];
static int cmd_len;
static char last_cmd[CMD_MAX + 1];
static bool running_init;

/* online escape: the +++ has to be framed by guard times */
static uint64_t last_tx_us, plus_us;
static int plus_count;

static telnet_t *tn;
static telnet_telopt_t tn_opts[6];

static enum { W_NONE, W_SCAN, W_JOIN } wifi_op;
static char join_ssid[WIFI_SSID_LEN + 1];
static char join_password[WIFI_PWD_LEN + 1];

/* ------------------------------------------------------------------ */
/* output to the Z80                                                   */

static unsigned out_free(void)
{
	return OUT_SIZE - (out_head - out_tail);
}

static void out_c(char c)
{
	if (running_init || !out_free())
		return;
	out_buf[out_head++ & (OUT_SIZE - 1)] = (uint8_t) c;
}

static void out_s(const char *s)
{
	while (*s)
		out_c(*s++);
}

static void out_crlf(void)
{
	out_c((char) sreg[S_CR]);
	out_c((char) sreg[S_LF]);
}

/* an information line, shown even in quiet mode as real modems do */
static void out_line(const char *s)
{
	out_crlf();
	out_s(s);
}

static void result(int code)
{
	char num[8];

	if (code == R_NONE || quiet)
		return;
	if (verbose) {
		out_crlf();
		out_s(result_text[code]);
		out_crlf();
	} else {
		snprintf(num, sizeof(num), "%d", code);
		out_s(num);
		out_c((char) sreg[S_CR]);
	}
}

/* ------------------------------------------------------------------ */
/* telnet                                                              */

static void tn_build_opts(void)
{
	static const short opts[] = {
		TELNET_TELOPT_SGA, TELNET_TELOPT_ECHO, TELNET_TELOPT_BINARY,
		TELNET_TELOPT_NAWS, TELNET_TELOPT_TTYPE
	};
	static const int regs[] = {
		S_TN_SGA, S_TN_ECHO, S_TN_BIN, S_TN_NAWS, S_TN_TTYPE
	};
	int i;

	for (i = 0; i < 5; i++) {
		tn_opts[i].telopt = opts[i];
		tn_opts[i].us = (sreg[regs[i]] & 1) ? TELNET_WILL : TELNET_WONT;
		tn_opts[i].him = (sreg[regs[i]] & 2) ? TELNET_DO : TELNET_DONT;
	}
	tn_opts[5].telopt = -1;
}

/* as the called end we echo, so the caller's telnet goes character mode */
static const telnet_telopt_t tn_server_opts[] = {
	{ TELNET_TELOPT_SGA, TELNET_WILL, TELNET_DO },
	{ TELNET_TELOPT_ECHO, TELNET_WILL, TELNET_DONT },
	{ TELNET_TELOPT_BINARY, TELNET_WILL, TELNET_DO },
	{ TELNET_TELOPT_NAWS, TELNET_WONT, TELNET_DO },
	{ -1, 0, 0 }
};

static void tn_event(telnet_t *t, telnet_event_t *ev, void *user)
{
	unsigned char naws[4];
	size_t i;

	(void) user;
	switch (ev->type) {
	case TELNET_EV_DATA:
		/* telnet_recv() is never fed more than out_free() bytes */
		for (i = 0; i < ev->data.size; i++) {
			uint8_t c = (uint8_t) ev->data.buffer[i];

			/* a caller's Enter is CR LF or CR NUL; CP/M wants CR */
			if (answered && rx_cr && (c == '\n' || c == 0)) {
				rx_cr = false;
				continue;
			}
			rx_cr = c == '\r';
			out_buf[out_head++ & (OUT_SIZE - 1)] = c;
		}
		break;
	case TELNET_EV_SEND:
		ns_write((const uint8_t *) ev->data.buffer,
			 (int) ev->data.size);
		break;
	case TELNET_EV_DO:
		if (ev->neg.telopt == TELNET_TELOPT_NAWS) {
			naws[0] = (unsigned char) (sreg[S_COLS] >> 8);
			naws[1] = (unsigned char) sreg[S_COLS];
			naws[2] = (unsigned char) (sreg[S_ROWS] >> 8);
			naws[3] = (unsigned char) sreg[S_ROWS];
			telnet_begin_sb(t, TELNET_TELOPT_NAWS);
			telnet_send(t, (const char *) naws, 4);
			telnet_finish_sb(t);
		}
		break;
	case TELNET_EV_TTYPE:
		if (ev->ttype.cmd == TELNET_TTYPE_SEND)
			telnet_ttype_is(t, ttype);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* connection                                                          */

static void send_byte(uint8_t c)
{
	if (tn)
		telnet_send(tn, (const char *) &c, 1);
	else
		ns_write(&c, 1);
}

static void hang_up(void)
{
	if (tn) {
		telnet_free(tn);
		tn = NULL;
	}
	ns_close();
	connected = false;
	answered = false;
	plus_count = 0;
	sreg[S_RINGCNT] = 0;
	mode = M_CMD;
}

static void answer(void)
{
	ns_answer();
	connected = true;
	answered = true;
	rx_cr = false;
	sreg[S_RINGCNT] = 0;
	plus_count = 0;
	last_tx_us = modem_time_us();
	mode = M_ONLINE;
	if (sreg[S_TELNET]) {
		tn = telnet_init(tn_server_opts, tn_event, 0, NULL);
		if (tn) {
			telnet_negotiate(tn, TELNET_WILL, TELNET_TELOPT_ECHO);
			telnet_negotiate(tn, TELNET_WILL, TELNET_TELOPT_SGA);
		}
	}
	result(R_CONNECT);
}

static bool ringing(void)
{
	return !connected && mode == M_CMD && ns_state() == NS_RINGING;
}

static bool parse_host(char *s, char *host, int hlen, uint16_t *port)
{
	char *p;
	long v;
	int n;

	while (*s == ' ')
		s++;
	/* T and P are dial modifiers, as on Hayes; so ATDT before a host
	   that itself starts with T or P */
	if (*s == 'T' || *s == 't' || *s == 'P' || *s == 'p')
		s++;
	while (*s == ' ')
		s++;
	n = strcspn(s, ": ");
	if (n == 0 || n >= hlen)
		return false;
	memcpy(host, s, n);
	host[n] = '\0';
	*port = DEFAULT_PORT;
	p = s + n;
	while (*p == ' ' || *p == ':')
		p++;
	if (*p) {
		v = strtol(p, NULL, 10);
		if (v <= 0 || v > 65535)
			return false;
		*port = (uint16_t) v;
	}
	return true;
}

static int dial(char *s)
{
	char host[64];
	uint16_t port;

	if (connected)
		return R_ERROR;
	if (wifi_get_state() != WIFI_UP)
		return R_NODIALTONE;
	if (!parse_host(s, host, sizeof(host), &port))
		return R_ERROR;
	ns_open(host, port, (uint32_t) sreg[S_WAITCD] * 1000);
	mode = M_DIAL;
	return R_NONE;
}

/* ------------------------------------------------------------------ */
/* AT+WIFI                                                             */

static void wifi_report(void)
{
	char line[96], ip[16];

	switch (wifi_get_state()) {
	case WIFI_OFF:
		out_line("WIFI: OFF");
		break;
	case WIFI_IDLE:
		out_line("WIFI: DOWN");
		break;
	case WIFI_JOINING:
		snprintf(line, sizeof(line), "WIFI: JOINING %s",
			 wifi_get_ssid());
		out_line(line);
		break;
	case WIFI_UP:
		wifi_get_ip(ip, sizeof(ip));
		snprintf(line, sizeof(line), "WIFI: UP %s %s %d dBm",
			 wifi_get_ssid(), ip, wifi_get_rssi());
		out_line(line);
		break;
	case WIFI_FAILED:
		snprintf(line, sizeof(line), "WIFI: FAILED %s: %s",
			 wifi_get_ssid(), wifi_fail_reason());
		out_line(line);
		break;
	}
	snprintf(line, sizeof(line), "SAVED: %s, connect at boot %s",
		 wifi_ssid[0] ? wifi_ssid : "(none)",
		 wifi_autoconnect ? "on" : "off");
	out_line(line);
	if (ns_listen_port())
		snprintf(line, sizeof(line), "LISTEN: port %u, answer after %d rings",
			 ns_listen_port(), sreg[S_RINGS]);
	else if (ns_listen_error()[0])
		snprintf(line, sizeof(line), "LISTEN: failed: %s",
			 ns_listen_error());
	else
		snprintf(line, sizeof(line), "LISTEN: off");
	out_line(line);
	if (connected || ringing()) {
		char who[72];

		ns_peer(who, sizeof(who));
		snprintf(line, sizeof(line), "%s: %s",
			 ringing() ? "RINGING" : answered ? "CALLER" : "CALLED",
			 who);
		out_line(line);
	}
}

/* copy a possibly quoted argument, keeping its case */
static char *take_arg(char *p, char *dst, int len)
{
	int n = 0;
	char end = ',';

	if (*p == '"') {
		end = '"';
		p++;
	}
	while (*p && *p != end) {
		if (n < len)
			dst[n++] = *p;
		p++;
	}
	dst[n] = '\0';
	if (end == '"' && *p == '"')
		p++;
	if (*p == ',')
		p++;
	return p;
}

static void wifi_join(const char *ssid, const char *password)
{
	strcpy(join_ssid, ssid);
	strcpy(join_password, password);
	if (wifi_get_state() == WIFI_OFF)
		wifi_start(wifi_country);
	wifi_connect(join_ssid, join_password);
	wifi_op = W_JOIN;
}

static int cmd_wifi(char *p)
{
	char a[WIFI_SSID_LEN + 1], b[WIFI_PWD_LEN + 1], kw[8];
	const wifi_scan_result_t *r;
	int i;

	if (*p == '?') {
		wifi_report();
		return R_OK;
	}
	if (*p != '=')
		return R_ERROR;
	p++;
	if (wifi_op != W_NONE)
		return R_ERROR;

	for (i = 0; i < 7 && isalpha((unsigned char) p[i]); i++)
		kw[i] = (char) toupper((unsigned char) p[i]);
	kw[i] = '\0';

	if (!strcmp(kw, "SCAN") && !p[i]) {
		if (wifi_get_state() == WIFI_OFF)
			wifi_start(wifi_country);
		wifi_scan();
		wifi_op = W_SCAN;
		return R_OK;
	}
	if (!strcmp(kw, "UP") && !p[i]) {
		if (!wifi_ssid[0])
			return R_ERROR;
		wifi_join(wifi_ssid, wifi_password);
		return R_OK;
	}
	if (!strcmp(kw, "DOWN") && !p[i]) {
		wifi_disconnect();
		return R_OK;
	}
	if (!strcmp(kw, "AUTO") && p[i] == ',') {
		wifi_autoconnect = p[i + 1] == '1';
		modem_save_settings();
		return R_OK;
	}
	if (isdigit((unsigned char) *p)) {
		i = atoi(p);
		if ((r = wifi_scan_result(i - 1)) == NULL)
			return R_ERROR;
		while (isdigit((unsigned char) *p))
			p++;
		if (*p == ',')
			p++;
		take_arg(p, b, WIFI_PWD_LEN);
		wifi_join(r->ssid, b);
		return R_OK;
	}
	if (*p == '"') {
		p = take_arg(p, a, WIFI_SSID_LEN);
		take_arg(p, b, WIFI_PWD_LEN);
		if (!a[0])
			return R_ERROR;
		wifi_join(a, b);
		return R_OK;
	}
	return R_ERROR;
}

static void wifi_poll(void)
{
	char line[96], ip[16];
	const wifi_scan_result_t *r;
	int i, n;

	if (wifi_op == W_NONE || wifi_busy())
		return;

	if (wifi_op == W_SCAN) {
		n = wifi_scan_count();
		for (i = 0; i < n; i++) {
			r = wifi_scan_result(i);
			snprintf(line, sizeof(line), "%2d %-32s %4d dBm %s",
				 i + 1, r->ssid, r->rssi,
				 !r->auth ? "open" : (r->auth & 4) ? "WPA2" :
				 (r->auth & 2) ? "WPA" : "WEP");
			out_line(line);
		}
		snprintf(line, sizeof(line), "SCAN DONE, %d FOUND", n);
		out_line(line);
		out_crlf();
	} else if (wifi_get_state() == WIFI_UP) {
		wifi_get_ip(ip, sizeof(ip));
		snprintf(line, sizeof(line), "WIFI CONNECTED %s", ip);
		out_line(line);
		out_crlf();
		strcpy(wifi_ssid, join_ssid);
		strcpy(wifi_password, join_password);
		modem_save_settings();
	} else {
		snprintf(line, sizeof(line), "WIFI FAILED: %s",
			 wifi_fail_reason());
		out_line(line);
		out_crlf();
	}
	wifi_op = W_NONE;
}

/* ------------------------------------------------------------------ */
/* commands                                                            */

static void defaults(void)
{
	int cols, rows;

	memset(sreg, 0, sizeof(sreg));
	sreg[S_ESC] = '+';
	sreg[S_CR] = '\r';
	sreg[S_LF] = '\n';
	sreg[S_BS] = '\b';
	sreg[S_WAITCD] = 30;
	sreg[S_GUARD] = 50;
	sreg[S_LISTEN] = 8023;
	sreg[S_TELNET] = 1;
	sreg[S_TN_SGA] = 3;
	sreg[S_TN_ECHO] = 2;	/* the far end echoes, we never do */
	sreg[S_TN_BIN] = 3;	/* 8 bit clean, for XMODEM over telnet */
	sreg[S_TN_NAWS] = 1;
	sreg[S_TN_TTYPE] = 1;
	modem_term_size(&cols, &rows);
	sreg[S_COLS] = cols;
	sreg[S_ROWS] = rows;
	sreg_sel = 0;
	echo = true;
	quiet = false;
	verbose = true;
	daemon_mode = false;
	strcpy(ttype, "ANSI");
}

static int get_num(char **pp)
{
	int v = 0;

	while (isdigit((unsigned char) **pp))
		v = v * 10 + (*(*pp)++ - '0');
	return v;
}

static void help(void)
{
	static const char *const lines[] = {
		"ATD host[:port]  dial, port 23 if none",
		"ATH  hang up     ATO  back online   +++  command mode",
		"ATZ  reset       AT&F factory defaults",
		"ATE0/1 echo      ATQ0/1 quiet       ATV0/1 words",
		"ATSn=v  ATSn?    S2 escape  S7 connect wait  S12 guard",
		"                 S15 telnet  S20/S21 window size",
		"AT&A  listen on port S14 (8023)    AT&A1  also E0Q1",
		"ATA  answer      ATS0=n  answer after n rings",
		"ATI0 version     ATI1 network",
		"AT+T=name  telnet terminal type, AT+T? to show",
		"AT+WIFI?         WiFi state",
		"AT+WIFI=SCAN     list networks",
		"AT+WIFI=n,\"pw\"   join network n from the scan",
		"AT+WIFI=\"ssid\",\"pw\"  join by name",
		"AT+WIFI=UP|DOWN  AT+WIFI=AUTO,0|1  connect at boot"
	};
	size_t i;

	for (i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
		out_line(lines[i]);
}

static int execute(char *p)
{
	char line[64], c;
	int n, code = R_OK;

	while (*p && code == R_OK) {
		c = (char) toupper((unsigned char) *p++);
		switch (c) {
		case ' ':
			break;

		case 'D':
			return dial(p);

		case 'H':
			n = get_num(&p);
			if (n == 0 && (connected || ringing()))
				hang_up();
			break;

		case 'O':
			get_num(&p);
			if (!connected)
				return R_NOCARRIER;
			mode = M_ONLINE;
			last_tx_us = modem_time_us();
			return R_CONNECT;

		case 'Z':
			get_num(&p);
			modem_init();
			return R_OK;

		case 'E':
			echo = get_num(&p) != 0;
			break;

		case 'Q':
			quiet = get_num(&p) != 0;
			break;

		case 'V':
			verbose = get_num(&p) != 0;
			break;

		/* accepted for the sake of common init strings */
		case 'B': case 'L': case 'M': case 'N': case 'P': case 'T':
		case 'W': case 'X': case 'Y':
			get_num(&p);
			break;

		case '&':
			c = (char) toupper((unsigned char) *p++);
			n = get_num(&p);
			if (c == 'F')
				defaults();
			else if (c == 'A') {
				/* z80pack: any &A listens, &A1 also quietly */
				ns_listen((uint16_t) sreg[S_LISTEN]);
				if (n) {
					daemon_mode = true;
					echo = false;
					quiet = true;
				}
			}
			else if (!c || !strchr("CDKSW", c))
				code = R_ERROR;
			break;

		case 'I':
			n = get_num(&p);
			if (n == 0)
				out_line("picocalc-z80 AT modem");
			else if (n == 1)
				wifi_report();
			else
				code = R_ERROR;
			break;

		case 'S':
			n = get_num(&p);
			if (n >= NREGS)
				return R_ERROR;
			sreg_sel = n;
			break;

		case '?':
			snprintf(line, sizeof(line), "%03d", sreg[sreg_sel]);
			out_line(line);
			break;

		case '=':
			sreg[sreg_sel] = get_num(&p);
			break;

		case '$':
			help();
			break;

		case 'A':
			if (!ringing())
				return R_NOCARRIER;
			answer();
			return R_NONE;

		case '+':
			if (!strncasecmp(p, "WIFI", 4))
				return cmd_wifi(p + 4);
			if (toupper((unsigned char) *p) == 'T') {
				p++;
				if (*p == '?') {
					out_line(ttype);
					return R_OK;
				}
				if (*p != '=')
					return R_ERROR;
				take_arg(p + 1, ttype, sizeof(ttype) - 1);
				if (!ttype[0])
					strcpy(ttype, "ANSI");
				return R_OK;
			}
			return R_ERROR;

		default:
			code = R_ERROR;
			break;
		}
	}
	return code;
}

static void run_line(void)
{
	cmd[cmd_len] = '\0';
	strcpy(last_cmd, cmd);
	result(execute(cmd));
}

static void command_char(uint8_t c)
{
	if (echo && !running_init)
		out_c((char) c);

	switch (pstate) {
	case P_IDLE:
		if (c == 'A' || c == 'a')
			pstate = P_A;
		break;
	case P_A:
		if (c == 'T' || c == 't') {
			pstate = P_AT;
			cmd_len = 0;
		} else if (c == '/') {
			pstate = P_IDLE;
			strcpy(cmd, last_cmd);
			cmd_len = (int) strlen(cmd);
			result(execute(cmd));
		} else
			pstate = (c == 'A' || c == 'a') ? P_A : P_IDLE;
		break;
	case P_AT:
		if (c == sreg[S_CR]) {
			pstate = P_IDLE;
			run_line();
		} else if (c == sreg[S_BS] || c == 0x7f) {
			if (cmd_len > 0)
				cmd_len--;
		} else if (c >= ' ' && cmd_len < CMD_MAX)
			cmd[cmd_len++] = (char) c;
		break;
	}
}

/* ------------------------------------------------------------------ */
/* polling, driven by the Z80 reading the status port                  */

static void flush_plus(void)
{
	while (plus_count) {
		send_byte((uint8_t) sreg[S_ESC]);
		plus_count--;
	}
}

static void online_poll(void)
{
	uint8_t buf[256];
	unsigned n;
	uint64_t now = modem_time_us();
	uint64_t guard = (uint64_t) sreg[S_GUARD] * 20000;
	ns_state_t st;

	/* +++ only counts with silence on both sides of it */
	if (plus_count && now - plus_us >= guard) {
		if (plus_count == 3) {
			plus_count = 0;
			mode = M_CMD;
			result(R_OK);
			return;
		}
		flush_plus();
		last_tx_us = now;
	}

	n = out_free();
	if (n > sizeof(buf))
		n = sizeof(buf);
	if (n && (n = (unsigned) ns_read(buf, (int) n)) > 0) {
		if (tn)
			telnet_recv(tn, (const char *) buf, n);
		else
			for (unsigned i = 0; i < n; i++)
				out_buf[out_head++ & (OUT_SIZE - 1)] = buf[i];
	}

	st = ns_state();
	if ((st == NS_CLOSED || st == NS_FAILED) && !ns_avail()) {
		hang_up();
		result(R_NOCARRIER);
	}
}

static void poll(void)
{
	ns_state_t st;

	wifi_poll();

	switch (mode) {
	case M_DIAL:
		st = ns_state();
		if (st == NS_CONNECTED) {
			connected = true;
			mode = M_ONLINE;
			plus_count = 0;
			last_tx_us = modem_time_us();
			answered = false;
			if (sreg[S_TELNET]) {
				tn_build_opts();
				tn = telnet_init(tn_opts, tn_event, 0, NULL);
			}
			result(R_CONNECT);
		} else if (st == NS_FAILED || st == NS_CLOSED) {
			int code = strstr(ns_error(), "refused") ? R_BUSY
								  : R_NOANSWER;
			ns_close();
			mode = M_CMD;
			result(code);
		}
		break;

	case M_ONLINE:
		online_poll();
		break;

	case M_CMD:
		if (!connected) {
			st = ns_state();
			if (st == NS_RINGING) {
				uint64_t now = modem_time_us();

				if (sreg[S_RINGCNT] == 0 || now >= next_ring_us) {
					if (sreg[S_RINGCNT] >= RING_MAX) {
						hang_up();
						break;
					}
					sreg[S_RINGCNT]++;
					next_ring_us = now + RING_US;
					result(R_RING);
				}
				if (sreg[S_RINGS] &&
				    sreg[S_RINGCNT] >= sreg[S_RINGS])
					answer();
			} else if (sreg[S_RINGCNT] &&
				   (st == NS_CLOSED || st == NS_FAILED)) {
				/* the caller gave up before we answered */
				ns_close();
				sreg[S_RINGCNT] = 0;
			}
			break;
		}
		/* a dropped line still says so while in command mode */
		{
			st = ns_state();
			if ((st == NS_CLOSED || st == NS_FAILED) &&
			    !ns_avail()) {
				hang_up();
				result(R_NOCARRIER);
			}
		}
		break;
	}
}

/* ------------------------------------------------------------------ */
/* the SIO-2 channel B interface                                       */

void modem_init(void)
{
	char line[MODEM_INIT_LEN + 4];
	const char *s;

	if (connected || mode == M_DIAL || ns_state() == NS_RINGING)
		hang_up();
	ns_listen(0);
	defaults();
	pstate = P_IDLE;
	last_cmd[0] = '\0';

	s = modem_init_string();
	if (!strncasecmp(s, "AT", 2))
		s += 2;
	snprintf(line, sizeof(line), "%s", s);
	running_init = true;
	strcpy(cmd, line);
	execute(cmd);
	running_init = false;
}

const char *modem_init_string(void)
{
	return modem_init_str[0] ? modem_init_str : MODEM_INIT_DEFAULT;
}

bool modem_rx_ready(void)
{
	poll();
	return out_head != out_tail;
}

uint8_t modem_getc(void)
{
	if (out_head != out_tail)
		last_in = out_buf[out_tail++ & (OUT_SIZE - 1)];
	return last_in;
}

bool modem_tx_ready(void)
{
	if (mode != M_ONLINE)
		return true;
	return ns_tx_space() >= TX_MARGIN;
}

void modem_putc(uint8_t c)
{
	uint64_t now;

	switch (mode) {
	case M_CMD:
		command_char(c);
		break;

	case M_DIAL:
		/* any key abandons the call, as on a real modem */
		ns_close();
		mode = M_CMD;
		result(R_NOCARRIER);
		break;

	case M_ONLINE:
		now = modem_time_us();
		if (!daemon_mode && c == sreg[S_ESC] && plus_count < 3 &&
		    (plus_count || now - last_tx_us >=
				   (uint64_t) sreg[S_GUARD] * 20000)) {
			plus_count++;
			plus_us = now;
			break;
		}
		flush_plus();
		send_byte(c);
		last_tx_us = now;
		break;
	}
}

bool modem_carrier(void)
{
	return connected;
}
