#include "term.h"

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include <stdlib.h>
#include <string.h>

#include "lcd.h"
#include "keyboard.h"

stdio_driver_t stdio_picocalc;
static void (*chars_available_callback)(void *) = NULL;
static void *chars_available_param = NULL;
static repeating_timer_t cursor_timer;

static void set_chars_available_callback(void (*fn)(void *), void *param) {
	chars_available_callback = fn;
	chars_available_param = param;
}

// Function to be called when characters become available
void chars_available_notify(void) {
	if (chars_available_callback) {
		chars_available_callback(chars_available_param);
	}
}

void stdio_picocalc_init() {
	keyboard_set_key_available_callback(chars_available_notify);
	stdio_set_driver_enabled(&stdio_picocalc, true);
}

void stdio_picocalc_deinit() {
	stdio_set_driver_enabled(&stdio_picocalc, false);
}

const static unsigned short palette[16] = {
	RGB(  0,   0,   0), // 0 black
	RGB(194,  54,  33), // 1 red
	RGB( 37, 188,  36), // 2 green
	RGB(173, 173,  39), // 3 yellow
	RGB( 73,  46, 225), // 4 blue
	RGB(211,  56, 211), // 5 magenta
	RGB( 51, 187, 200), // 6 cyan
	RGB(203, 204, 205), // 7 white
	// high intensity
	RGB( 85,  85,  85), // 8 black
	RGB(255,  85,  85), // 9 red
	RGB( 85, 255,  85), // a green
	RGB(255, 255,  85), // b yellow
	RGB(85,   85, 255), // c blue
	RGB(255,  85, 255), // d magenta
	RGB( 85, 255, 255), // e cyan
	RGB(255, 255, 255), // f white
};

enum {
	AnsiNone,
	AnsiEscape,
	AnsiBracket,
};

typedef struct {
	int state;
	int x, y, cx, cy, len;
	u16 fg, bg;
	char stack[ANSI_STACK_SIZE];
	int stack_size;
	int scroll;
	bool cursor_enabled;
	bool cursor_visible;
	bool cursor_manual;
	bool cursor_held;
	bool c_inverse;
	bool c_bold;
} ansi_t;

static ansi_t ansi = {
	.state=AnsiNone,
	.x=0, .y=0,
	.fg=palette[DEFAULT_FG],
	.bg=palette[DEFAULT_BG],
	.stack={0},
	.stack_size=0,
	.cursor_enabled=false,
	.c_inverse=false,
	.c_bold=false,
};

static int len_col(int len) {
	return (ansi.x + len) % font.term_width;
}

static int len_row(int len) {
	return ansi.y + (ansi.x + len) / font.term_width;
}

/*
 * The panel cannot be read back, so a shadow of every cell in the scroll
 * ring is kept for the cursor, which shows its cell inverted and then puts
 * it back. Colours go through a table so that a cell is three bytes.
 */
typedef struct { u8 c, fg, bg; } cell_t;

static cell_t *cells;
static int cells_cols, cells_rows;
static u16 colors[256];
static int ncolors;

static u8 color_index(u16 color) {
	int i, best = 0, bestd = 0x7fffffff;
	for (i = 0; i < ncolors; i++)
		if (colors[i] == color) return i;
	if (ncolors < 256) {
		colors[ncolors] = color;
		return ncolors++;
	}
	// only 240 distinct 24 bit colours get here, then the nearest one has to do
	for (i = 0; i < ncolors; i++) {
		int d = abs(RED(colors[i]) - RED(color)) + abs(GREEN(colors[i]) - GREEN(color)) + abs(BLUE(colors[i]) - BLUE(color));
		if (d < bestd) { bestd = d; best = i; }
	}
	return best;
}

// the shadow follows the font and the scroll area, which only change through term_clear()
static void cells_fit() {
	int rows = (lcd_scroll_height + font.glyph_height - 1) / font.glyph_height;
	int cols = font.term_width;
	if (!ncolors) {
		memcpy(colors, palette, sizeof(palette));
		ncolors = 16;
	}
	if (cells && rows == cells_rows && cols == cells_cols) return;
	cell_t *old = cells;
	// the blink timer on the other core reads this, so it must never pair the old block with the new size
	cells = NULL;
	free(old);
	cells_rows = rows;
	cells_cols = cols;
	cells = malloc(rows * cols * sizeof(cell_t));
}

