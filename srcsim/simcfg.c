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

#include <stdarg.h>
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
#include "drivemenu.h"
#include "loadmenu.h"
#include "picosim.h"

#include "net_vars.h"
#ifdef PICOCALC_WIFI
#include "wifi.h"
#include "update.h"
#include "wifimenu.h"
#include "version.h"
#endif

static datetime_t t = { .year = 2024, .month = 1, .day = 1, .dotw = 1,
			.hour = 0, .min = 0, .sec = 0 };
/* kept in the config file so phase 2 can wire it to the keyboard
   controller backlight register without changing the format */
static int brightness = 1000;
static const char *cfg = Z80_DIR CONF_FILE;

char font_file[DISKLEN+1];

#define SGR_KEY		"\033[96m"
#define SGR_LABEL	"\033[93m"
#define SGR_OFF		"\033[0m"

static void menu_item(const char *key, const char *label, const char *fmt, ...)
{
	va_list ap;

	printf(SGR_KEY "%s" SGR_OFF " - " SGR_LABEL "%s" SGR_OFF, key, label);
	if (fmt != NULL) {
		putchar(' ');
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}
	putchar('\n');
}

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
#define CFG_VERSION	4

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
		/* the fonts moved from /Z80 into /Z80/fonts, and an older
		   config still names them where they were */
		if (!strncmp(font_file, Z80_DIR, sizeof(Z80_DIR) - 1)
		    && !strchr(font_file + sizeof(Z80_DIR) - 1, '/')) {
			char name[DISKLEN+1];

			strcpy(name, font_file + sizeof(Z80_DIR) - 1);
			strcpy(font_file, FONTS_DIR);
			strcat(font_file, name);
		}
		if (version >= 2) {
			f_read(&sd_file, &wifi_country, sizeof(wifi_country),
			       &br);
			f_read(&sd_file, &wifi_autoconnect,
			       sizeof(wifi_autoconnect), &br);
			f_read(&sd_file, &modem_init_str, sizeof(modem_init_str), &br);
		}
		if (version >= 3) {
			f_read(&sd_file, &update_url, sizeof(update_url), &br);
			update_url[UPDATE_URL_LEN] = '\0';
		}
		if (version >= 4)
			f_read(&sd_file, &disk_cache, sizeof(disk_cache), &br);
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
		f_write(&sd_file, &update_url, sizeof(update_url), &br);
		f_write(&sd_file, &disk_cache, sizeof(disk_cache), &br);
		f_close(&sd_file);
	}
}

#ifdef PICOCALC_WIFI
/*
 * Software update: a check against the release site, and from there the
 * download. See update.h.
 */
static void update_check(void)
{
	char tag[TAG_LEN+1], card[32], s[2];
	const char *err;
	bool have_card;

	if (!update_url[0]) {
		puts("No update URL set, use U to enter one\n");
		return;
	}
	if (wifi_get_state() != WIFI_UP) {
		puts("No Network - connect to Wifi to check for or download "
		     "updates\n");
		return;
	}
	have_card = update_card_version(card, sizeof(card));
	printf("Installed firmware: %s\n", USR_REL);
	printf("SD card files: %s\n", have_card ? card : "unknown");
	printf("Checking %s... ", update_url);
	update_begin();
	if (!update_latest(tag, sizeof(tag), &err)) {
		printf("failed: %s\n\n", err);
		return;
	}
	printf("latest release is %s\n", tag);
#ifdef PICOCALC_STACK_WATERMARK
	printf("core 0 stack headroom: %u bytes\n", stack_headroom());
#endif
	putchar('\n');
	if (version_cmp(tag, USR_REL) <= 0 &&
	    (!have_card || version_cmp(tag, card) <= 0)) {
		puts("Already up to date\n");
		return;
	}
	printf("Version %s is available\n\n", tag);
	for (;;) {
		menu_item("U", "Update to version:", "%s", tag);
		menu_item("X", "Exit: no action taken", NULL);
		printf("\n" SGR_KEY "Command:" SGR_OFF " ");
		get_cmdline(s, 2);
		putchar('\n');
		switch (tolower((unsigned char) s[0])) {
		case 'u':
			/* the network can drop between the check and the update */
			if (wifi_get_state() != WIFI_UP) {
				puts("No Network - connect to Wifi to check for "
				     "or download updates\n");
				return;
			}
			printf("This overwrites files in the Z80 directory of "
			       "the SD card.\nDisk images you have changed are "
			       "backed up first.\nContinue (y/N)? ");
			get_cmdline(s, 2);
			putchar('\n');
			if (tolower((unsigned char) s[0]) == 'y')
				update_run(tag);
			return;
		case 'x':
			return;
		}
	}
}

