/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024-2025 by Udo Munk & Thomas Eberhardt
 *
 * This module configures the machine appropriate for the
 * Z80/8080 software we want to run on it.
 *
 * History:
 * 20-APR-2024 dummy, no configuration implemented yet
 * 12-MAY-2024 implemented configuration dialog
 * 27-MAY-2024 implemented load file
 * 28-MAY-2024 implemented mount/unmount of disk images
 * 03-JUN-2024 added directory list for code files and disk images
 * 24-MAY-2025 separate read/save config file from config and add network config
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "pico/stdlib.h"
#include "pico/time.h"

#include "ff.h"

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simcore.h"
#include "simport.h"
#include "simio.h"
#include "simcfg.h"
#include "rtc_compat.h"
#include "keymap.h"

#include "lcd.h"
#include "term.h"
#include "status.h"
#include "scrollback.h"

#include "disks.h"
#include "picosim.h"

#include "net_vars.h"
#ifdef PICOCALC_WIFI
#include "wifi.h"
#endif

static datetime_t t = { .year = 2024, .month = 1, .day = 1, .dotw = 1,
			.hour = 0, .min = 0, .sec = 0 };
/* kept in the config file so phase 2 can wire it to the keyboard
   controller backlight register without changing the format */
static int brightness = 1000;
static const char *cfg = Z80_DIR CONF_FILE;

char font_file[DISKLEN+1];

/*
 * prompt for a filename
 */
static void prompt_fn(char *s)
{
	printf("Filename: ");
	get_cmdline(s, FNLEN+1);
	while (*s) {
		*s = toupper((unsigned char) *s);
		s++;
	}
}

/*
 * get an integer with range check
 */
static int get_int(const char *prompt, const char *hint,
		   int min_val, int max_val)
{
	int i;
	char s[6];

	for (;;) {
		printf("Enter %s%s: ", prompt, hint);
		get_cmdline(s, 5);
		if (s[0] == '\0')
			return -1;
		i = atoi(s);
		if (i < min_val || i > max_val) {
			printf("Invalid %s: range %d - %d\n",
			       prompt, min_val, max_val);
		} else
			return i;
	}
}

/*
 * Udo's layout had no header. Files from before the password grew start
 * straight with the CPU type, which can never read as this magic.
 */
#define CFG_MAGIC	"PC80"
#define CFG_VERSION	2

/*
 * try to read config file
 */
void read_config(void)
{
	unsigned int br;
	char magic[4];
	uint16_t version = 1;
	char old_password[WIFI_PWD_LEN_V1+1];

	sd_res = f_open(&sd_file, cfg, FA_READ);
	if (sd_res == FR_OK) {
		f_read(&sd_file, magic, sizeof(magic), &br);
		if (br == sizeof(magic) && !memcmp(magic, CFG_MAGIC, 4))
			f_read(&sd_file, &version, sizeof(version), &br);
		else
			f_lseek(&sd_file, 0);

		f_read(&sd_file, &cpu, sizeof(cpu), &br);
		f_read(&sd_file, &speed, sizeof(speed), &br);
		f_read(&sd_file, &fp_value, sizeof(fp_value), &br);
		f_read(&sd_file, &brightness, sizeof(brightness), &br);
		f_read(&sd_file, &t, sizeof(t), &br);
		f_read(&sd_file, &wifi_ssid, sizeof(wifi_ssid), &br);
		if (version == 1) {
			f_read(&sd_file, old_password, sizeof(old_password),
			       &br);
			old_password[WIFI_PWD_LEN_V1] = '\0';
			strcpy(wifi_password, old_password);
		} else
			f_read(&sd_file, &wifi_password, sizeof(wifi_password),
			       &br);
		f_read(&sd_file, &ntp_server, sizeof(ntp_server), &br);
		f_read(&sd_file, &utc_offset, sizeof(utc_offset), &br);
		f_read(&sd_file, &disks[0], DISKLEN+1, &br);
		f_read(&sd_file, &disks[1], DISKLEN+1, &br);
		f_read(&sd_file, &disks[2], DISKLEN+1, &br);
		f_read(&sd_file, &disks[3], DISKLEN+1, &br);
		f_read(&sd_file, &disks[HD_SLOT], DISKLEN+1, &br);
		f_read(&sd_file, &keymap_arrows, sizeof(keymap_arrows), &br);
		f_read(&sd_file, &font_file, DISKLEN+1, &br);
		if (version >= 2) {
			f_read(&sd_file, &wifi_country, sizeof(wifi_country),
			       &br);
			f_read(&sd_file, &wifi_autoconnect,
			       sizeof(wifi_autoconnect), &br);
			f_read(&sd_file, &modem_init_str, sizeof(modem_init_str), &br);
		}
		f_close(&sd_file);

		wifi_ssid[WIFI_SSID_LEN] = '\0';
		wifi_password[WIFI_PWD_LEN] = '\0';
		ntp_server[HOST_NAME_MAX] = '\0';
		wifi_country[2] = '\0';
		modem_init_str[MODEM_INIT_LEN] = '\0';
	}
#if defined(EXCLUDE_I8080) || defined(EXCLUDE_Z80)
	cpu = DEF_CPU;
#endif
}