static cell_t *cell_at(int col, int row) {
	if (!cells || col < 0 || col >= cells_cols) return NULL;
	row %= cells_rows;
	if (row < 0) row += cells_rows;
	return cells + row * cells_cols + col;
}

static void shadow_char(int col, int row, u16 fg, u16 bg, char c) {
	cell_t *cell = cell_at(col, row);
	if (cell) {
		cell->c = c;
		cell->fg = color_index(fg);
		cell->bg = color_index(bg);
	}
	// drawing over the cursor takes it off the screen, so a later blink-off must not put it back
	if (ansi.cursor_visible && ansi.cy == row && ansi.cx == col) ansi.cursor_visible = false;
}

static void shadow_blank(int col, int row, int ncols, u16 bg) {
	u8 fgi = color_index(ansi.fg), bgi = color_index(bg);
	for (int i = col; i < col + ncols; i++) {
		cell_t *cell = cell_at(i, row);
		if (cell) {
			cell->c = ' ';
			cell->fg = fgi;
			cell->bg = bgi;
		}
	}
	if (ansi.cursor_visible && ansi.cy == row && ansi.cx >= col && ansi.cx < col + ncols) ansi.cursor_visible = false;
}

static int row_y(int row) {
	return (row * font.glyph_height) % lcd_scroll_height;
}

// a row that straddles the end of the ring is drawn again at the top
static void paint_char(int col, int row, u16 fg, u16 bg, char c) {
	int x = col * font.glyph_width, y = row_y(row);
	lcd_draw_char(x, y, fg, bg, c);
	if (y > lcd_scroll_height - font.glyph_height)
		lcd_draw_char(x, y - lcd_scroll_height, fg, bg, c);
}

static void paint_fill(int col, int row, int width, u16 bg) {
	int x = col * font.glyph_width, y = row_y(row);
	lcd_fill(bg, x, y, width, font.glyph_height);
	if (y > lcd_scroll_height - font.glyph_height)
		lcd_fill(bg, x, y - lcd_scroll_height, width, font.glyph_height);
}

static void put_char(int col, int row, u16 fg, u16 bg, char c) {
	shadow_char(col, row, fg, bg, c);
	paint_char(col, row, fg, bg, c);
}

static void put_blank(int col, int row, int ncols, u16 bg) {
	shadow_blank(col, row, ncols, bg);
	paint_fill(col, row, ncols * font.glyph_width, bg);
}

static void (*observer)(int what, int x, int y, char c) = NULL;

void term_set_observer(void (*callback)(int what, int x, int y, char c)) {
	observer = callback;
}

static inline void observe(int what, int x, int y, char c) {
	if (observer) observer(what, x, y, c);
}

int term_get_scroll() {
	return ansi.scroll;
}

void term_scroll(int lines) {
	if (lines != ansi.scroll) {
		// the rows coming into view may be the very ones leaving the top, so clear them first
		int from = ansi.scroll + font.term_height;
		if (from < lines) from = lines;
		ansi.scroll = lines;
		for (int y = from; y < lines + font.term_height; y++)
			term_erase_line(y);
		lcd_scroll(lines * font.glyph_height);
	}
}

void term_reset() {
	observe(TERM_OBS_CLEAR, 0, 0, 0);
	ansi.x = ansi.y = ansi.len = 0;
	lcd_scroll(0);
	ansi.scroll = 0;
	cells_fit();
	if (cells) {
		cell_t blank = { ' ', color_index(ansi.fg), color_index(0) };
		for (int i = 0; i < cells_rows * cells_cols; i++) cells[i] = blank;
	}
	ansi.cursor_visible = false;
}

void term_clear() {
	// whatever sits below the scroll area is not the terminal's to clear
	lcd_fill(0, 0, 0, LCD_WIDTH, lcd_scroll_height);
	term_reset();
}

void term_erase_line(int y) {
	observe(TERM_OBS_ERASE_ROW, 0, y, 0);
	shadow_blank(0, y, font.term_width, ansi.bg);
	paint_fill(0, y, LCD_WIDTH, ansi.bg);
}

void term_erase_from_cursor() {
	observe(TERM_OBS_ERASE_EOL, ansi.x, ansi.y, 0);
	shadow_blank(ansi.x, ansi.y, font.term_width - ansi.x, ansi.bg);
	paint_fill(ansi.x, ansi.y, LCD_WIDTH - ansi.x * font.glyph_width, ansi.bg);
}

