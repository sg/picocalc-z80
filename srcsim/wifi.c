/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * WiFi station and NTP clock set. See wifi.h.
 *
 * Join and scan handling follow picocalc_lua's drivers/wifi.c, the NTP
 * exchange follows Udo Munk's net_ntp.c; both are reworked to never block.
 */

#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "net_vars.h"
#include "netsock.h"
#include "http.h"
#include "rtc_compat.h"
#include "wifi.h"

#define JOIN_TIMEOUT_MS		30000
/* the driver reports transient failures while the link is still settling */
#define JOIN_SETTLE_MS		2000
#define REJOIN_DELAY_MS		30000
#define LINK_POLL_MS		100
#define RSSI_POLL_MS		5000

#define NTP_PORT		123
#define NTP_MSG_LEN		48
#define NTP_DELTA		2208988800u	/* 1900 to 1970 */
#define NTP_TRY_MS		5000
#define NTP_TRIES		3

enum { REQ_NONE, REQ_START, REQ_CONNECT, REQ_DISCONNECT, REQ_SCAN };

/* written by core 1 before req is set, read by core 0 after */
static volatile int req;
static char req_country[3];
static char req_ssid[WIFI_SSID_LEN+1];
static char req_password[WIFI_PWD_LEN+1];

static volatile wifi_state_t state = WIFI_OFF;
static const char *volatile fail_reason = "";
static volatile uint32_t ip4;
static volatile int rssi;
static char cur_ssid[WIFI_SSID_LEN+1];
static char cur_password[WIFI_PWD_LEN+1];
static bool want_up;
static absolute_time_t join_start, next_link_poll, next_rssi_poll;
static absolute_time_t rejoin_at;
static bool rejoin_pending;

static wifi_scan_result_t scan_results[WIFI_SCAN_MAX];
static volatile int scan_n;
static volatile bool scanning;

static struct udp_pcb *ntp_pcb;
static ip_addr_t ntp_addr;
static enum { NTP_IDLE, NTP_RESOLVE, NTP_WAIT_DNS, NTP_WAIT_REPLY } ntp_phase;
static int ntp_tries;
static absolute_time_t ntp_deadline;
static volatile bool ntp_dns_done, ntp_dns_ok, ntp_got;
static volatile uint32_t ntp_epoch;
static volatile bool clock_synced;

/* ------------------------------------------------------------------ */
/* core 1 side                                                         */

static void post(int r)
{
	while (req != REQ_NONE)
		sleep_ms(1);
	__dmb();
	req = r;
}

void wifi_start(const char *country)
{
	while (req != REQ_NONE)
		sleep_ms(1);
	strncpy(req_country, country ? country : "", sizeof(req_country) - 1);
	post(REQ_START);
}

void wifi_connect(const char *ssid, const char *password)
{
	while (req != REQ_NONE)
		sleep_ms(1);
	strncpy(req_ssid, ssid, WIFI_SSID_LEN);
	req_ssid[WIFI_SSID_LEN] = '\0';
	strncpy(req_password, password, WIFI_PWD_LEN);
	req_password[WIFI_PWD_LEN] = '\0';
	post(REQ_CONNECT);
}

void wifi_disconnect(void)
{
	post(REQ_DISCONNECT);
}

void wifi_scan(void)
{
	post(REQ_SCAN);
}

bool wifi_busy(void)
{
	return req != REQ_NONE || state == WIFI_JOINING || scanning;
}

