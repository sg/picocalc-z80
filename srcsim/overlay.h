/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * A box kit for the menus drawn over the running machine's screen, on top
 * of term_overlay_*: one centred box at a time, bordered rows, a message
 * row, a key wait that can time out so a view can follow live state, and a
 * one-line editor. Everything here runs on core 1 with the CPU stopped.
 */

#ifndef OVERLAY_INC
#define OVERLAY_INC

#include <stdbool.h>
#include <stdint.h>

#include "lcd.h"
#include "keyboard.h"

#define OV_MAX_COLS	46
#define OV_MSG_LEN	48

#define OV_FG		RGB(255, 255, 255)
#define OV_BG		RGB(  0,   0, 160)
#define OV_TITLE	RGB(255, 255,  85)
#define OV_DIM		RGB(170, 170, 170)
#define OV_WARN		RGB(255,  85,  85)

extern char ov_msg[OV_MSG_LEN];

/* a box of rows, as wide as the screen allows up to OV_MAX_COLS */
extern void ov_open(int rows);
extern void ov_close(void);
extern int ov_cols(void);
extern int ov_rows(void);

extern void ov_edge(int row, const char *title);
/* a row between the side borders, blank padded; the colours fill the row,
   which is how a highlight bar is drawn */
extern void ov_line(int row, const char *s, u16 fg, u16 bg);
extern void ov_text(int row, int col, u16 fg, u16 bg, const char *s);
extern void ov_blank(int from, int to);

/* the next key press, a held arrow repeating; .state is 0 when timeout_ms
   passed without one, and 0 waits as long as it takes */
extern input_event_t ov_wait_key(uint32_t timeout_ms);
extern bool ov_nav_key(unsigned char code);

/* edit buf in place on a row after prompt; true on Enter, false on Esc,
   which puts the old text back */
extern bool ov_edit(int row, const char *prompt, char *buf, int max);

/* the 8.3 stems matching pattern in dir, upper case and in name order; how
   many, up to max */
#define OV_NAME_LEN	8
extern int ov_list_dir(const char *dir, const char *pattern,
		       char names[][OV_NAME_LEN + 1], int max);

/* a list to choose from, filling the box: the arrows, PgUp, PgDn, Home and
   End move, Enter returns the index, Esc -1; ov_msg shows under the list */
extern int ov_pick(const char *title, const char *footer,
		   char names[][OV_NAME_LEN + 1], int n, int sel);

#endif /* !OVERLAY_INC */