static void cursor_cell(int *col, int *row) {
	*col = len_col(ansi.len);
	*row = len_row(ansi.len);
	// a line that has just filled its last column keeps the cursor there until the next character wraps
	if (ansi.len == 0 && ansi.x >= font.term_width) {
		*col = font.term_width - 1;
		*row = ansi.y;
	}
}

// the block is the cell under it in inverse video, so nothing is lost when it blinks off
static void paint_cursor(bool on) {
	cell_t *cell = cell_at(ansi.cx, ansi.cy);
	if (cell) {
		u16 fg = colors[cell->fg], bg = colors[cell->bg];
		paint_char(ansi.cx, ansi.cy, on ? bg : fg, on ? fg : bg, cell->c);
	} else {
		// without a shadow to put back, only a bar that covers nothing will do
		paint_fill(ansi.cx, ansi.cy, 1, on ? ansi.fg : ansi.bg);
	}
}

static void draw_cursor() {
	if (ansi.cursor_enabled && !ansi.cursor_held && !ansi.cursor_visible) {
		cursor_cell(&ansi.cx, &ansi.cy);
		paint_cursor(true);
		ansi.cursor_visible = true;
	}
}

static void erase_cursor() {
	if (ansi.cursor_enabled && ansi.cursor_visible) {
		paint_cursor(false);
		ansi.cursor_visible = false;
	}
}

void term_hold_cursor(bool hold) {
	ansi.cursor_held = hold;
	ansi.cursor_manual = true;
	if (hold) erase_cursor();
	else draw_cursor();
}

/*
 * The overlay box, see term.h. Rows are screen rows here and ring rows in the
 * paint calls, so the box stays put whatever the scroll position is.
 */
static struct { bool open; int col, row, cols, rows; } overlay;

void term_overlay_open(int col, int row, int cols, int rows) {
	if (col < 0) { cols += col; col = 0; }
	if (row < 0) { rows += row; row = 0; }
	if (col + cols > font.term_width) cols = font.term_width - col;
	if (row + rows > font.term_height) rows = font.term_height - row;
	overlay.col = col;
	overlay.row = row;
	overlay.cols = cols > 0 ? cols : 0;
	overlay.rows = rows > 0 ? rows : 0;
	overlay.open = true;
	term_hold_cursor(true);
}

void term_overlay_fill(int col, int row, int cols, int rows, u16 bg) {
	int c0 = col > overlay.col ? col : overlay.col;
	int r0 = row > overlay.row ? row : overlay.row;
	int c1 = col + cols < overlay.col + overlay.cols ? col + cols : overlay.col + overlay.cols;
	int r1 = row + rows < overlay.row + overlay.rows ? row + rows : overlay.row + overlay.rows;
	if (!overlay.open || c1 <= c0 || r1 <= r0) return;
	for (int r = r0; r < r1; r++)
		paint_fill(c0, ansi.scroll + r, (c1 - c0) * font.glyph_width, bg);
}

void term_overlay_text(int col, int row, u16 fg, u16 bg, const char *s) {
	int n = strlen(s);
	int c0 = col > overlay.col ? col : overlay.col;
	int c1 = col + n < overlay.col + overlay.cols ? col + n : overlay.col + overlay.cols;
	if (!overlay.open || row < overlay.row || row >= overlay.row + overlay.rows) return;
	for (int c = c0; c < c1; c++)
		paint_char(c, ansi.scroll + row, fg, bg, s[c - col]);
}

void term_overlay_close(void) {
	if (!overlay.open) return;
	for (int r = overlay.row; r < overlay.row + overlay.rows; r++) {
		int ring = ansi.scroll + r;
		for (int c = overlay.col; c < overlay.col + overlay.cols; c++) {
			cell_t *cell = cell_at(c, ring);
			if (cell) paint_char(c, ring, colors[cell->fg], colors[cell->bg], cell->c);
			else paint_fill(c, ring, font.glyph_width, ansi.bg);
		}
	}
	overlay.open = false;
	term_hold_cursor(false);
}

static bool on_cursor_timer(repeating_timer_t *rt) {
	if (!ansi.cursor_manual && ansi.cursor_visible) {
		erase_cursor();
	} else {
		ansi.cursor_manual = false;
		draw_cursor();
	}
	return true;
}

bool term_get_blinking_cursor() {
	return ansi.cursor_enabled;
}

void term_set_blinking_cursor(bool enabled) {
	if (enabled && !ansi.cursor_enabled) {
		ansi.cursor_manual = true;
		ansi.cursor_enabled = true;
		draw_cursor();
		add_repeating_timer_ms(CURSOR_BLINK_MS, on_cursor_timer, NULL, &cursor_timer);
	} else if (!enabled && ansi.cursor_enabled) {
		erase_cursor();
		ansi.cursor_enabled = false;
		cancel_repeating_timer(&cursor_timer);
	}
}

