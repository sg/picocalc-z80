/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The drive menu. See drivemenu.h.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"

#include "ff.h"

#include "lcd.h"
#include "term.h"
#include "keyboard.h"

#include "sim.h"
#include "simdefs.h"

#include "disks.h"
#include "simcfg.h"
#include "scrollback.h"
#include "drivemenu.h"

#define BOX_COLS	46
#define BOX_MIN_ROWS	14
#define BOX_MAX_ROWS	22
#define NAMES_MAX	48	/* images the picker lists */
#define MSG_LEN		48

#define FG		RGB(255, 255, 255)
#define BG		RGB(  0,   0, 160)
#define TITLE		RGB(255, 255,  85)
#define DIM		RGB(170, 170, 170)

_Static_assert(NUMDRV == 5 && HD_SLOT == 4, "the menu lists A to D and I");

static int box_col, box_row, box_cols, box_rows;
static char msg[MSG_LEN];

static void text(int row, int col, u16 fg, u16 bg, const char *s)
{
	term_overlay_text(box_col + col, box_row + row, fg, bg, s);
}

/* a row between the side borders, blank padded; the colours fill the row,
   which is how the highlight bar is drawn */
static void line(int row, const char *s, u16 fg, u16 bg)
{
	char buf[BOX_COLS + 1];
	int w = box_cols - 2, n = snprintf(buf, sizeof(buf), " %s", s);

	if (n > w)
		n = w;
	memset(buf + n, ' ', w - n);
	buf[w] = '\0';
	text(row, 0, FG, BG, "|");
	text(row, 1, fg, bg, buf);
	text(row, box_cols - 1, FG, BG, "|");
}

/* a top or bottom border, with the title let into it */
static void edge(int row, const char *title)
{
	char buf[BOX_COLS + 1];
	int n = strlen(title);

	memset(buf, '-', box_cols);
	buf[0] = buf[box_cols - 1] = '+';
	buf[box_cols] = '\0';
	text(row, 0, FG, BG, buf);
	if (n && n + 4 <= box_cols) {
		snprintf(buf, sizeof(buf), " %s ", title);
		text(row, (box_cols - n - 2) / 2, TITLE, BG, buf);
	}
}

/* the image in a slot without its directory, as CP/M would name it */
static const char *image_name(int slot)
{
	const char *p = strrchr(disks[slot], '/');

	if (!disks[slot][0])
		return "(empty)";
	return p ? p + 1 : disks[slot];
}

static bool is_nav(unsigned char code)
{
	return code == KEY_UP || code == KEY_DOWN || code == KEY_PAGEUP ||
	       code == KEY_PAGEDOWN;
}

/* the next key press; a held arrow repeats */
static input_event_t wait_key(void)
{
	input_event_t ev;

	for (;;) {
		ev = keyboard_poll(false);
		if (ev.state == KEY_STATE_PRESSED ||
		    (ev.state == KEY_STATE_HOLD && is_nav(ev.code)))
			return ev;
		sleep_ms(10);
	}
}

/* ------------------------------------------------------------------ */
/* the picker                                                          */

/* the images on the card for a slot: upper case stems, in name order */
static int list_images(int slot, char names[][FNLEN + 1])
{
	DIR dp;
	FILINFO fno;
	char stem[FNLEN + 1];
	const char *dot;
	int n = 0, len, i;

	if (f_findfirst(&dp, &fno, DISKS_DIR,
			(slot == HD_SLOT) ? "*.HDD" : "*.DSK") != FR_OK)
		return 0;
	while (fno.fname[0] && n < NAMES_MAX) {
		dot = strrchr(fno.fname, '.');
		len = dot ? (int) (dot - fno.fname) : (int) strlen(fno.fname);
		/* the drive table only has room for an 8.3 name */
		if (len > 0 && len <= FNLEN) {
			for (i = 0; i < len; i++)
				stem[i] = toupper((unsigned char) fno.fname[i]);
			stem[len] = '\0';
			for (i = n; i > 0 && strcmp(names[i - 1], stem) > 0; i--)
				strcpy(names[i], names[i - 1]);
			strcpy(names[i], stem);
			n++;
		}
		if (f_findnext(&dp, &fno) != FR_OK)
			break;
	}
	f_closedir(&dp);
	return n;
}

/* which other drive of the same kind holds this image, or -1 */
static int in_slot(const char *stem, int except)
{
	const char *base;
	size_t n = strlen(stem);
	int i;

	for (i = 0; i < NUMDRV; i++) {
		if (i == except || !disks[i][0] ||
		    (i == HD_SLOT) != (except == HD_SLOT))
			continue;
		base = strrchr(disks[i], '/');
		base = base ? base + 1 : disks[i];
		if (!strncasecmp(base, stem, n) && base[n] == '.')
			return i;
	}
	return -1;
}