bool wifi_wait(uint32_t timeout_ms)
{
	absolute_time_t until = make_timeout_time_ms(timeout_ms);

	while (wifi_busy()) {
		if (time_reached(until))
			return false;
		sleep_ms(20);
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* shared readers                                                      */

wifi_state_t wifi_get_state(void)
{
	return state;
}

const char *wifi_fail_reason(void)
{
	return fail_reason;
}

bool wifi_get_ip(char *buf, size_t len)
{
	uint32_t a = ip4;

	if (state != WIFI_UP || !a)
		return false;
	snprintf(buf, len, "%u.%u.%u.%u", (unsigned) (a & 0xff),
		 (unsigned) ((a >> 8) & 0xff), (unsigned) ((a >> 16) & 0xff),
		 (unsigned) (a >> 24));
	return true;
}

const char *wifi_get_ssid(void)
{
	return cur_ssid;
}

int wifi_get_rssi(void)
{
	return rssi;
}

bool wifi_scan_busy(void)
{
	return scanning;
}

int wifi_scan_count(void)
{
	return scanning ? 0 : scan_n;
}

const wifi_scan_result_t *wifi_scan_result(int i)
{
	return (i >= 0 && i < wifi_scan_count()) ? &scan_results[i] : NULL;
}

bool wifi_clock_synced(void)
{
	return clock_synced;
}

void wifi_status_text(char *buf, size_t len)
{
	char ip[16];

	switch (state) {
	case WIFI_OFF:
		snprintf(buf, len, "off");
		break;
	case WIFI_IDLE:
		snprintf(buf, len, "not connected");
		break;
	case WIFI_JOINING:
		snprintf(buf, len, "connecting to %s", cur_ssid);
		break;
	case WIFI_UP:
		wifi_get_ip(ip, sizeof(ip));
		snprintf(buf, len, "%s %s %ddBm", cur_ssid, ip, rssi);
		break;
	case WIFI_FAILED:
		snprintf(buf, len, "%s: %s", cur_ssid, fail_reason);
		break;
	}
}

/* ------------------------------------------------------------------ */
/* core 0 side                                                         */

static void fail(const char *why)
{
	fail_reason = why;
	ip4 = 0;
	state = WIFI_FAILED;
}

static void do_start(void)
{
	uint32_t country = CYW43_COUNTRY_WORLDWIDE;

	if (state != WIFI_OFF)
		return;
	if (strlen(req_country) == 2)
		country = CYW43_COUNTRY(req_country[0] & ~0x20,
					req_country[1] & ~0x20, 0);
	if (cyw43_arch_init_with_country(country)) {
		fail_reason = "radio did not start";
		return;
	}
	cyw43_arch_enable_sta_mode();
	state = WIFI_IDLE;
}

static void do_join(void)
{
	uint32_t auth = cur_password[0] ? CYW43_AUTH_WPA2_AES_PSK
					: CYW43_AUTH_OPEN;

	rejoin_pending = false;
	ip4 = 0;
	if (cyw43_arch_wifi_connect_async(cur_ssid,
					  cur_password[0] ? cur_password : NULL,
					  auth)) {
		fail("join request refused");
		/* the driver refuses while it is still tearing the old link
		   down, so this deserves another go like any other failure;
		   without it a drop could leave the machine off the air */
		if (want_up) {
			rejoin_pending = true;
			rejoin_at = make_timeout_time_ms(REJOIN_DELAY_MS);
		}
		return;
	}
	fail_reason = "";
	join_start = get_absolute_time();
	state = WIFI_JOINING;
}

static void do_leave(void)
{
	if (state == WIFI_JOINING || state == WIFI_UP)
		cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
	ip4 = 0;
}

static int scan_cb(void *env, const cyw43_ev_scan_result_t *r)
{
	int i, n = scan_n;

	(void) env;
	if (!r->ssid_len)
		return 0;
	/* access points repeat per BSSID and per beacon; keep the strongest */
	for (i = 0; i < n; i++) {
		if (strlen(scan_results[i].ssid) == r->ssid_len &&
		    !memcmp(scan_results[i].ssid, r->ssid, r->ssid_len)) {
			if (r->rssi > scan_results[i].rssi) {
				scan_results[i].rssi = r->rssi;
				scan_results[i].channel = r->channel;
				scan_results[i].auth = r->auth_mode;
			}
			return 0;
		}
	}
	if (n >= WIFI_SCAN_MAX)
		return 0;
	memcpy(scan_results[n].ssid, r->ssid, r->ssid_len);
	scan_results[n].ssid[r->ssid_len] = '\0';
	scan_results[n].rssi = r->rssi;
	scan_results[n].channel = r->channel;
	scan_results[n].auth = r->auth_mode;
	scan_n = n + 1;
	return 0;
}

static void do_scan(void)
{
	cyw43_wifi_scan_options_t opts = { 0 };

	if (state == WIFI_OFF || state == WIFI_JOINING)
		return;
	scan_n = 0;
	scanning = true;
	if (cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_cb))
		scanning = false;
}

static void scan_finish(void)
{
	int i, j, n = scan_n;
	wifi_scan_result_t tmp;

	for (i = 1; i < n; i++) {
		tmp = scan_results[i];
		for (j = i; j > 0 && scan_results[j - 1].rssi < tmp.rssi; j--)
			scan_results[j] = scan_results[j - 1];
		scan_results[j] = tmp;
	}
	scanning = false;
}

/* NTP: callbacks run in lwIP's context, the rest in wifi_service() */

static void ntp_dns_cb(const char *name, const ip_addr_t *addr, void *arg)
{
	(void) name;
	(void) arg;
	if (addr)
		ntp_addr = *addr;
	ntp_dns_ok = addr != NULL;
	ntp_dns_done = true;
}

static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
		     const ip_addr_t *addr, u16_t port)
{
	uint8_t b[4];

	(void) arg;
	(void) pcb;
	if (ip_addr_cmp(addr, &ntp_addr) && port == NTP_PORT &&
	    p->tot_len == NTP_MSG_LEN && (pbuf_get_at(p, 0) & 7) == 4 &&
	    pbuf_get_at(p, 1) != 0) {
		pbuf_copy_partial(p, b, 4, 40);
		ntp_epoch = ((uint32_t) b[0] << 24 | (uint32_t) b[1] << 16 |
			     (uint32_t) b[2] << 8 | b[3]) - NTP_DELTA;
		ntp_got = true;
	}
	pbuf_free(p);
}