int term_get_x() {
	return ansi.x;
}
int term_get_y() {
	return ansi.y;
}

int term_get_width() {
	return font.term_width;
}

int term_get_height() {
	return font.term_height;
}

void term_set_pos(int x, int y) {
	ansi.cursor_manual = true;
	erase_cursor();
	if (x >= 0 && x < font.term_width) ansi.x = x;
	if (y >= 0 && y < font.term_height) ansi.y = ansi.scroll + y;
	draw_cursor();
}

u16 term_get_fg() {
	return ansi.fg;
}
u16 term_get_bg() {
	return ansi.bg;
}

void term_set_fg(u16 color) {
	ansi.fg = color;
}
void term_set_bg(u16 color) {
	ansi.bg = color;
}

void term_blit(const char* text, const char* fg, const char* bg) {
	u16 pfg = ansi.fg, pbg = ansi.bg;
	const char *lfg = fg, *lbg = bg;
	while(*text) {
		if (*lfg >= '0' && *lfg <= '9') pfg = palette[*lfg - '0'];
		else if (*lfg >= 'a' && *lfg <= 'f') pfg = palette[*lfg - 'a' + 10];
		else if (*lfg >= 'A' && *lfg <= 'F') pfg = palette[*lfg - 'A' + 10];
		if (*lbg >= '0' && *lbg <= '9') pbg = palette[*lbg - '0'];
		else if (*lbg >= 'a' && *lbg <= 'f') pbg = palette[*lbg - 'a' + 10];
		else if (*lbg >= 'A' && *lbg <= 'F') pbg = palette[*lbg - 'A' + 10];
		put_char(ansi.x, ansi.y, pfg, pbg, *text);
		ansi.x += 1;
		if (ansi.x > font.term_width) return;
		text ++;
		lfg ++; if (!*lfg) lfg = fg;
		lbg ++; if (!*lbg) lbg = bg;
	}
}

static inline void should_scroll() {
	if (ansi.x >= font.term_width) {
		ansi.x = 0;
		ansi.y += 1;
	}
	if (ansi.y >= font.term_height) {
		term_scroll(ansi.y - (font.term_height - 1));
	}
}

static void out_char(char c) {
	u16 fg, bg;
	if (ansi.c_inverse) {
		fg = ansi.bg;
		bg = ansi.fg;
	} else {
		fg = ansi.fg;
		bg = ansi.bg;
	}
	
	if (c == '\f') {
		term_clear();
	} else if (c == '\n') {
		ansi.x = 0;
		ansi.y += 1;
		//term_erase_line(ansi.y);
		should_scroll();
	} else if (c == '\b') ansi.x -= 1;
	else if (c == '\r') ansi.x = 0;
	else {
		if (c == '\t') c = ' ';
		if (c >= 32 && c < 127) {
			should_scroll();
			put_char(ansi.x, ansi.y, fg, bg, c);
			observe(TERM_OBS_CHAR, ansi.x, ansi.y, c);
			ansi.x += 1;
		}
	}
}

