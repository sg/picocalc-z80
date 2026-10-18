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

#include "term.h"

#include "sim.h"
#include "simdefs.h"

#include "disks.h"
#include "simcfg.h"
#include "scrollback.h"
#include "overlay.h"
#include "drivemenu.h"

#define BOX_MIN_ROWS	14
#define BOX_MAX_ROWS	22
#define NAMES_MAX	48	/* images the picker lists */

_Static_assert(NUMDRV == 5 && HD_SLOT == 4, "the menu lists A to D and I");
_Static_assert(FNLEN == OV_NAME_LEN, "the box kit lists CP/M names");

/* the image in a slot without its directory, as CP/M would name it */
static const char *image_name(int slot)
{
	const char *p = strrchr(disks[slot], '/');

	if (!disks[slot][0])
		return "(empty)";
	return p ? p + 1 : disks[slot];
}

/* ------------------------------------------------------------------ */
/* the picker                                                          */

/* the images on the card for a slot */
static int list_images(int slot, char names[][FNLEN + 1])
{
	return ov_list_dir(DISKS_DIR, (slot == HD_SLOT) ? "*.HDD" : "*.DSK",
			   names, NAMES_MAX);
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
	char buf[OV_MAX_COLS + 1];
	int rows = ov_rows() - 5, i, k, in;

	snprintf(buf, sizeof(buf), "Disk for %c:", drive_letter(slot));
	ov_edge(0, buf);
	ov_line(1, "", OV_FG, OV_BG);
	for (i = 0; i < rows; i++) {
		k = top + i;
		if (k >= n) {
			ov_line(2 + i, (k == 0) ? "(no images)" : "", OV_DIM,
				OV_BG);
			continue;
		}
		in = in_slot(names[k], slot);
		if (in >= 0)
			snprintf(buf, sizeof(buf), "%-8s  in %c:", names[k],
				 drive_letter(in));
		else
			snprintf(buf, sizeof(buf), "%s", names[k]);
		ov_line(2 + i, buf,
			(k == sel) ? OV_BG : (in >= 0) ? OV_DIM : OV_FG,
			(k == sel) ? OV_FG : OV_BG);
	}
	/* more above or below the window */
	ov_text(2, ov_cols() - 3, OV_DIM, OV_BG, (top > 0) ? "^" : " ");
	ov_text(1 + rows, ov_cols() - 3, OV_DIM, OV_BG,
		(top + rows < n) ? "v" : " ");
	ov_line(ov_rows() - 3, "Enter: insert   Esc: back", OV_FG, OV_BG);
	ov_line(ov_rows() - 2, ov_msg, OV_TITLE, OV_BG);
	ov_edge(ov_rows() - 1, "");
}

/* choose an image for a slot; true when the drive's contents changed */
static bool pick_image(int slot)
{
	char names[NAMES_MAX][FNLEN + 1];
	const char *cur = image_name(slot), *err;
	int n = list_images(slot, names), rows = ov_rows() - 5;
	int sel = 0, top = 0, i;
	input_event_t ev;

	/* start on the disk that is in the drive now */
	for (i = 0; i < n; i++) {
		size_t len = strlen(names[i]);

		if (!strncasecmp(names[i], cur, len) && cur[len] == '.')
			sel = i;
	}
	ov_msg[0] = '\0';
	for (;;) {
		if (sel < top)
			top = sel;
		if (sel >= top + rows)
			top = sel - rows + 1;
		draw_picker(slot, names, n, sel, top);
		ev = ov_wait_key(0);
		ov_msg[0] = '\0';
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
				snprintf(ov_msg, sizeof(ov_msg), "%s", err);
				break;
			}
			snprintf(ov_msg, sizeof(ov_msg), "%c: now %s",
				 drive_letter(slot), image_name(slot));
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
	char buf[OV_MAX_COLS + 1];
	int i;

	ov_edge(0, "Drives");
	ov_line(1, "", OV_FG, OV_BG);
	for (i = 0; i < NUMDRV; i++) {
		snprintf(buf, sizeof(buf), "%c: %.40s", drive_letter(i),
			 image_name(i));
		ov_line(2 + i, buf, (i == sel) ? OV_BG : OV_FG,
			(i == sel) ? OV_FG : OV_BG);
	}
	ov_line(7, "", OV_FG, OV_BG);
	ov_line(8, "Up, Down, A-D, I: choose a drive", OV_FG, OV_BG);
	ov_line(9, "Enter: insert a disk    Del: eject", OV_FG, OV_BG);
	ov_line(10, "Esc: close", OV_FG, OV_BG);
	ov_line(11, "CP/M sees a changed disk after ^C", OV_DIM, OV_BG);
	ov_blank(12, ov_rows() - 2);
	ov_line(ov_rows() - 2, ov_msg, OV_TITLE, OV_BG);
	ov_edge(ov_rows() - 1, "");
}

void drive_menu(void)
{
	int sel = 0, slot, rows;
	bool changed = false, done = false;
	input_event_t ev;

	/* the box goes over the live screen, not over the history */
	scrollback_leave();

	rows = term_get_height() - 4;
	if (rows > BOX_MAX_ROWS)
		rows = BOX_MAX_ROWS;
	if (rows < BOX_MIN_ROWS)
		rows = BOX_MIN_ROWS;
	ov_open(rows);
	while (!done) {
		draw_drives(sel);
		ev = ov_wait_key(0);
		ov_msg[0] = '\0';
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
				snprintf(ov_msg, sizeof(ov_msg), "%c: ejected",
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
	ov_close();

	/* the drives are part of the saved configuration, as they are when
	   the machine starts */
	if (changed)
		save_config();
}
