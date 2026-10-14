/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Status block along the bottom of the PicoCalc screen. See status.h.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pico/stdlib.h"

#include "lcd.h"
#include "term.h"
#include "keyboard.h"

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"

#include "disks.h"
#include "dazzler.h"
#include "picosim.h"
#include "scrollback.h"
#include "status.h"
#ifdef PICOCALC_WIFI
#include "wifi.h"
#include "modem.h"
#include "netsock.h"
#endif

#define REPAINT_MS	250
#define BATTERY_MS	5000

/* P1 phosphor, the green of the terminals this machine is pretending to be */
#define STATUS_FG	RGB( 51, 255,  51)
#define STATUS_DIM	RGB( 32, 176,  32)
#define STATUS_BG	RGB(  0,   0,   0)

/* the widest line the narrowest font can show */
#define LINE_MAX	(LCD_WIDTH / 4)

bool status_enabled = true;

static repeating_timer_t repaint_timer;
static int battery = -1;
static bool charging;
static absolute_time_t battery_due;
static int painted_top = -1;

static void fill_row(int row, u16 color)
{
	int y = (row * font.glyph_height) % lcd_current_height;

	lcd_fill(color, 0, y, LCD_WIDTH, font.glyph_height);
	if (y > lcd_current_height - font.glyph_height)
		lcd_fill(color, 0, y - lcd_current_height, LCD_WIDTH,
			 font.glyph_height);
}

static void paint_row(int row, const char *text, int len, u16 fg)
{
	int y = (row * font.glyph_height) % lcd_current_height;

	fill_row(row, STATUS_BG);
	lcd_draw_text(0, y, fg, STATUS_BG, text, len, LCD_ALIGN_LEFT);
	if (y > lcd_current_height - font.glyph_height)
		lcd_draw_text(0, y - lcd_current_height, fg, STATUS_BG,
			      text, len, LCD_ALIGN_LEFT);
}

static const char *disk_name(int drive)
{
	const char *s = disks[drive];
	const char *base;

	if (!s[0])
		return NULL;
	base = strrchr(s, '/');
	return base ? base + 1 : s;
}

/*
 * One drive field, sized to whatever the font leaves. A wide screen gets the
 * whole 8.3 name, a narrow one drops the extension rather than truncating
 * into the middle of a name - they are all .DSK or .HDD anyway.
 */
static int drive_field(char *out, int size, char letter, int slot, int namew)
{
	const char *name = disk_name(slot);
	char trimmed[16];
	const char *dot;

	if (name == NULL)
		return snprintf(out, size, "%c:%-*s", letter, namew,
				(namew >= 7) ? "[EMPTY]" : "-");

	if (namew < 12) {
		dot = strchr(name, '.');
		if (dot != NULL) {
			int n = dot - name;

			if (n > (int) sizeof(trimmed) - 1)
				n = sizeof(trimmed) - 1;
			memcpy(trimmed, name, n);
			trimmed[n] = '\0';
			name = trimmed;
		}
	}
	return snprintf(out, size, "%c:%-*.*s", letter, namew, namew, name);
}