void stdio_picocalc_out_chars(const char *buf, int length) {
	while (length > 0) {
		if (ansi.state == AnsiNone) {
			if (*buf == 27 || *buf == '\x1b') ansi.state = AnsiEscape;
			else if (*buf == '\t') {
				out_char(*buf); out_char(*buf);
			}
			else out_char(*buf);
		} else if (ansi.state == AnsiEscape) {
			if (*buf == '[') ansi.state = AnsiBracket;
			else ansi.state = AnsiNone;
			ansi.stack_size = 0;
		} else if (ansi.state == AnsiBracket) {
			ansi.stack[ansi.stack_size] = 0;
			int a = 0, b = 0, semicolon = 0;
			//lcd_printf(0, 39 * 8, 0xffff, 0, "buf = '%c' stack = %s         ", *buf, ansi.stack);
			//keyboard_wait();
			switch (*buf) {
				case 'A': ansi.y -= atoi(ansi.stack); ansi.state = AnsiNone; break; // cursor up
				case 'B': ansi.y += atoi(ansi.stack); ansi.state = AnsiNone; break; // cursor down
				case 'C': ansi.x += atoi(ansi.stack); ansi.state = AnsiNone; break; // cursor right
				case 'D': ansi.x -= atoi(ansi.stack); ansi.state = AnsiNone; break; // cursor left
				case 'J': term_clear(); ansi.state = AnsiNone; break; // erase display
				case 'K':	term_erase_from_cursor(); ansi.state = AnsiNone; break;
				case 'm':
					if (ansi.stack_size == 0) {
						ansi.fg = palette[DEFAULT_FG];
						ansi.bg = palette[DEFAULT_BG];
						ansi.c_inverse = false;
						ansi.c_bold = false;
					} else {
						while (semicolon <= ansi.stack_size-1) {
							a = atoi(ansi.stack + semicolon);
							b = a % 10;
							if (a == 0) { // reset all
								ansi.fg = palette[DEFAULT_FG];
								ansi.bg = palette[DEFAULT_BG];
								ansi.c_inverse = false;
								ansi.c_bold = false;
							}
							else if (a == 7)  ansi.c_inverse = true;
							else if (a == 27) ansi.c_inverse = false;
							else if (a == 1)  ansi.c_bold = true;
							else if (a == 22) ansi.c_bold = false;
							else if (a == 38 || a == 48) { // 2;r;g;b, the 24 bit form
								int p[4], n = 0;
								while (n < 4) {
									while (semicolon < ansi.stack_size && ansi.stack[semicolon] != ';') semicolon++;
									if (semicolon >= ansi.stack_size) break;
									p[n++] = atoi(ansi.stack + ++semicolon);
								}
								if (n == 4 && p[0] == 2) {
									int r = p[1] & 0xff, g = p[2] & 0xff, bl = p[3] & 0xff;
									u16 c = RGB(r, g, bl);
									if (a == 38) ansi.fg = c;
									else ansi.bg = c;
								}
							}
							else if (a >= 30 && a <= 39) { // dim foreground
								if (b == 9) ansi.fg = palette[DEFAULT_FG];
								else if (b <= 7) ansi.fg = palette[b + ansi.c_bold * 8];
							}
							else if (a >= 40 && a <= 49) { // dim background
								if (b == 9) ansi.bg = palette[DEFAULT_BG];
								else if (b <= 7) ansi.bg = palette[b + ansi.c_bold * 8];
							}
							else if (a >= 90 && a <= 97) { // bright foreground
								ansi.fg = palette[b + 8];
							}
							else if (a >= 100 && a <= 107) { // bright background
								ansi.bg = palette[b + 8];
							}
							while (semicolon < ansi.stack_size-1 && ansi.stack[semicolon] != ';') semicolon++;
							semicolon++;
						}
					}
					ansi.state = AnsiNone;
					break;
				case 'H':
					if (ansi.stack_size == 0) {
						term_scroll(0);
						term_set_pos(0, 0);
					} else {
						a = atoi(ansi.stack);
						while (semicolon < ansi.stack_size && ansi.stack[semicolon] != ';') semicolon++;
						if (semicolon < ansi.stack_size - 1) b = atoi(ansi.stack + semicolon + 1);
						if (a > 0 && b > 0) {
							term_set_pos(b-1, a-1);
						}
					}
					ansi.state = AnsiNone;
					break;
				case 'l':
				case 'h':
					if (strncmp(ansi.stack, "?25", 3) == 0) {
						if (*buf == 'l') term_set_blinking_cursor(false);
						if (*buf == 'h') term_set_blinking_cursor(true);
					}
					ansi.state = AnsiNone;
					break;
				default:
					if (ansi.stack_size < ANSI_STACK_SIZE - 1) ansi.stack[ansi.stack_size++] = *buf;
					else ansi.state = AnsiNone;
			}
		}
		buf++;
		length--;
	}
}

static int stdio_picocalc_in_chars(char *buf, int length) {
	input_event_t event = keyboard_poll(false);
	if (event.state == KEY_STATE_PRESSED && event.code > 0) {
		if (event.modifiers & MOD_CONTROL && event.code >= 'a' && event.code <= 'z') {
			*buf = event.code - 'a' + 1;
		} else {
			*buf = event.code;
		}
		//stdio_picocalc_out_chars(buf, 1);
		return 1;
	}
	return 0;
}

stdio_driver_t stdio_picocalc = {
	.out_chars = stdio_picocalc_out_chars,
	.in_chars = stdio_picocalc_in_chars,
	.set_chars_available_callback = set_chars_available_callback,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
	.crlf_enabled = true,
#endif
};

static void term_erase_input(int size) {
	for (int i = 0; i < size + 1; i++)
		put_blank(len_col(i), len_row(i), 1, ansi.bg);
}

