/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The WiFi menu. See wifimenu.h.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "term.h"

#include "sim.h"
#include "simdefs.h"

#include "net_vars.h"
#include "wifi.h"
#include "simcfg.h"
#include "scrollback.h"
#include "overlay.h"
#include "wifimenu.h"

#define ROWS		15
#define TICK_MS		250	/* how often the status row follows the radio */

enum { R_NET = 2, R_STATUS = 3, R_AUTO = 8, R_CY = 9, R_NTP = 10,
       R_MODEM = 11, R_MSG = 13 };

/* everything the box can change, for deciding whether to save on close */
typedef struct {
	char ssid[WIFI_SSID_LEN + 1], pw[WIFI_PWD_LEN + 1];
	char ntp[HOST_NAME_MAX + 1], modem[MODEM_INIT_LEN + 1], country[3];
	int autoconnect, utc;
} settings_t;

static void snapshot(settings_t *s)
{
	strcpy(s->ssid, wifi_ssid);
	strcpy(s->pw, wifi_password);
	strcpy(s->ntp, ntp_server);
	strcpy(s->modem, modem_init_str);
	strcpy(s->country, wifi_country);
	s->autoconnect = wifi_autoconnect;
	s->utc = utc_offset;
}

static bool changed(const settings_t *s)
{
	return strcmp(s->ssid, wifi_ssid) || strcmp(s->pw, wifi_password) ||
	       strcmp(s->ntp, ntp_server) || strcmp(s->modem, modem_init_str) ||
	       strcmp(s->country, wifi_country) ||
	       s->autoconnect != wifi_autoconnect || s->utc != utc_offset;
}

static void draw_status(void)
{
	char buf[OV_MAX_COLS + 40];
	u16 fg;

	switch (wifi_get_state()) {
	case WIFI_UP:		fg = OV_FG; break;
	case WIFI_JOINING:	fg = OV_TITLE; break;
	case WIFI_FAILED:	fg = OV_WARN; break;
	default:		fg = OV_DIM; break;
	}
	strcpy(buf, "Status:   ");
	wifi_status_text(buf + strlen(buf), sizeof(buf) - strlen(buf));
	ov_line(R_STATUS, buf, fg, OV_BG);
}

static void draw(void)
{
	char buf[OV_MAX_COLS + 80];

	ov_edge(0, "WiFi");
	ov_line(1, "", OV_FG, OV_BG);
	snprintf(buf, sizeof(buf), "Network:  %s",
		 wifi_ssid[0] ? wifi_ssid : "(none)");
	ov_line(R_NET, buf, OV_FG, OV_BG);
	draw_status();
	ov_line(4, "", OV_FG, OV_BG);
	ov_line(5, "C: connect          D: disconnect", OV_FG, OV_BG);
	ov_line(6, "S: scan for a network", OV_FG, OV_BG);
	ov_line(7, "N: network name     P: password", OV_FG, OV_BG);
	snprintf(buf, sizeof(buf), "A: connect at boot: %s",
		 wifi_autoconnect ? "yes" : "no");
	ov_line(R_AUTO, buf, OV_FG, OV_BG);
	snprintf(buf, sizeof(buf), "Y: country: %-9s U: UTC offset: %d",
		 wifi_country[0] ? wifi_country : "worldwide", utc_offset);
	ov_line(R_CY, buf, OV_FG, OV_BG);
	snprintf(buf, sizeof(buf), "T: NTP server: %s", ntp_server);
	ov_line(R_NTP, buf, OV_FG, OV_BG);
	snprintf(buf, sizeof(buf), "M: modem init: %s", modem_init_str);
	ov_line(R_MODEM, buf, OV_FG, OV_BG);
	ov_line(12, "Esc: close", OV_FG, OV_BG);
	ov_line(R_MSG, ov_msg, OV_TITLE, OV_BG);
	ov_edge(ROWS - 1, "");
}

static void connect(void)
{
	if (!wifi_ssid[0]) {
		snprintf(ov_msg, sizeof(ov_msg),
			 "No network chosen: S scans, N names one");
		return;
	}
	wifi_connect(wifi_ssid, wifi_password);
	snprintf(ov_msg, sizeof(ov_msg), "Connecting to %.30s", wifi_ssid);
}