static void draw_picker(int slot, char names[][FNLEN + 1], int n, int sel,
			int top)
{
	char buf[BOX_COLS + 1];
	int rows = box_rows - 5, i, k, in;

	snprintf(buf, sizeof(buf), "Disk for %c:", drive_letter(slot));
	edge(0, buf);
	line(1, "", FG, BG);
	for (i = 0; i < rows; i++) {
		k = top + i;
		if (k >= n) {
			line(2 + i, (k == 0) ? "(no images)" : "", DIM, BG);
			continue;
		}
		in = in_slot(names[k], slot);
		if (in >= 0)
			snprintf(buf, sizeof(buf), "%-8s  in %c:", names[k],
				 drive_letter(in));
		else
			snprintf(buf, sizeof(buf), "%s", names[k]);
		line(2 + i, buf, (k == sel) ? BG : (in >= 0) ? DIM : FG,
		     (k == sel) ? FG : BG);
	}
	/* more above or below the window */
	text(2, box_cols - 3, DIM, BG, (top > 0) ? "^" : " ");
	text(1 + rows, box_cols - 3, DIM, BG, (top + rows < n) ? "v" : " ");
	line(box_rows - 3, "Enter: insert   Esc: back", FG, BG);
	line(box_rows - 2, msg, TITLE, BG);
	edge(box_rows - 1, "");
}

/* choose an image for a slot; true when the drive's contents changed */
static bool pick_image(int slot)
{
	char names[NAMES_MAX][FNLEN + 1];
	const char *cur = image_name(slot), *err;
	int n = list_images(slot, names), rows = box_rows - 5;
	int sel = 0, top = 0, i;
	input_event_t ev;

	/* start on the disk that is in the drive now */
	for (i = 0; i < n; i++) {
		size_t len = strlen(names[i]);

		if (!strncasecmp(names[i], cur, len) && cur[len] == '.')
			sel = i;
	}
	msg[0] = '\0';
	for (;;) {
		if (sel < top)
			top = sel;
		if (sel >= top + rows)
			top = sel - rows + 1;
		draw_picker(slot, names, n, sel, top);
		ev = wait_key();
		msg[0] = '\0';
		switch (ev.code) {
		case KEY_UP:
			if (sel > 0)
				sel--;
			break;
		case KEY_DOWN:
			if (sel < n - 1)
				sel++;
			break;
		case KEY_PAGEUP:
			sel = (sel > rows) ? sel - rows : 0;
			break;
		case KEY_PAGEDOWN:
			sel = (sel + rows < n) ? sel + rows : n - 1;
			break;
		case KEY_HOME:
			sel = 0;
			break;
		case KEY_END:
			sel = n - 1;
			break;
		case KEY_ENTER:
			if (!n)
				break;
			if (!mount_image(slot, names[sel], &err)) {
				snprintf(msg, sizeof(msg), "%s", err);
				break;
			}
			snprintf(msg, sizeof(msg), "%c: now %s", drive_letter(slot),
				 image_name(slot));
			return true;
		case KEY_ESC:
			return false;
		}
		if (sel < 0)
			sel = 0;
	}
}

/* ------------------------------------------------------------------ */
/* the drives                                                          */

static void draw_drives(int sel)
{
	char buf[BOX_COLS + 1];
	int i;

	edge(0, "Drives");
	line(1, "", FG, BG);
	for (i = 0; i < NUMDRV; i++) {
		snprintf(buf, sizeof(buf), "%c: %.40s", drive_letter(i),
			 image_name(i));
		line(2 + i, buf, (i == sel) ? BG : FG, (i == sel) ? FG : BG);
	}
	line(7, "", FG, BG);
	line(8, "Up, Down, A-D, I: choose a drive", FG, BG);
	line(9, "Enter: insert a disk    Del: eject", FG, BG);
	line(10, "Esc: close", FG, BG);
	line(11, "CP/M sees a changed disk after ^C", DIM, BG);
	for (i = 12; i < box_rows - 2; i++)
		line(i, "", FG, BG);
	line(box_rows - 2, msg, TITLE, BG);
	edge(box_rows - 1, "");
}

void drive_menu(void)
{
	int sel = 0, slot;
	bool changed = false, done = false;
	input_event_t ev;

	/* the box goes over the live screen, not over the history */
	scrollback_leave();

	box_cols = BOX_COLS;
	if (box_cols > term_get_width())
		box_cols = term_get_width();
	box_rows = term_get_height() - 4;
	if (box_rows > BOX_MAX_ROWS)
		box_rows = BOX_MAX_ROWS;
	if (box_rows < BOX_MIN_ROWS)
		box_rows = BOX_MIN_ROWS;
	if (box_rows > term_get_height())
		box_rows = term_get_height();
	box_col = (term_get_width() - box_cols) / 2;
	box_row = (term_get_height() - box_rows) / 2;
	msg[0] = '\0';

	term_overlay_open(box_col, box_row, box_cols, box_rows);
	while (!done) {
		draw_drives(sel);
		ev = wait_key();
		msg[0] = '\0';
		slot = -1;
		switch (tolower(ev.code)) {
		case 'a': case 'b': case 'c': case 'd':
			slot = tolower(ev.code) - 'a';
			break;
		case 'i':
			slot = HD_SLOT;
			break;
		case KEY_UP:
			if (sel > 0)
				sel--;
			break;
		case KEY_DOWN:
			if (sel < NUMDRV - 1)
				sel++;
			break;
		case KEY_ENTER:
			slot = sel;
			break;
		case KEY_DELETE:
		case KEY_BACKSPACE:
			if (disks[sel][0]) {
				eject_disk(sel);
				changed = true;
				snprintf(msg, sizeof(msg), "%c: ejected",
					 drive_letter(sel));
			}
			break;
		case KEY_ESC:
			done = true;
			break;
		}
		if (slot >= 0) {
			sel = slot;
			if (pick_image(slot))
				changed = true;
		}
	}
	term_overlay_close();

	/* the drives are part of the saved configuration, as they are when
	   the machine starts */
	if (changed)
		save_config();
}
