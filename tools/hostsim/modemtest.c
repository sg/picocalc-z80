/*
 * A dumb terminal straight onto srcsim/modem.c, without the Z80: for trying
 * AT commands, dialling and telnet against real hosts from a PC.
 *
 *   modemtest            interactive; Enter sends CR
 *   modemtest < script   scripted; the session ends when input does
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "modem.h"

static struct termios saved;

static void restore(void)
{
	tcsetattr(0, TCSANOW, &saved);
}

int main(void)
{
	struct termios t;
	unsigned char c;
	ssize_t n;
	int idle = 0, eof = 0;

	if (tcgetattr(0, &saved) == 0) {
		atexit(restore);
		t = saved;
		t.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(0, TCSANOW, &t);
	}
	fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);

	modem_init();
	for (;;) {
		if (!eof && modem_tx_ready()) {
			n = read(0, &c, 1);
			if (n == 1) {
				modem_putc(c == '\n' ? '\r' : c);
				idle = 0;
			} else if (n == 0)
				eof = 1;
		}
		while (modem_rx_ready()) {
			putchar(modem_getc());
			idle = 0;
		}
		fflush(stdout);
		/* after scripted input, linger long enough to see the replies */
		if (eof && ++idle > 3000)
			break;
		usleep(1000);
	}
	putchar('\n');
	return 0;
}
