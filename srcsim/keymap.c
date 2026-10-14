/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * PicoCalc keyboard to terminal byte stream. See keymap.h.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"

#include "keyboard.h"
#include "term.h"
#include "keymap.h"
#include "scrollback.h"

int keymap_arrows = KEYMAP_ARROWS_ANSI;

#define PENDING_MAX 8

static char pending[PENDING_MAX];
static int pending_len;
static int pending_pos;

static void (*chars_available_callback)(void *);
static void *chars_available_param;

static int put(char *out, const char *s)
{
	int n = (int) strlen(s);

	memcpy(out, s, n);
	return n;
}

static int put1(char *out, int c)
{
	out[0] = (char) c;
	return 1;
}

/*
 * Returns the number of bytes the key produces, zero for keys that send
 * nothing at all (the modifiers themselves, CapsLock, BREAK).
 */
static int translate(const input_event_t *ev, char *out)
{
	bool ws = (keymap_arrows == KEYMAP_ARROWS_WORDSTAR);
	int c = ev->code;

	if (ev->modifiers & MOD_CONTROL) {
		if ((c >= 'a') && (c <= 'z'))
			return put1(out, c - 'a' + 1);
		if ((c >= 'A') && (c <= 'Z'))
			return put1(out, c - 'A' + 1);
		switch (c) {
		case '[':  return put1(out, 0x1b);
		case '\\': return put1(out, 0x1c);
		case ']':  return put1(out, 0x1d);
		case '^':  return put1(out, 0x1e);
		case '-':
		case '_':  return put1(out, 0x1f);
		case '@':
		case ' ':  return put1(out, 0x00);
		default:   break;
		}
	}

	switch (c) {
	case KEY_ESC:		return put1(out, 0x1b);
	case KEY_ENTER:		return put1(out, '\r');
	case KEY_BACKSPACE:	return put1(out, 0x08);
	case KEY_TAB:		return put1(out, 0x09);

	case KEY_UP:	 return ws ? put1(out, 0x05) : put(out, "\x1b[A");
	case KEY_DOWN:	 return ws ? put1(out, 0x18) : put(out, "\x1b[B");
	case KEY_RIGHT:	 return ws ? put1(out, 0x04) : put(out, "\x1b[C");
	case KEY_LEFT:	 return ws ? put1(out, 0x13) : put(out, "\x1b[D");

	case KEY_HOME:	 return ws ? put(out, "\x11S") : put(out, "\x1b[H");
	case KEY_END:	 return ws ? put(out, "\x11" "D") : put(out, "\x1b[F");
	case KEY_PAGEUP:   return ws ? put1(out, 0x12) : put(out, "\x1b[5~");
	case KEY_PAGEDOWN: return ws ? put1(out, 0x03) : put(out, "\x1b[6~");

	case KEY_DELETE: return ws ? put1(out, 0x07) : put1(out, 0x7f);
	case KEY_INSERT: return ws ? put1(out, 0x16) : put(out, "\x1b[2~");

	case KEY_F1: return put(out, "\x1bOP");
	case KEY_F2: return put(out, "\x1bOQ");
	case KEY_F3: return put(out, "\x1bOR");
	case KEY_F4: return put(out, "\x1bOS");
	case KEY_F5: return put(out, "\x1b[15~");
	case KEY_F6: return put(out, "\x1b[17~");
	case KEY_F7: return put(out, "\x1b[18~");
	case KEY_F8: return put(out, "\x1b[19~");
	case KEY_F9: return put(out, "\x1b[20~");
	case KEY_F10: return put(out, "\x1b[21~");

	default: break;
	}

	if ((c >= 32) && (c < 127)) {
		/* Alt is the usual way to reach a meta prefix on a keyboard
		   this size, and CP/M software that wants one wants ESC */
		if (ev->modifiers & MOD_ALT) {
			out[0] = 0x1b;
			out[1] = (char) c;
			return 2;
		}
		return put1(out, c);
	}

	return 0;
}

static int keymap_in_chars(char *buf, int length)
{
	input_event_t ev;

	if (length < 1)
		return PICO_ERROR_NO_DATA;

	if (pending_pos >= pending_len) {
		ev = keyboard_poll(false);
		if (ev.code <= 0)
			return PICO_ERROR_NO_DATA;
		/*
		 * The navigation keys belong to the screen, not the machine,
		 * and unlike anything the machine sees they act on a held
		 * key as well, so scrolling back repeats.
		 */
		if (((ev.state == KEY_STATE_PRESSED)
		     || (ev.state == KEY_STATE_HOLD))
		    && scrollback_key(&ev))
			return PICO_ERROR_NO_DATA;
		if (ev.state != KEY_STATE_PRESSED)
			return PICO_ERROR_NO_DATA;
		pending_len = translate(&ev, pending);
		pending_pos = 0;
		if (pending_len < 1)
			return PICO_ERROR_NO_DATA;
	}

	*buf = pending[pending_pos++];
	return 1;
}

static void keymap_chars_available(void)
{
	if (chars_available_callback)
		chars_available_callback(chars_available_param);
}

static void keymap_set_chars_available_callback(void (*fn)(void *), void *param)
{
	chars_available_callback = fn;
	chars_available_param = param;
}

static stdio_driver_t stdio_keymap = {
	.out_chars = stdio_picocalc_out_chars,
	.in_chars = keymap_in_chars,
	.set_chars_available_callback = keymap_set_chars_available_callback,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
	.crlf_enabled = true,
#endif
};

void keymap_init(void)
{
	keyboard_set_key_available_callback(keymap_chars_available);
	stdio_set_driver_enabled(&stdio_keymap, true);
}