static void ntp_send(void)
{
	struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);

	if (!p)
		return;
	memset(p->payload, 0, NTP_MSG_LEN);
	((uint8_t *) p->payload)[0] = 0x1b;	/* v3, client */
	udp_sendto(ntp_pcb, p, &ntp_addr, NTP_PORT);
	pbuf_free(p);
}

static void ntp_begin(void)
{
	ntp_tries = 0;
	ntp_got = false;
	clock_synced = false;
	ntp_phase = NTP_RESOLVE;
}

static void ntp_set_clock(void)
{
	time_t local = (time_t) ntp_epoch + (time_t) utc_offset * 3600;
	struct tm tm;
	datetime_t dt;

	/* the status repaint calls localtime() from an IRQ on this core */
	gmtime_r(&local, &tm);
	dt = (datetime_t) {
		.year = tm.tm_year + 1900, .month = tm.tm_mon + 1,
		.day = tm.tm_mday, .dotw = tm.tm_wday,
		.hour = tm.tm_hour, .min = tm.tm_min, .sec = tm.tm_sec
	};

	rtc_set_datetime(&dt);
	clock_synced = true;
}

static void ntp_poll(void)
{
	err_t err;

	switch (ntp_phase) {
	case NTP_IDLE:
		return;

	case NTP_RESOLVE:
		if (++ntp_tries > NTP_TRIES) {
			ntp_phase = NTP_IDLE;
			return;
		}
		cyw43_arch_lwip_begin();
		if (!ntp_pcb) {
			ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
			if (ntp_pcb)
				udp_recv(ntp_pcb, ntp_recv, NULL);
		}
		ntp_dns_done = false;
		err = ntp_pcb ? dns_gethostbyname(ntp_server, &ntp_addr,
						  ntp_dns_cb, NULL) : ERR_MEM;
		if (err == ERR_OK) {
			ntp_send();
			ntp_phase = NTP_WAIT_REPLY;
		} else if (err == ERR_INPROGRESS)
			ntp_phase = NTP_WAIT_DNS;
		cyw43_arch_lwip_end();
		ntp_deadline = make_timeout_time_ms(NTP_TRY_MS);
		return;

	case NTP_WAIT_DNS:
		if (ntp_dns_done) {
			if (ntp_dns_ok) {
				cyw43_arch_lwip_begin();
				ntp_send();
				cyw43_arch_lwip_end();
				ntp_phase = NTP_WAIT_REPLY;
				ntp_deadline = make_timeout_time_ms(NTP_TRY_MS);
			} else
				ntp_phase = NTP_RESOLVE;
		} else if (time_reached(ntp_deadline))
			ntp_phase = NTP_RESOLVE;
		return;

	case NTP_WAIT_REPLY:
		if (ntp_got) {
			ntp_set_clock();
			ntp_phase = NTP_IDLE;
		} else if (time_reached(ntp_deadline))
			ntp_phase = NTP_RESOLVE;
		return;
	}
}

