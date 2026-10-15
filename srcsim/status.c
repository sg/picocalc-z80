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

/* everything below is touched on core 0 only, from interrupts of one priority */
static char shown[2][LINE_MAX + 1];
static int shown_width;
static int painted_y = -1;
static volatile bool repaint_due;

enum { SEPARATOR, MACHINE, DRIVES, STRIP, PIECES };

static void fill_at(int y, int h, u16 color)
{
	y %= lcd_current_height;
	lcd_fill(color, 0, y, LCD_WIDTH, h);
	if (y + h > lcd_current_height)
		lcd_fill(color, 0, y - lcd_current_height, LCD_WIDTH, h);
}

/* opaque text over the old line, so a repaint never shows a blank row */
static void text_at(int y, const char *text, int len, u16 fg)
{
	int used = len * font.glyph_width;
	int h = font.glyph_height;

	y %= lcd_current_height;
	lcd_draw_text(0, y, fg, STATUS_BG, text, len, LCD_ALIGN_LEFT);
	if (used < LCD_WIDTH)
		lcd_fill(STATUS_BG, used, y, LCD_WIDTH - used, h);
	if (y + h > lcd_current_height) {
		y -= lcd_current_height;
		lcd_draw_text(0, y, fg, STATUS_BG, text, len, LCD_ALIGN_LEFT);
		if (used < LCD_WIDTH)
			lcd_fill(STATUS_BG, used, y, LCD_WIDTH - used, h);
	}
}

static int block_y(int scroll)
{
	return (scroll + term_get_height() * font.glyph_height) %
	       lcd_current_height;
}

/* a font whose height does not divide 320 leaves a sliver under the block */
static int piece_height(int piece)
{
	int h;

	if (piece != STRIP)
		return font.glyph_height;
	h = LCD_HEIGHT - (term_get_height() + STATUS_ROWS) * font.glyph_height;
	return (h > 0) ? h : 0;
}

static void paint_piece(int top, int piece)
{
	int y = top + piece * font.glyph_height;

	switch (piece) {
	case MACHINE:
		text_at(y, shown[0], shown_width, STATUS_FG);
		break;
	case DRIVES:
		text_at(y, shown[1], shown_width, STATUS_DIM);
		break;
	default:
		if (piece_height(piece))
			fill_at(y, piece_height(piece), term_get_bg());
		break;
	}
}

static bool offscreen(int scroll, int y, int h)
{
	int d = ((y - scroll) % lcd_current_height + lcd_current_height) %
		lcd_current_height;

	return (d >= LCD_HEIGHT) && (d + h <= lcd_current_height);
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

static int line_width(void)
{
	int width = term_get_width();

	return (width > LINE_MAX) ? LINE_MAX : width;
}

static bool hidden(void)
{
	return !status_enabled || dazzler_state;
}

static void compose(void)
{
	shown_width = line_width();
	compose_machine(shown[0], shown_width);
	compose_drives(shown[1], shown_width);
}

static void refresh(bool force)
{
	char machine[LINE_MAX + 1], drives[LINE_MAX + 1];
	int width = line_width();
	int top = block_y(lcd_get_scroll());
	int i;

	if (hidden())
		return;

	compose_machine(machine, width);
	compose_drives(drives, width);
	if (force || (top != painted_y) || (width != shown_width)) {
		memcpy(shown[0], machine, width);
		memcpy(shown[1], drives, width);
		shown_width = width;
		for (i = 0; i < PIECES; i++)
			paint_piece(top, i);
		painted_y = top;
		return;
	}
	if (memcmp(shown[0], machine, width)) {
		memcpy(shown[0], machine, width);
		paint_piece(top, MACHINE);
	}
	if (memcmp(shown[1], drives, width)) {
		memcpy(shown[1], drives, width);
		paint_piece(top, DRIVES);
	}
}

/*
 * The block moves through display memory with every scroll. What will be
 * offscreen until the scroll lands is drawn before it, and the rest straight
 * after, so the block is wrong on screen for two rows' worth of drawing.
 */
static void on_scroll(int from, int to, bool done)
{
	static const int order[] = { MACHINE, SEPARATOR, DRIVES, STRIP };
	static bool early[PIECES];
	int top = block_y(to);
	int text_h, d, i;

	if (hidden())
		return;
	if (shown_width != line_width())
		compose();

	if (!done) {
		for (i = 0; i < PIECES; i++) {
			early[i] = offscreen(from, top + i * font.glyph_height,
					     piece_height(i));
			if (early[i])
				paint_piece(top, i);
		}
		return;
	}

	/* the old block, and any stale ring rows past it, are text area now */
	if (painted_y >= 0) {
		text_h = term_get_height() * font.glyph_height;
		d = ((painted_y - to) % lcd_current_height +
		     lcd_current_height) % lcd_current_height;
		if (d < text_h)
			fill_at(painted_y, text_h - d, term_get_bg());
	}
	for (i = 0; i < PIECES; i++)
		if (!early[order[i]])
			paint_piece(top, order[i]);
	painted_y = top;
}

static bool on_repaint(repeating_timer_t *rt)
{
	(void) rt;
	refresh(repaint_due);
	repaint_due = false;
	return true;
}

void status_paint(void)
{
	uint32_t save;

	if (get_core_num() != 0) {
		repaint_due = true;
		return;
	}
	/* main context on core 0, which the drawing interrupts must not split */
	save = save_and_disable_interrupts();
	refresh(true);
	restore_interrupts(save);
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
	term_reserve_bottom(true);
	lcd_set_scroll_hook(on_scroll);
	add_repeating_timer_ms(REPAINT_MS, on_repaint, NULL, &repaint_timer);
}