/*
 * try to save config file
 */
void save_config(void)
{
	unsigned int br;
	uint16_t version = CFG_VERSION;

	sd_res = f_open(&sd_file, cfg, FA_WRITE | FA_CREATE_ALWAYS);
	if (sd_res == FR_OK) {
		f_write(&sd_file, CFG_MAGIC, 4, &br);
		f_write(&sd_file, &version, sizeof(version), &br);
		f_write(&sd_file, &cpu, sizeof(cpu), &br);
		f_write(&sd_file, &speed, sizeof(speed), &br);
		f_write(&sd_file, &fp_value, sizeof(fp_value), &br);
		f_write(&sd_file, &brightness, sizeof(brightness), &br);
		f_write(&sd_file, &t, sizeof(t), &br);
		f_write(&sd_file, &wifi_ssid, sizeof(wifi_ssid), &br);
		f_write(&sd_file, &wifi_password, sizeof(wifi_password), &br);
		f_write(&sd_file, &ntp_server, sizeof(ntp_server), &br);
		f_write(&sd_file, &utc_offset, sizeof(utc_offset), &br);
		f_write(&sd_file, &disks[0], DISKLEN+1, &br);
		f_write(&sd_file, &disks[1], DISKLEN+1, &br);
		f_write(&sd_file, &disks[2], DISKLEN+1, &br);
		f_write(&sd_file, &disks[3], DISKLEN+1, &br);
		f_write(&sd_file, &disks[HD_SLOT], DISKLEN+1, &br);
		f_write(&sd_file, &keymap_arrows, sizeof(keymap_arrows), &br);
		f_write(&sd_file, &font_file, DISKLEN+1, &br);
		f_write(&sd_file, &wifi_country, sizeof(wifi_country), &br);
		f_write(&sd_file, &wifi_autoconnect, sizeof(wifi_autoconnect),
			&br);
		f_write(&sd_file, &modem_init_str, sizeof(modem_init_str), &br);
		f_close(&sd_file);
	}
}

#ifdef PICOCALC_WIFI
/*
 * one line summary of the WiFi state, for both menus
 */
static void net_status(char *buf, size_t len)
{
	char ip[16];

	switch (wifi_get_state()) {
	case WIFI_OFF:
		snprintf(buf, len, "off");
		break;
	case WIFI_IDLE:
		snprintf(buf, len, "not connected");
		break;
	case WIFI_JOINING:
		snprintf(buf, len, "connecting to %s", wifi_get_ssid());
		break;
	case WIFI_UP:
		wifi_get_ip(ip, sizeof(ip));
		snprintf(buf, len, "%s %s %ddBm", wifi_get_ssid(), ip,
			 wifi_get_rssi());
		break;
	case WIFI_FAILED:
		snprintf(buf, len, "%s: %s", wifi_get_ssid(),
			 wifi_fail_reason());
		break;
	}
}

static void net_start(void)
{
	if (wifi_get_state() == WIFI_OFF) {
		wifi_start(wifi_country);
		wifi_wait(5000);
		if (wifi_get_state() == WIFI_OFF)
			printf("WiFi: %s\n", wifi_fail_reason());
	}
}