static void update_config(void)
{
	bool quit = false;
	char s[UPDATE_URL_LEN+1];
	size_t n;

	while (!quit) {
		menu_item("U", "Set update URL:", "%s",
			  update_url[0] ? update_url : "(not set)");
		menu_item("C", "Check for update", NULL);
		menu_item("X", "Exit: no action taken", NULL);

		printf("\n" SGR_KEY "Command:" SGR_OFF " ");
		get_cmdline(s, 2);
		putchar('\n');

		switch (tolower((unsigned char) s[0])) {
		case 'u':
			printf("Enter update URL: ");
			get_cmdline(s, UPDATE_URL_LEN+1);
			putchar('\n');
			n = strlen(s);
			while (n && isspace((unsigned char) s[n - 1]))
				s[--n] = '\0';
			if (n)
				strcpy(update_url, s);
			break;

		case 'c':
			update_check();
			break;

		case 'x':
			quit = true;
			break;
		}
	}
	/* the check borrows the machine's memory */
	update_end();
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
 * The status block takes the rows the terminal is denied, below the panel's
 * scroll area so that it holds still while the text scrolls.
 */
static void fit_terminal(void)
{
	if (status_enabled && (font.term_height > STATUS_ROWS + 1)) {
		font.term_height -= STATUS_ROWS;
		lcd_scroll_area(font.term_height * font.glyph_height);
	} else
		lcd_scroll_area(0);

	/* the terminal is a different width now, so the history is stale */
	scrollback_clear();
	lcd_clear();
	term_reset();
	status_paint();
}

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
		fit_terminal();
		printf("font not found, using the built-in one\n\n");
		return;
	}
	if (!font_file[0])
		lcd_load_font(NULL);
	fit_terminal();
}

/* the drives on one line: A:CPM22 B:TE C:- D:COMMS I:- */
static const char *disk_summary(char *buf, size_t len)
{
	const char *base, *dot;
	size_t n = 0;
	int i;

	for (i = 0; i < NUMDRV && n < len; i++) {
		base = strrchr(disks[i], '/');
		base = base ? base + 1 : disks[i];
		if (!base[0])
			base = "-";
		dot = strchr(base, '.');
		n += snprintf(buf + n, len - n, "%s%c:%.*s", i ? " " : "",
			      drive_letter(i),
			      dot ? (int) (dot - base) : (int) strlen(base), base);
	}
	return buf;
}

/*
 * Configuration dialog for the machine
 */
void config(void)
{
	char s[FNLEN+1], summary[64];
	int go_flag = 0;
	int i, n;
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

	while (!go_flag) {
		rtc_get_datetime(&t);
		printf(SGR_LABEL "Current time:" SGR_OFF " %s %04d-%02d-%02d "
		       "%02d:%02d:%02d\n", dotw[t.dotw],
		       t.year, t.month, t.day, t.hour, t.min, t.sec);
#ifdef PICOCALC_WIFI
		wifi_status_text(s_net, sizeof(s_net));
		menu_item("N", "Network setup:", "%s", s_net);
		menu_item("U", "Update PicoCalc-Z80", NULL);
#endif
		menu_item("V", "Font:", "%s (%dx%d)",
			  font_file[0] ? font_file : "built-in",
			  term_get_width(), term_get_height());
		menu_item("K", "Arrow keys:", "%s",
			  (keymap_arrows == KEYMAP_ARROWS_WORDSTAR) ?
			  "WordStar" : "ANSI");
		menu_item("A", "Set date/time", NULL);
		menu_item("C", "Switch CPU, currently", "%s",
			  (cpu == Z80) ? "Z80" : "8080");
		if (speed == 0)
			menu_item("S", "CPU speed:", "unlimited");
		else
			menu_item("S", "CPU speed:", "%d MHz", speed);
		menu_item("P", "Port 255 value:", "%02XH", fp_value);
		menu_item("L", "Load file", NULL);
		menu_item("D", "Disk setup:", "%s", disk_summary(summary, sizeof(summary)));
		if (disk_cache_possible())
			menu_item("M", "Disk cache in PSRAM:", "%s",
				  disk_cache ? "on" : "off");
		menu_item("R", "Run machine", NULL);
		putchar('\n');
		printf(SGR_KEY "Command:" SGR_OFF " ");
		get_cmdline(s, 2);
		putchar('\n');

		switch (tolower((unsigned char) s[0])) {
#ifdef PICOCALC_WIFI
		case 'n':
			wifi_menu();
			break;

		case 'u':
			update_config();
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
				strcpy(font_file, FONTS_DIR);
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

		case 'l':
			if (load_menu(s, sizeof(s), &n)) {
				/* run from the program rather than the boot ROM,
				   which would read the boot sector of A: over it */
				PC = 0;
				printf("%s.BIN loaded at 0, %d bytes; R runs it\n\n",
				       s, n);
			}
			break;

		case 'd':
			drive_menu();
			break;

		case 'm':
			if (disk_cache_possible())
				disk_cache_set(!disk_cache);
			break;

		case 'r':
			go_flag = 1;
			break;

		default:
			break;
		}
	}

}