static void compose_machine(char *line, int width)
{
	char left[48], right[32];
	int nl, nr, gap, i, act;
#ifdef PICOCALC_WIFI
	int ncpu;
#endif
	time_t now;
	struct tm *tm;

	act = disk_act;
	disk_act = -1;

	nl = snprintf(left, sizeof(left), "%s ", (cpu == Z80) ? "Z80" : "8080");
	if (speed)
		nl += snprintf(left + nl, sizeof(left) - nl, "%dMHz", speed);
	else
		nl += snprintf(left + nl, sizeof(left) - nl, "max");
#ifdef PICOCALC_WIFI
	ncpu = nl;
#endif

	if (scrollback_active())
		nl += snprintf(left + nl, sizeof(left) - nl, "  BACK %d",
			       scrollback_offset());
	else if (act >= 0) {
		int slot = act & 0x7f;
		char drive = (slot == HD_SLOT) ? 'I' : 'A' + slot;

		nl += snprintf(left + nl, sizeof(left) - nl, "  %c:%c",
			       drive, (act & 0x80) ? 'W' : 'R');
	}

	time(&now);
	tm = localtime(&now);
	nr = snprintf(right, sizeof(right), "%02d:%02d:%02d",
		      tm->tm_hour, tm->tm_min, tm->tm_sec);
	if (battery >= 0)
		nr += snprintf(right + nr, sizeof(right) - nr, "  %3d%%%c",
			       battery, charging ? '+' : ' ');

#ifdef PICOCALC_WIFI
	/* the network field is the first thing to go on a narrow screen */
	{
		char net[32], ip[16], port[8] = "";
		int nn = 0;

		switch (wifi_get_state()) {
		case WIFI_UP:
			/* the port is where callers reach the modem, and the
			   asterisk is its carrier detect */
			if (ns_listen_port())
				snprintf(port, sizeof(port), ":%u",
					 ns_listen_port());
			if (wifi_get_ip(ip, sizeof(ip)))
				nn = snprintf(net, sizeof(net), "  %s%s%s", ip,
					      port, modem_carrier() ? "*" : "");
			break;
		case WIFI_JOINING:
			nn = snprintf(net, sizeof(net), "  WiFi...");
			break;
		case WIFI_FAILED:
			nn = snprintf(net, sizeof(net), "  WiFi fail");
			break;
		default:
			break;
		}
		if (nn && nl + nn + 2 + nr <= width &&
		    nl + nn < (int) sizeof(left)) {
			memmove(left + ncpu + nn, left + ncpu, nl - ncpu);
			memcpy(left + ncpu, net, nn);
			nl += nn;
		}
	}
#endif

	if (nl > width)
		nl = width;
	if (nr > width - nl)
		nr = width - nl;
	gap = width - nl - nr;

	memcpy(line, left, nl);
	for (i = 0; i < gap; i++)
		line[nl + i] = ' ';
	memcpy(line + nl + gap, right, nr);
}

static void compose_drives(char *line, int width)
{
	static const char letters[] = { 'A', 'B', 'C', 'D', 'I' };
	char field[24];
	int namew, i, n = 0, len;

	/* five fields of "X:" plus a name, with a space between each */
	namew = (width - NUMDRV * 2 - (NUMDRV - 1)) / NUMDRV;
	if (namew > 12)
		namew = 12;
	if (namew < 1)
		namew = 1;

	memset(line, ' ', width);
	for (i = 0; i < NUMDRV; i++) {
		len = drive_field(field, sizeof(field), letters[i],
				  (i == 4) ? HD_SLOT : i, namew);
		if (len > (int) sizeof(field) - 1)
			len = sizeof(field) - 1;
		if (n + len > width)
			break;
		memcpy(line + n, field, len);
		n += len + 1;
	}
}

void status_paint(void)
{
	char line[LINE_MAX + 1];
	int width = term_get_width();
	int top = term_get_scroll() + term_get_height();
	int i;

	if (!status_enabled || dazzler_state)
		return;
	if (width > LINE_MAX)
		width = LINE_MAX;

	/*
	 * The row the block vacates when the screen scrolls becomes the top
	 * text row. Normally that is the blank separator and needs nothing,
	 * but a scroll of more than one row leaves status text behind.
	 */
	if ((painted_top >= 0) && (painted_top < top))
		for (i = painted_top; i < top && i < painted_top + STATUS_ROWS;
		     i++)
			fill_row(i, term_get_bg());
	painted_top = top;

	fill_row(top, term_get_bg());		/* separator */

	compose_machine(line, width);
	paint_row(top + 1, line, width, STATUS_FG);

	compose_drives(line, width);
	paint_row(top + 2, line, width, STATUS_DIM);
}

static bool on_repaint(repeating_timer_t *rt)
{
	(void) rt;
	status_paint();
	return true;
}

void status_poll(void)
{
	if (!status_enabled)
		return;
	if (absolute_time_diff_us(get_absolute_time(), battery_due) > 0)
		return;

	/* I2C, so this belongs in the idle loop and not in the repaint timer */
	battery = get_battery(&charging);
	battery_due = make_timeout_time_ms(BATTERY_MS);
}

void status_init(void)
{
	if (!status_enabled)
		return;

	battery_due = get_absolute_time();
	term_set_scroll_callback(status_paint);
	add_repeating_timer_ms(REPAINT_MS, on_repaint, NULL, &repaint_timer);
}