static void link_poll(void)
{
	int st;
	int64_t ms;

	if (!time_reached(next_link_poll))
		return;
	next_link_poll = make_timeout_time_ms(LINK_POLL_MS);

	st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

	switch (state) {
	case WIFI_JOINING:
		ms = absolute_time_diff_us(join_start, get_absolute_time())
		     / 1000;
		if (st == CYW43_LINK_UP) {
			ip4 = ip4_addr_get_u32(
				netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]));
			state = WIFI_UP;
			next_rssi_poll = get_absolute_time();
			ntp_begin();
		} else if (st < 0 && ms > JOIN_SETTLE_MS) {
			cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
			fail(st == CYW43_LINK_BADAUTH ? "wrong password" :
			     st == CYW43_LINK_NONET ? "network not found" :
			     "join failed");
		} else if (ms > JOIN_TIMEOUT_MS) {
			cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
			fail(st == CYW43_LINK_NOIP ? "no address from DHCP" :
			     "timed out");
		}
		if (state == WIFI_FAILED && want_up &&
		    st != CYW43_LINK_BADAUTH) {
			rejoin_pending = true;
			rejoin_at = make_timeout_time_ms(REJOIN_DELAY_MS);
		}
		break;

	case WIFI_UP:
		if (st != CYW43_LINK_UP) {
			ip4 = 0;
			if (want_up)
				do_join();
			else
				state = WIFI_IDLE;
		}
		break;

	case WIFI_FAILED:
		if (rejoin_pending && time_reached(rejoin_at))
			do_join();
		break;

	default:
		break;
	}
}

void wifi_service(void)
{
	int r = req;
	int32_t v;

	if (r != REQ_NONE) {
		switch (r) {
		case REQ_START:
			do_start();
			break;
		case REQ_CONNECT:
			if (state == WIFI_OFF)
				do_start();
			if (state == WIFI_OFF)
				break;
			do_leave();
			strcpy(cur_ssid, req_ssid);
			strcpy(cur_password, req_password);
			want_up = true;
			do_join();
			break;
		case REQ_DISCONNECT:
			want_up = false;
			rejoin_pending = false;
			do_leave();
			if (state != WIFI_OFF)
				state = WIFI_IDLE;
			break;
		case REQ_SCAN:
			do_scan();
			break;
		}
		__dmb();
		req = REQ_NONE;
	}

	ns_service(state != WIFI_OFF);
	http_service(state != WIFI_OFF);

	if (state == WIFI_OFF)
		return;

	link_poll();

	if (scanning && !cyw43_wifi_scan_active(&cyw43_state))
		scan_finish();

	if (state == WIFI_UP) {
		ntp_poll();
		if (time_reached(next_rssi_poll)) {
			next_rssi_poll = make_timeout_time_ms(RSSI_POLL_MS);
			if (!cyw43_wifi_get_rssi(&cyw43_state, &v))
				rssi = v;
		}
	}
}
