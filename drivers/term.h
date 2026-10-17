#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "types.h"

#define ANSI_STACK_SIZE 32

#define DEFAULT_FG 15
#define DEFAULT_BG 0

#define HISTORY_MAX 32
typedef struct {
	char* buffer[HISTORY_MAX];
	int current;
} history_t;

#define CURSOR_BLINK_MS 300

/* what an observer is told about, see term_set_observer() */
enum {
	TERM_OBS_CHAR,		/* character c drawn at column x of row y */
	TERM_OBS_CLEAR,		/* whole screen cleared, rows restart at 0 */
	TERM_OBS_ERASE_EOL,	/* row y cleared from column x rightwards */
	TERM_OBS_ERASE_ROW,	/* row y cleared */
};

void stdio_picocalc_init();
void stdio_picocalc_deinit();
void stdio_picocalc_out_chars(const char *buf, int length);
void term_clear();
/* start over at the top of a screen something else has already blanked */
void term_reset();
void term_scroll(int lines);
void term_erase_line(int y);
int term_get_width();
int term_get_scroll();
void term_set_observer(void (*callback)(int what, int x, int y, char c));
int term_get_height();
int term_get_x();
int term_get_y();
void term_set_pos(int x, int y);
u16 term_get_fg();
u16 term_get_bg();
void term_set_fg(u16 color);
void term_set_bg(u16 color);
bool term_get_blinking_cursor();
void term_set_blinking_cursor(bool enabled);
/* keep the cursor off the screen while something else owns it, leaving the program's setting alone */
void term_hold_cursor(bool hold);
/*
 * An overlay is a box drawn over the text, in screen cells, for something
 * that is not the machine's output. It paints the panel directly and leaves
 * the shadow alone, so closing it repaints the box from the shadow and the
 * text underneath comes back as it was. The caller keeps the machine quiet
 * while it is open; fills and text outside the box are clipped away.
 */
void term_overlay_open(int col, int row, int cols, int rows);
void term_overlay_fill(int col, int row, int cols, int rows, u16 bg);
void term_overlay_text(int col, int row, u16 fg, u16 bg, const char *s);
void term_overlay_close(void);
void term_blit(const char* text, const char* fg, const char* bg);
int term_readline(const char* prompt, char* buffer, int max_length, history_t* history);