static void net_connect(void)
{
	char ip[16];

	if (!wifi_ssid[0]) {
		puts("No network chosen\n");
		return;
	}
	printf("Connecting to %s... ", wifi_ssid);
	wifi_connect(wifi_ssid, wifi_password);
	wifi_wait(35000);
	if (wifi_get_state() == WIFI_UP) {
		wifi_get_ip(ip, sizeof(ip));
		printf("connected, %s\n", ip);
		/* NTP normally answers well inside this */
		for (int i = 0; i < 30 && !wifi_clock_synced(); i++)
			sleep_ms(100);
		if (wifi_clock_synced())
			puts("Clock set from NTP");
	} else
		printf("failed: %s\n", wifi_fail_reason());
	putchar('\n');
}

static void net_scan(void)
{
	const wifi_scan_result_t *r;
	int i, n;

	net_start();
	if (wifi_get_state() == WIFI_OFF)
		return;
	if (wifi_get_state() == WIFI_JOINING) {
		puts("Busy connecting, try again in a moment\n");
		return;
	}
	printf("Scanning... ");
	wifi_scan();
	wifi_wait(15000);
	n = wifi_scan_count();
	printf("%d found\n", n);
	for (i = 0; i < n; i++) {
		r = wifi_scan_result(i);
		printf("%2d %-24.24s %4d dBm ch %2d %s\n", i + 1, r->ssid,
		       r->rssi, r->channel,
		       !r->auth ? "open" : (r->auth & 4) ? "WPA2" :
		       (r->auth & 2) ? "WPA" : "WEP");
	}
	putchar('\n');
}

/*
 * network configuration
 */
static void net_config(void)
{
	bool quit = false;
	char s[HOST_NAME_MAX+1];
	const wifi_scan_result_t *r;
	int i;

	while (!quit) {
		net_status(s, sizeof(s));
		printf("Network: %s\n\n", s);
		printf("s - scan for networks\n");
		if (wifi_scan_count())
			printf("1-%d - choose a network from the scan\n",
			       wifi_scan_count());
		printf("e - SSID: %s\n", wifi_ssid);
		printf("p - password: %s\n",
		       wifi_password[0] ? "********" : "(none)");
		printf("c - connect\n");
		printf("d - disconnect\n");
		printf("a - connect at boot: %s\n",
		       wifi_autoconnect ? "yes" : "no");
		printf("y - country: %s\n",
		       wifi_country[0] ? wifi_country : "worldwide");
		printf("n - NTP server: %s\n", ntp_server);
		printf("u - UTC offset in hours: %d\n", utc_offset);
		printf("m - modem init: %s\n", modem_init_str);
		printf("q - quit\n");

		printf("\nCommand: ");
		get_cmdline(s, 3);
		putchar('\n');

		if (isdigit((unsigned char) s[0])) {
			i = atoi(s);
			if ((r = wifi_scan_result(i - 1)) == NULL)
				continue;
			if (strcmp(wifi_ssid, r->ssid)) {
				strcpy(wifi_ssid, r->ssid);
				wifi_password[0] = '\0';
			}
			if (r->auth) {
				printf("Password%s: ", wifi_password[0] ?
				       " (Enter keeps the current one)" : "");
				get_cmdline(s, WIFI_PWD_LEN+1);
				putchar('\n');
				if (s[0])
					strcpy(wifi_password, s);
			} else
				wifi_password[0] = '\0';
			net_connect();
			continue;
		}

		switch (tolower((unsigned char) s[0])) {
		case 's':
			net_scan();
			break;

		case 'e':
			printf("Enter SSID: ");
			get_cmdline(s, WIFI_SSID_LEN+1);
			strcpy(wifi_ssid, s);
			putchar('\n');
			break;

		case 'p':
			printf("Enter password: ");
			get_cmdline(s, WIFI_PWD_LEN+1);
			strcpy(wifi_password, s);
			putchar('\n');
			break;

		case 'c':
			net_connect();
			break;

		case 'd':
			if (wifi_get_state() != WIFI_OFF) {
				wifi_disconnect();
				wifi_wait(2000);
			}
			break;

		case 'a':
			wifi_autoconnect = !wifi_autoconnect;
			break;

		case 'y':
			printf("Enter country code (Enter for worldwide): ");
			get_cmdline(s, 3);
			putchar('\n');
			wifi_country[0] = toupper((unsigned char) s[0]);
			wifi_country[1] = toupper((unsigned char) s[1]);
			wifi_country[2] = '\0';
			if (wifi_country[0] && !wifi_country[1])
				wifi_country[0] = '\0';
			if (wifi_get_state() != WIFI_OFF)
				puts("The radio is already running, the country "
				     "applies from the next boot\n");
			break;

		case 'n':
			printf("Enter NTP server: ");
			get_cmdline(s, HOST_NAME_MAX+1);
			if (strlen(s))
				strcpy(ntp_server, s);
			else
				strcpy(ntp_server, DEFAULT_NTP);
			putchar('\n');
			break;

		case 'u':
			/* get_int() spends -1 on "no input", a real offset */
			printf("Enter UTC offset in hours (-12 - 14): ");
			get_cmdline(s, 4);
			putchar('\n');
			if (s[0]) {
				i = atoi(s);
				if (i >= -12 && i <= 14)
					utc_offset = i;
				else
					puts("Invalid UTC offset");
			}
			break;

		case 'm':
			printf("Enter modem init string: ");
			get_cmdline(s, MODEM_INIT_LEN+1);
			strcpy(modem_init_str, s);
			putchar('\n');
			break;

		case 'q':
			quit = true;
			break;
		}
	}
}