// TODO: this should be less specialized
static void term_draw_input(char* buffer, int size, int cursor) {
	if (ansi.y + (size + ansi.x) / font.term_width >= font.term_height) term_scroll(ansi.y + (size + ansi.x) / font.term_width - (font.term_height-1));
	for (int i = 0; i < size + 1; i++) {
		if (i < size) put_char(len_col(i), len_row(i), ansi.fg, ansi.bg, buffer[i]);
		else put_blank(len_col(i), len_row(i), 1, ansi.bg);
	}
}

static void history_save(history_t* history, int entry, char* text, int size) {
	if (entry >= 0 && entry < HISTORY_MAX) {
		if (history->buffer[entry] != NULL) free(history->buffer[entry]);
		history->buffer[entry] = strndup(text, size);
	}
}

int term_readline(const char* prompt, char* buffer, int max_length, history_t* history) {
	int cursor = 0;
	int size = 0;

	buffer[size] = '\0';

	if (history) {
		history->current = 0;
		if (history->buffer[0] != NULL && history->buffer[0][0] != '\0') memmove(history->buffer + 1, history->buffer, 31 * sizeof(char*));
		history->buffer[0] = strdup(buffer);
	}

	stdio_picocalc_out_chars(prompt, strlen(prompt));

	bool cursor_was_enabled = ansi.cursor_enabled;
	term_set_blinking_cursor(true);

	while (true) {
		input_event_t event = keyboard_wait();
		if (event.state == KEY_STATE_PRESSED) {
			if (event.code == 'c' && event.modifiers & MOD_CONTROL) {
				term_erase_input(size);
				size = cursor = 0;
			} else if (event.code == 'l' && event.modifiers & MOD_CONTROL) {
				term_clear();
				stdio_picocalc_out_chars(prompt, strlen(prompt));
			} else if (event.code == KEY_ENTER) {
				term_draw_input(buffer, size, -1);
				buffer[size] = '\0';
				ansi.x += size % font.term_width;
				ansi.y += size / font.term_width;
				stdio_picocalc_out_chars("\n", 1);
				if (history) history_save(history, 0, buffer, size);
				ansi.len = 0;
				term_set_blinking_cursor(cursor_was_enabled);
				return size;
			} else if (history && event.code == KEY_UP && history->current < 31 && history->buffer[history->current + 1] != NULL) {
				term_erase_input(size);
				history_save(history, history->current, buffer, size);
				history->current++;
				size = cursor = strlen(history->buffer[history->current]);
				memcpy(buffer, history->buffer[history->current], size);
			} else if (history && event.code == KEY_DOWN && history->current > 0) {
				term_erase_input(size);
				history_save(history, history->current, buffer, size);
				history->current--;
				size = cursor = strlen(history->buffer[history->current]);
				memcpy(buffer, history->buffer[history->current], size);
			} else if (event.code == KEY_LEFT) {
				if (event.modifiers & MOD_CONTROL) {
					while (cursor > 0 && buffer[cursor] != ' ') cursor--;
				} else if (cursor > 0) cursor -= 1;
			} else if (event.code == KEY_RIGHT) {
				if (event.modifiers & MOD_CONTROL) {
					while (cursor < size && buffer[cursor] != ' ') cursor++;
				} else if (cursor < size) cursor += 1;
			} else if (event.code == KEY_HOME) {
				cursor = 0;
			} else if (event.code == KEY_END) {
				cursor = size;
			} else if ((event.code == KEY_BACKSPACE && cursor > 0) || (event.code == KEY_DELETE && cursor < size)) {
				if (event.code == KEY_DELETE) cursor++;
				term_erase_input(size);
				cursor -= 1;
				size -= 1;
				memmove(buffer + cursor, buffer + cursor + 1, size - cursor);
			} else if (event.code >= 32 && event.code < 127) {
				if (size < max_length - 1) {
					if (cursor < size) {
						memmove(buffer + cursor + 1, buffer + cursor, size - cursor);
					}
					buffer[cursor] = event.code;
					size += 1;
					cursor += 1;
				}
			}
			if (cursor != ansi.len) {
				ansi.cursor_manual = true;
				erase_cursor();
			}
			term_draw_input(buffer, size, cursor);
			if (cursor != ansi.len) {
				ansi.len = cursor;
				draw_cursor();
			}
		}
	}
	term_set_blinking_cursor(cursor_was_enabled);
	return 0;
}