static void password(void)
{
	char pw[WIFI_PWD_LEN + 1] = "";

	if (!ov_edit(R_MSG, "Password: ", pw, sizeof(pw)))
		return;
	strcpy(wifi_password, pw);
	snprintf(ov_msg, sizeof(ov_msg), pw[0] ? "Password set" :
		 "Password cleared");
}

static void country(void)
{
	char buf[3];

	strcpy(buf, wifi_country);
	if (!ov_edit(R_CY, "Y: country, two letters or none: ", buf, sizeof(buf)))
		return;
	buf[0] = toupper((unsigned char) buf[0]);
	buf[1] = toupper((unsigned char) buf[1]);
	if (!buf[0])
		wifi_country[0] = '\0';
	else if (isalpha((unsigned char) buf[0]) && isalpha((unsigned char) buf[1]))
		strcpy(wifi_country, buf);
	else {
		snprintf(ov_msg, sizeof(ov_msg), "Two letters, or none for worldwide");
		return;
	}
	/* the code is given to the radio when it starts */
	if (wifi_get_state() != WIFI_OFF)
		snprintf(ov_msg, sizeof(ov_msg), "Applies from the next boot");
}

static void utc(void)
{
	char buf[5];
	int v;

	snprintf(buf, sizeof(buf), "%d", utc_offset);
	if (!ov_edit(R_CY, "U: UTC offset in hours: ", buf, sizeof(buf)))
		return;
	v = atoi(buf);
	if (buf[0] && v >= -12 && v <= 14)
		utc_offset = v;
	else
		snprintf(ov_msg, sizeof(ov_msg), "The offset runs from -12 to 14");
}

static void ntp(void)
{
	if (!ov_edit(R_NTP, "T: NTP server: ", ntp_server, HOST_NAME_MAX + 1))
		return;
	if (!ntp_server[0])
		strcpy(ntp_server, DEFAULT_NTP);
}

static void draw_scan(int n, int sel, int top)
{
	char buf[OV_MAX_COLS + 40];
	const wifi_scan_result_t *r;
	int rows = ov_rows() - 5, i, k;

	ov_edge(0, "Networks");
	ov_line(1, "", OV_FG, OV_BG);
	for (i = 0; i < rows; i++) {
		k = top + i;
		if (k >= n) {
			ov_line(2 + i, "", OV_FG, OV_BG);
			continue;
		}
		r = wifi_scan_result(k);
		snprintf(buf, sizeof(buf), "%-24.24s %4d dBm %s", r->ssid,
			 r->rssi, !r->auth ? "open" : (r->auth & 4) ? "WPA2" :
			 (r->auth & 2) ? "WPA" : "WEP");
		ov_line(2 + i, buf, (k == sel) ? OV_BG : OV_FG,
			(k == sel) ? OV_FG : OV_BG);
	}
	ov_text(2, ov_cols() - 3, OV_DIM, OV_BG, (top > 0) ? "^" : " ");
	ov_text(1 + rows, ov_cols() - 3, OV_DIM, OV_BG,
		(top + rows < n) ? "v" : " ");
	ov_line(ov_rows() - 3, "Enter: join   Esc: back", OV_FG, OV_BG);
	ov_line(ov_rows() - 2, ov_msg, OV_TITLE, OV_BG);
	ov_edge(ov_rows() - 1, "");
}