/*
 * Bring WiFi up in the background at boot, so the banner and the config
 * dialog do not wait for it.
 */
void net_autoconnect(void)
{
	if (wifi_autoconnect && wifi_ssid[0]) {
		wifi_start(wifi_country);
		wifi_connect(wifi_ssid, wifi_password);
	}
}
#endif

/*
 * Load the configured font, falling back to the built-in one. Changing it
 * changes the number of rows and columns, so the screen is cleared after.
 */
void apply_font(void)
{
	/*
	 * Characters already handed to core 0 may still be drawing, and
	 * lcd_load_font() frees the glyph data out from under them. This is
	 * a request/reply round trip through the same FIFO, so it does not
	 * return until core 0 has drained everything queued ahead of it.
	 */
	lcd_buffer_enable(LCD_BUFFERMODE_DIRECT);

	if (font_file[0] && (lcd_load_font(font_file) != FR_OK)) {
		font_file[0] = '\0';
		lcd_load_font(NULL);
		if (status_enabled && (font.term_height > STATUS_ROWS + 1))
			font.term_height -= STATUS_ROWS;
		scrollback_clear();
		term_clear();
		printf("font not found, using the built-in one\n\n");
		return;
	}
	if (!font_file[0])
		lcd_load_font(NULL);

	/* the status block lives in the rows the terminal is denied */
	if (status_enabled && (font.term_height > STATUS_ROWS + 1))
		font.term_height -= STATUS_ROWS;

	/* the terminal is a different width now, so the history is stale */
	scrollback_clear();
	term_clear();
	status_paint();
}

/*
 * Configuration dialog for the machine
 */
