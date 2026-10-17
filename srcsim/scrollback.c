/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Scrollback for the PicoCalc console. See scrollback.h.
 * Copyright (C) 2026 Steve Gibson
 *
 */

#include <string.h>

#include "pico/stdlib.h"

#include "lcd.h"
#include "term.h"
#include "keyboard.h"

#include "dazzler.h"
#include "scrollback.h"

static char sb[SB_LINES][SB_COLS];
static char blank[SB_COLS];
static int sb_oldest;		/* oldest absolute row still in the shadow */
static int offset;		/* rows back from live, 0 while live */
static bool enabled;

static char *sb_row(int row)
{
	return sb[((row % SB_LINES) + SB_LINES) % SB_LINES];
}

/*
 * Rows arrive in order, so keeping the oldest one that has not yet been
 * overwritten is enough to know how far back the view may go.
 */
static void note_row(int row)
{
	if (row - sb_oldest >= SB_LINES)
		sb_oldest = row - SB_LINES + 1;
}

static void observe(int what, int x, int y, char c)
{
	switch (what) {
	case TERM_OBS_CHAR:
		if ((x >= 0) && (x < SB_COLS)) {
			note_row(y);
			sb_row(y)[x] = c;
		}
		break;

	case TERM_OBS_ERASE_ROW:
		note_row(y);
		memset(sb_row(y), ' ', SB_COLS);
		break;

	case TERM_OBS_ERASE_EOL:
		if ((x >= 0) && (x < SB_COLS)) {
			note_row(y);
			memset(sb_row(y) + x, ' ', SB_COLS - x);
		}
		break;

	case TERM_OBS_CLEAR:
		/* term.c restarts its row numbering, so the history goes too */
		scrollback_clear();
		break;

	default:
		break;
	}
}

void scrollback_clear(void)
{
	memset(blank, ' ', sizeof(blank));
	memset(sb, ' ', sizeof(sb));
	sb_oldest = 0;
	offset = 0;
}

/*
 * Paint the text area from the shadow. Visible line k sits at the same place
 * in display memory whether it is showing history or live text, so this is
 * also how the screen is put back when the view returns to the bottom.
 */
static void repaint(void)
{
	int scroll = term_get_scroll();
	int rows = term_get_height();
	int cols = term_get_width();
	u16 fg = term_get_fg(), bg = term_get_bg();
	int k, row, y;

	if (cols > SB_COLS)
		cols = SB_COLS;

	for (k = 0; k < rows; k++) {
		row = scroll + k - offset;
		y = ((scroll + k) * font.glyph_height) % lcd_scroll_height;

		const char *src = (row >= sb_oldest) ? sb_row(row) : blank;
		int used = cols * font.glyph_width;

		/* drawing the text paints its own background, so only the
		   sliver the glyphs do not reach has to be filled */
		lcd_draw_text(0, y, fg, bg, src, cols, LCD_ALIGN_LEFT);
		if (used < LCD_WIDTH)
			lcd_fill(bg, used, y, LCD_WIDTH - used,
				 font.glyph_height);

		/* a font whose height does not divide the ring straddles it */
		if (y > lcd_scroll_height - font.glyph_height) {
			y -= lcd_scroll_height;
			lcd_draw_text(0, y, fg, bg, src, cols, LCD_ALIGN_LEFT);
			if (used < LCD_WIDTH)
				lcd_fill(bg, used, y, LCD_WIDTH - used,
					 font.glyph_height);
		}
	}
}

static int max_offset(void)
{
	int back = term_get_scroll() - sb_oldest;

	return (back > 0) ? back : 0;
}

/*
 * The keyboard reports the modifier keys as events in their own right, and
 * repeats them while held, so they must not count as "some other key" or
 * holding Ctrl would keep dropping the view back to the bottom.
 */
static bool is_modifier(int code)
{
	switch (code) {
	case KEY_CONTROL:
	case KEY_ALT:
	case KEY_LSHIFT:
	case KEY_RSHIFT:
	case KEY_CAPSLOCK:
	case KEY_BREAK:
		return true;
	default:
		return false;
	}
}

static bool is_nav(const input_event_t *ev, int *delta)
{
	int page = term_get_height();

	/*
	 * Ctrl is what enters scrollback, but once the view is off the
	 * bottom the arrows navigate on their own. That way a modifier the
	 * keyboard drops from a repeat cannot be read as an ordinary key
	 * and snap the view back down.
	 */
	if (!(ev->modifiers & MOD_CONTROL) && (offset == 0))
		return false;

	switch (ev->code) {
	case KEY_UP:		*delta = 1; return true;
	case KEY_DOWN:		*delta = -1; return true;
	case KEY_PAGEUP:	*delta = page; return true;
	case KEY_PAGEDOWN:	*delta = -page; return true;
	default:		return false;
	}
}

bool scrollback_key(const input_event_t *ev)
{
	int delta, want, limit;

	if (!enabled || dazzler_state)
		return false;

	if (is_nav(ev, &delta)) {
		limit = max_offset();
		want = offset + delta;
		if (want < 0)
			want = 0;
		if (want > limit)
			want = limit;
		if (want != offset) {
			/* the cursor would blink the live text through
			   the history, so it stays off the screen until
			   the view is back at the bottom */
			if (offset == 0)
				term_hold_cursor(true);
			offset = want;
			/* a held key queues faster than a repaint takes, so
			   only draw once the run of them has been consumed */
			if (!keyboard_key_available())
				repaint();
			if (offset == 0)
				term_hold_cursor(false);
		}
		return true;
	}

	if (is_modifier(ev->code))
		return false;

	/* anything else means the user wants to be back at the prompt */
	if ((ev->state == KEY_STATE_PRESSED) && (offset != 0)) {
		offset = 0;
		repaint();
		term_hold_cursor(false);
	}
	return false;
}

void scrollback_leave(void)
{
	if (offset != 0) {
		offset = 0;
		repaint();
		term_hold_cursor(false);
	}
}

bool scrollback_active(void)
{
	return enabled && (offset != 0);
}

int scrollback_offset(void)
{
	return offset;
}

void scrollback_enable(bool on)
{
	if (!on)
		offset = 0;
	enabled = on;
}

void scrollback_init(void)
{
	scrollback_clear();
	term_set_observer(observe);
}
