/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Software update from a release site. See update.h.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "ff.h"

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"

#include "disks.h"
#include "net_vars.h"
#include "version.h"
#include "http.h"
#include "update.h"

/* the work area carved out of the machine's first 64 KB */
#define W_HTTP		0
#define W_URL		(W_HTTP + HTTP_WORK_SIZE)
#define W_END		(W_URL + HTTP_URL_MAX)

_Static_assert(W_END <= 65536, "the update work area does not fit bnk0");

static char *url;
static bool borrowed;

void update_begin(void)
{
	if (borrowed)
		return;
	http_init(bnk0 + W_HTTP);
	url = (char *) bnk0 + W_URL;
	borrowed = true;
}

void update_end(void)
{
	if (!borrowed)
		return;
	borrowed = false;
	init_memory();
}

bool update_card_version(char *buf, size_t len)
{
	FIL f;
	UINT br = 0;

	if (f_open(&f, Z80_DIR "VERSION.TXT", FA_READ) != FR_OK)
		return false;
	f_read(&f, buf, len - 1, &br);
	f_close(&f);
	while (br && isspace((unsigned char) buf[br - 1]))
		br--;
	buf[br] = '\0';
	return br > 0;
}

/* the update URL without a trailing slash, plus tail */
static bool build_url(const char *tail)
{
	size_t n = strlen(update_url);

	while (n && update_url[n - 1] == '/')
		n--;
	if (n + strlen(tail) >= HTTP_URL_MAX)
		return false;
	memcpy(url, update_url, n);
	strcpy(url + n, tail);
	return true;
}

bool update_latest(char *tag, size_t len, const char **err)
{
	static char msg[40];
	http_reply_t r;
	version_t v;
	const char *p, *e;
	size_t n;

	if (!build_url("/releases/latest")) {
		*err = "update URL too long";
		return false;
	}
	if (!http_probe(url, &r, err))
		return false;
	if (r.status / 100 != 3 || !r.location) {
		if (r.status == 404)
			*err = "no release found there";
		else {
			snprintf(msg, sizeof(msg), "unexpected answer %d",
				 r.status);
			*err = msg;
		}
		return false;
	}

	/* .../releases/tag/<tag>, relative or absolute */
	e = r.location + strcspn(r.location, "?#");
	while (e > r.location && e[-1] == '/')
		e--;
	for (p = e; p > r.location && p[-1] != '/'; p--)
		;
	n = e - p;
	if (!n || n >= len) {
		*err = "no tag in the redirect";
		return false;
	}
	memcpy(tag, p, n);
	tag[n] = '\0';
	if (!version_parse(tag, &v)) {
		*err = "the redirect does not name a version";
		return false;
	}
	return true;
}