void config(void)
{
	const char *cpath = CODE_DIR;
	const char *cext = "*.BIN";
	const char *dpath = DISKS_DIR;
	const char *dext = "*.DSK";
	const char *hext = "*.HDD";
	char s[FNLEN+1];
	int go_flag = 0;
	int i, n, menu;
	static const char *dotw[7] = { "Sun", "Mon", "Tue", "Wed",
				       "Thu", "Fri", "Sat" };
#ifdef PICOCALC_WIFI
	char s_net[HOST_NAME_MAX+1];
#endif

#ifdef PICOCALC_WIFI
	/* NTP may already have set the clock while the banner was printing */
	if (!wifi_clock_synced())
#endif
	rtc_set_datetime(&t);
	sleep_us(64);

	menu = 1;

	while (!go_flag) {
		if (menu) {
			rtc_get_datetime(&t);
			printf("Current time: %s %04d-%02d-%02d "
			       "%02d:%02d:%02d\n", dotw[t.dotw],
			       t.year, t.month, t.day, t.hour, t.min, t.sec);
#ifdef PICOCALC_WIFI
			net_status(s_net, sizeof(s_net));
			printf("n - network: %s\n", s_net);
#endif
			printf("v - font: %s (%dx%d)\n",
			       font_file[0] ? font_file : "built-in",
			       term_get_width(), term_get_height());
			printf("k - arrow keys: %s\n",
			       (keymap_arrows == KEYMAP_ARROWS_WORDSTAR) ?
			       "WordStar" : "ANSI");
			printf("a - set date\n");
			printf("t - set time\n");
			printf("c - switch CPU, currently %s\n",
			       (cpu == Z80) ? "Z80" : "8080");
			printf("s - CPU speed: ");
			if (speed == 0)
				puts("unlimited");
			else
				printf("%d MHz\n", speed);
			printf("p - Port 255 value: %02XH\n", fp_value);
			printf("f - list files\n");
			printf("r - load file\n");
			printf("d - list disks\n");
			printf("0 - Disk 0: %s\n", disks[0]);
			printf("1 - Disk 1: %s\n", disks[1]);
			printf("2 - Disk 2: %s\n", disks[2]);
			printf("3 - Disk 3: %s\n", disks[3]);
			printf("i - Hard disk I: %s\n", disks[HD_SLOT]);
			printf("g - run machine\n\n");
		} else
			menu = 1;
		printf("Command: ");
		get_cmdline(s, 2);
		putchar('\n');

		switch (tolower((unsigned char) s[0])) {
#ifdef PICOCALC_WIFI
		case 'n':
			net_config();
			break;
#endif

		case 'a':
			n = 0;
			rtc_get_datetime(&t);
			if ((i = get_int("weekday", " (0=Sun)", 0, 6)) >= 0) {
				t.dotw = i;
				n++;
			}
			if ((i = get_int("year", "", 0, 4095)) >= 0) {
				t.year = i;
				n++;
			}
			if ((i = get_int("month", "", 1, 12)) >= 0) {
				t.month = i;
				n++;
			}
			if ((i = get_int("day", "", 1, 31)) >= 0) {
				t.day = i;
				n++;
			}
			if (n > 0) {
				rtc_set_datetime(&t);
				sleep_us(64);
			}
			putchar('\n');
			break;

		case 't':
			n = 0;
			rtc_get_datetime(&t);
			if ((i = get_int("hour", "", 0, 23)) >= 0) {
				t.hour = i;
				n++;
			}
			if ((i = get_int("minute", "", 0, 59)) >= 0) {
				t.min = i;
				n++;
			}
			if ((i = get_int("second", "", 0, 59)) >= 0) {
				t.sec = i;
				n++;
			}
			if (n > 0) {
				rtc_set_datetime(&t);
				sleep_us(64);
			}
			putchar('\n');
			break;

		case 'v':
			prompt_fn(s);
			if (s[0]) {
				strcpy(font_file, Z80_DIR);
				strcat(font_file, s);
				strcat(font_file, ".FNT");
			} else
				font_file[0] = '\0';
			apply_font();
			putchar('\n');
			break;

		case 'k':
			keymap_arrows =
				(keymap_arrows == KEYMAP_ARROWS_WORDSTAR) ?
				KEYMAP_ARROWS_ANSI : KEYMAP_ARROWS_WORDSTAR;
			break;

		case 'c':
			if (cpu == Z80)
				switch_cpu(I8080);
			else
				switch_cpu(Z80);
			break;

		case 's':
			i = get_int("speed", " in MHz (0=unlimited)", 0, 40);
			putchar('\n');
			if (i >= 0)
				speed = i;
			break;

		case 'p':
again:
			printf("Enter value in Hex: ");
			get_cmdline(s, 3);
			if (s[0]) {
				if (!isxdigit((unsigned char) s[0]) ||
				    !isxdigit((unsigned char) s[1])) {
					puts("Invalid value: range 00 - FF");
					goto again;
				}
				fp_value = (s[0] <= '9' ? s[0] - '0' :
					    toupper((unsigned char) s[0]) -
					    'A' + 10) << 4;
				fp_value += (s[1] <= '9' ? s[1] - '0' :
					     toupper((unsigned char) s[1]) -
					     'A' + 10);
			}
			putchar('\n');
			break;

		case 'f':
			list_files(cpath, cext);
			putchar('\n');
			menu = 0;
			break;

		case 'r':
			prompt_fn(s);
			if (s[0])
				load_file(s);
			putchar('\n');
			break;

		case 'd':
			list_files(dpath, dext);
			list_files(dpath, hext);
			putchar('\n');
			menu = 0;
			break;

		case 'i':
		case '0':
		case '1':
		case '2':
		case '3':
			i = (s[0] == 'i') ? HD_SLOT : s[0] - '0';
			prompt_fn(s);
			if (s[0])
				mount_disk(i, s);
			else {
				disks[i][0] = '\0';
				putchar('\n');
			}
			break;

		case 'g':
			go_flag = 1;
			break;

		default:
			break;
		}
	}

}