/* scan, pick a network, take its password and join it */
static void scan(void)
{
	const wifi_scan_result_t *r;
	char pw[WIFI_PWD_LEN + 1] = "";
	int n, rows = ov_rows() - 5, sel = 0, top = 0;
	input_event_t ev;

	if (wifi_get_state() == WIFI_OFF) {
		wifi_start(wifi_country);
		wifi_wait(5000);
		if (wifi_get_state() == WIFI_OFF) {
			snprintf(ov_msg, sizeof(ov_msg), "WiFi: %s",
				 wifi_fail_reason());
			return;
		}
	}
	if (wifi_get_state() == WIFI_JOINING) {
		snprintf(ov_msg, sizeof(ov_msg),
			 "Busy connecting, try again in a moment");
		return;
	}
	wifi_scan();
	snprintf(ov_msg, sizeof(ov_msg), "Scanning...");
	draw();
	while (wifi_busy()) {
		ev = ov_wait_key(TICK_MS);
		if (ev.code == KEY_ESC) {
			ov_msg[0] = '\0';
			return;
		}
	}
	n = wifi_scan_count();
	if (!n) {
		snprintf(ov_msg, sizeof(ov_msg), "No networks found");
		return;
	}

	ov_msg[0] = '\0';
	for (;;) {
		if (sel < top)
			top = sel;
		if (sel >= top + rows)
			top = sel - rows + 1;
		draw_scan(n, sel, top);
		ev = ov_wait_key(0);
		if (ev.code == KEY_UP && sel > 0)
			sel--;
		else if (ev.code == KEY_DOWN && sel < n - 1)
			sel++;
		else if (ev.code == KEY_PAGEUP)
			sel = (sel > rows) ? sel - rows : 0;
		else if (ev.code == KEY_PAGEDOWN)
			sel = (sel + rows < n) ? sel + rows : n - 1;
		else if (ev.code == KEY_HOME)
			sel = 0;
		else if (ev.code == KEY_END)
			sel = n - 1;
		else if (ev.code == KEY_ESC)
			return;
		else if (ev.code == KEY_ENTER)
			break;
	}

	r = wifi_scan_result(sel);
	if (strcmp(wifi_ssid, r->ssid)) {
		strcpy(wifi_ssid, r->ssid);
		wifi_password[0] = '\0';
	}
	if (r->auth) {
		if (wifi_password[0])
			snprintf(ov_msg, sizeof(ov_msg),
				 "Enter alone keeps the current password");
		ov_line(ov_rows() - 2, ov_msg, OV_TITLE, OV_BG);
		if (!ov_edit(ov_rows() - 3, "Password: ", pw, sizeof(pw)))
			return;
		if (pw[0])
			strcpy(wifi_password, pw);
		else if (!wifi_password[0]) {
			snprintf(ov_msg, sizeof(ov_msg),
				 "%.30s needs a password", r->ssid);
			return;
		}
	} else
		wifi_password[0] = '\0';
	connect();
}

void wifi_menu(void)
{
	settings_t before;
	wifi_state_t was = wifi_get_state(), now;
	input_event_t ev;
	bool done = false;

	snapshot(&before);
	scrollback_leave();
	ov_open(ROWS);
	draw();
	while (!done) {
		ev = ov_wait_key(TICK_MS);
		if (!ev.state) {
			/* a join moves on while the box waits */
			now = wifi_get_state();
			if (now != was) {
				if (was == WIFI_JOINING && now == WIFI_UP)
					snprintf(ov_msg, sizeof(ov_msg),
						 "Connected");
				else if (was == WIFI_JOINING && now == WIFI_FAILED)
					snprintf(ov_msg, sizeof(ov_msg), "%s",
						 wifi_fail_reason());
				was = now;
				draw_status();
				ov_line(R_MSG, ov_msg, OV_TITLE, OV_BG);
			}
			continue;
		}
		ov_msg[0] = '\0';
		switch (tolower(ev.code)) {
		case 'c':
			connect();
			break;
		case 'd':
			if (wifi_get_state() != WIFI_OFF) {
				wifi_disconnect();
				snprintf(ov_msg, sizeof(ov_msg), "Disconnected");
			}
			break;
		case 's':
			scan();
			break;
		case 'n':
			ov_edit(R_NET, "Network:  ", wifi_ssid, WIFI_SSID_LEN + 1);
			break;
		case 'p':
			password();
			break;
		case 'a':
			wifi_autoconnect = !wifi_autoconnect;
			break;
		case 'y':
			country();
			break;
		case 'u':
			utc();
			break;
		case 't':
			ntp();
			break;
		case 'm':
			ov_edit(R_MODEM, "M: modem init: ", modem_init_str,
				MODEM_INIT_LEN + 1);
			break;
		case KEY_ESC:
			done = true;
			break;
		}
		was = wifi_get_state();
		draw();
	}
	ov_close();

	/* what the config dialog saved when the machine started is saved here
	   as it changes, since the box is also reached while it runs */
	if (changed(&before))
		save_config();
}
