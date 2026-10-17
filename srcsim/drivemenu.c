/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The drive menu. See drivemenu.h.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "lcd.h"
#include "term.h"
#include "keyboard.h"

#include "sim.h"
#include "simdefs.h"

#include "disks.h"
#include "drivemenu.h"

#define BOX_COLS	46
#define BOX_ROWS	8

#define FG		RGB(255, 255, 255)
#define BG		RGB(  0,   0, 160)
#define TITLE		RGB(255, 255,  85)

static int box_col, box_row, box_cols;

/* a line of the box with its side borders, blank padded to the width */
static void box_line(int row, const char *text)
{
	char line[128];
	int n = snprintf(line, sizeof(line), "|%s", text);

	while (n < box_cols - 1 && n < (int) sizeof(line) - 1)
		line[n++] = ' ';
	line[n] = '|';
	line[n + 1] = '\0';
	term_overlay_text(box_col, box_row + row, FG, BG, line);
}

/* a border row with the title let into it */
static void box_edge(int row, const char *title)
{
	char line[128];
	int n = strlen(title), at;

	memset(line, '-', box_cols);
	line[0] = line[box_cols - 1] = '+';
	line[box_cols] = '\0';
	term_overlay_text(box_col, box_row + row, FG, BG, line);
	if (n) {
		at = (box_cols - n - 2) / 2;
		term_overlay_text(box_col + at, box_row + row, TITLE, BG, " ");
		term_overlay_text(box_col + at + 1, box_row + row, TITLE, BG, title);
		term_overlay_text(box_col + at + 1 + n, box_row + row, TITLE, BG, " ");
	}
}

/* the image in a slot, as the drive menu of the config dialog shows it */
static const char *image_name(int slot)
{
	const char *p = disks[slot];

	if (!p[0])
		return "(empty)";
	return strrchr(p, '/') ? strrchr(p, '/') + 1 : p;
}

static void draw(void)
{
	char line[128];

	box_edge(0, "Drives");
	box_line(1, "");
	snprintf(line, sizeof(line), "  A: %-14s B: %-14s",
		 image_name(0), image_name(1));
	box_line(2, line);
	snprintf(line, sizeof(line), "  C: %-14s D: %-14s",
		 image_name(2), image_name(3));
	box_line(3, line);
	snprintf(line, sizeof(line), "  I: %-14s", image_name(HD_SLOT));
	box_line(4, line);
	box_line(5, "");
	box_line(6, "  Esc: close");
	box_edge(7, "");
}

void drive_menu(void)
{
	input_event_t ev;

	box_cols = BOX_COLS;
	if (box_cols > term_get_width())
		box_cols = term_get_width();
	box_col = (term_get_width() - box_cols) / 2;
	box_row = (term_get_height() - BOX_ROWS) / 2;
	if (box_row < 0)
		box_row = 0;

	term_overlay_open(box_col, box_row, box_cols, BOX_ROWS);
	draw();
	for (;;) {
		ev = keyboard_poll(false);
		if (ev.state == KEY_STATE_PRESSED && ev.code == KEY_ESC)
			break;
		sleep_ms(10);
	}
	term_overlay_close();
}
