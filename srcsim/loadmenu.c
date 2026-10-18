/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The program loader. See loadmenu.h.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "term.h"

#include "sim.h"
#include "simdefs.h"

#include "disks.h"
#include "overlay.h"
#include "loadmenu.h"

#define NAMES_MAX	48
#define BOX_MIN_ROWS	12
#define BOX_MAX_ROWS	22

bool load_menu(char *name, size_t len, int *bytes)
{
	char names[NAMES_MAX][OV_NAME_LEN + 1];
	const char *err;
	int n, sel = 0, rows;

	rows = term_get_height() - 4;
	if (rows > BOX_MAX_ROWS)
		rows = BOX_MAX_ROWS;
	if (rows < BOX_MIN_ROWS)
		rows = BOX_MIN_ROWS;
	ov_open(rows);
	n = ov_list_dir(CODE_DIR, "*.BIN", names, NAMES_MAX);
	for (;;) {
		sel = ov_pick("Load a program", "Enter: load at 0   Esc: back",
			      names, n, sel);
		if (sel < 0) {
			ov_close();
			return false;
		}
		if (load_image(names[sel], bytes, &err)) {
			snprintf(name, len, "%s", names[sel]);
			ov_close();
			return true;
		}
		snprintf(ov_msg, sizeof(ov_msg), "%s", err);
	}
}
