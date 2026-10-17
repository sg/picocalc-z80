/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The overlay box kit. See overlay.h.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "term.h"
#include "overlay.h"

#define EDIT_MAX	128

char ov_msg[OV_MSG_LEN];

static int box_col, box_row, box_cols, box_rows;

void ov_open(int rows)
{
	box_cols = OV_MAX_COLS;
	if (box_cols > term_get_width())
		box_cols = term_get_width();
	box_rows = rows;
	if (box_rows > term_get_height())
		box_rows = term_get_height();
	box_col = (term_get_width() - box_cols) / 2;
	box_row = (term_get_height() - box_rows) / 2;
	ov_msg[0] = '\0';
	term_overlay_open(box_col, box_row, box_cols, box_rows);
}

void ov_close(void)
{
	term_overlay_close();
}

int ov_cols(void)
{
	return box_cols;
}

int ov_rows(void)
{
	return box_rows;
}

void ov_text(int row, int col, u16 fg, u16 bg, const char *s)
{
	term_overlay_text(box_col + col, box_row + row, fg, bg, s);
}

void ov_line(int row, const char *s, u16 fg, u16 bg)
{
	char buf[OV_MAX_COLS + 1];
	int w = box_cols - 2, n = snprintf(buf, sizeof(buf), " %s", s);

	if (n > w)
		n = w;
	memset(buf + n, ' ', w - n);
	buf[w] = '\0';
	ov_text(row, 0, OV_FG, OV_BG, "|");
	ov_text(row, 1, fg, bg, buf);
	ov_text(row, box_cols - 1, OV_FG, OV_BG, "|");
}

/* a top or bottom border, with the title let into it */
void ov_edge(int row, const char *title)
{
	char buf[OV_MAX_COLS + 1];
	int n = strlen(title);

	memset(buf, '-', box_cols);
	buf[0] = buf[box_cols - 1] = '+';
	buf[box_cols] = '\0';
	ov_text(row, 0, OV_FG, OV_BG, buf);
	if (n && n + 4 <= box_cols) {
		snprintf(buf, sizeof(buf), " %s ", title);
		ov_text(row, (box_cols - n - 2) / 2, OV_TITLE, OV_BG, buf);
	}
}

void ov_blank(int from, int to)
{
	for (int r = from; r < to; r++)
		ov_line(r, "", OV_FG, OV_BG);
}

bool ov_nav_key(unsigned char code)
{
	return code == KEY_UP || code == KEY_DOWN || code == KEY_PAGEUP ||
	       code == KEY_PAGEDOWN;
}

input_event_t ov_wait_key(uint32_t timeout_ms)
{
	absolute_time_t until = make_timeout_time_ms(timeout_ms);
	input_event_t ev;

	for (;;) {
		ev = keyboard_poll(false);
		if (ev.state == KEY_STATE_PRESSED ||
		    (ev.state == KEY_STATE_HOLD && ov_nav_key(ev.code)))
			return ev;
		if (timeout_ms && time_reached(until))
			return (input_event_t) { 0, 0, KEY_NONE };
		sleep_ms(10);
	}
}

bool ov_edit(int row, const char *prompt, char *buf, int max)
{
	char keep[EDIT_MAX], shown[OV_MAX_COLS + 1];
	int n = strlen(buf), w = box_cols - 4 - (int) strlen(prompt), start;
	input_event_t ev;

	if (max > EDIT_MAX)
		max = EDIT_MAX;
	strncpy(keep, buf, sizeof(keep) - 1);
	keep[sizeof(keep) - 1] = '\0';
	for (;;) {
		/* the tail of the text once it is longer than the row */
		start = (n > w) ? n - w : 0;
		snprintf(shown, sizeof(shown), "%s%s_", prompt, buf + start);
		ov_line(row, shown, OV_FG, OV_BG);
		ev = ov_wait_key(0);
		if (ev.code == KEY_ENTER)
			return true;
		if (ev.code == KEY_ESC) {
			strcpy(buf, keep);
			return false;
		}
		if (ev.code == KEY_BACKSPACE || ev.code == KEY_DELETE) {
			if (n)
				buf[--n] = '\0';
		} else if (!(ev.modifiers & (MOD_CONTROL | MOD_ALT)) &&
			   ev.code >= 32 && ev.code < 127 && n < max - 1) {
			buf[n++] = ev.code;
			buf[n] = '\0';
		}
	}
}
