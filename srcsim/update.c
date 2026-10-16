/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Software update from a release site. See update.h.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pico/stdlib.h"

#include "ff.h"

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"
#include "simport.h"

#include "disks.h"
#include "net_vars.h"
#include "version.h"
#include "sha256.h"
#include "http.h"
#include "untar.h"
#include "update.h"

/* where uf2loader looks for firmware on this chip */
#if PICO_RP2040
#define APPS_DIR	"/pico1-apps"
#else
#define APPS_DIR	"/pico2-apps"
#endif

#define FW_PREFIX	"picocalc-z80-" USR_BOARD "-"
#define STR_(x)		#x
#define STR(x)		STR_(x)
#define TAG_FMT		"%." STR(TAG_LEN) "s"
#define NAME_FMT	"%." STR(NAME_LEN) "s"
#define SD_PREFIX	"picocalc-z80-sd-files-"
#define SUMS_FILE	"SHA256SUMS.txt"
#define PART_NAME	"picocalc-z80.part"

#define SUMS_SIZE	2048	/* the sums file is under 100 bytes a line */
#define FILE_BUF	4096
#define NAME_LEN	63	/* a release asset name */
#define NAMES_MAX	16	/* older firmware files offered for deletion */

/* the work area carved out of the machine's first 64 KB */
#define W_HTTP		0
#define W_URL		(W_HTTP + HTTP_WORK_SIZE)
#define W_SUMS		(W_URL + HTTP_URL_MAX)
#define W_FILE		(W_SUMS + SUMS_SIZE)
#define W_NAMES		(W_FILE + FILE_BUF)
#define W_END		(W_NAMES + NAMES_MAX * (NAME_LEN + 1))

_Static_assert(W_END <= sizeof(bnk0), "the update work area does not fit bnk0");
_Static_assert(UNTAR_WORK_SIZE <= sizeof(bnk1), "the untar work area does not fit bnk1");

static char *url;
static char *sums;
static bool borrowed;

typedef struct {
	char *buf;
	size_t cap, len;
	const char *err;
} mem_sink_t;

typedef struct {
	FIL f;
	uint8_t *buf;
	size_t fill;
	sha256_t sha;
	const char *err;
} file_sink_t;

void update_begin(void)
{
	if (borrowed)
		return;
	http_init(bnk0 + W_HTTP);
	url = (char *) bnk0 + W_URL;
	sums = (char *) bnk0 + W_SUMS;
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

/* ------------------------------------------------------------------ */
/* downloads                                                           */

static bool mem_sink(const uint8_t *data, int len, void *arg)
{
	mem_sink_t *m = arg;

	if (m->len + len >= m->cap) {
		m->err = "it is too big";
		return false;
	}
	memcpy(m->buf + m->len, data, len);
	m->len += len;
	return true;
}

static bool file_flush(file_sink_t *s)
{
	UINT bw;

	if (!s->fill)
		return true;
	if (f_write(&s->f, s->buf, s->fill, &bw) != FR_OK || bw != s->fill) {
		s->err = "could not write to the SD card";
		return false;
	}
	s->fill = 0;
	return true;
}

/* hashes as it goes, and writes in FILE_BUF pieces for the card's sake */
static bool file_sink(const uint8_t *data, int len, void *arg)
{
	file_sink_t *s = arg;
	size_t n;

	sha256_update(&s->sha, data, len);
	while (len > 0) {
		n = FILE_BUF - s->fill;
		if (n > (size_t) len)
			n = len;
		memcpy(s->buf + s->fill, data, n);
		s->fill += n;
		data += n;
		len -= n;
		if (s->fill == FILE_BUF && !file_flush(s))
			return false;
	}
	return true;
}

/* GET <update_url>/releases/download/<tag>/<name> */
static bool fetch(const char *tag, const char *name, http_sink_t sink,
		  void *arg, const char **err)
{
	char tail[NAME_LEN + 64];
	http_reply_t r;

	snprintf(tail, sizeof(tail), "/releases/download/%s/%s", tag, name);
	if (!build_url(tail)) {
		*err = "update URL too long";
		return false;
	}
	return http_get(url, sink, arg, &r, err);
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c = tolower((unsigned char) c);
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

/* the hash listed for name in the sums file; false when it is not there */
static bool sum_of(const char *name, uint8_t out[SHA256_LEN])
{
	const char *line = sums, *end, *p;
	size_t nl = strlen(name), len;
	int i, hi, lo;

	for (; *line; line = *end ? end + 1 : end) {
		end = line + strcspn(line, "\n");
		len = end - line;
		while (len && (line[len - 1] == '\r' || line[len - 1] == ' '))
			len--;
		/* <64 hex digits>  <name>, with a * before a binary name */
		if (len < 2 * SHA256_LEN + 2 + nl || line[2 * SHA256_LEN] != ' ')
			continue;
		p = line + 2 * SHA256_LEN + 1;
		while (*p == ' ' || *p == '*')
			p++;
		if ((size_t) (line + len - p) != nl || strncmp(p, name, nl))
			continue;
		for (i = 0; i < SHA256_LEN; i++) {
			hi = hexval(line[2 * i]);
			lo = hexval(line[2 * i + 1]);
			if (hi < 0 || lo < 0)
				return false;
			out[i] = (uint8_t) (hi << 4 | lo);
		}
		return true;
	}
	return false;
}

static bool fetch_sums(const char *tag)
{
	mem_sink_t m = { sums, SUMS_SIZE, 0, NULL };
	const char *err;

	printf("Fetching " SUMS_FILE "... ");
	if (!fetch(tag, SUMS_FILE, mem_sink, &m, &err)) {
		printf("failed: %s\n", m.err ? m.err : err);
		return false;
	}
	sums[m.len] = '\0';
	puts("ok");
	return true;
}

/* download to part, which becomes path only once the hash matches */
static bool fetch_file(const char *tag, const char *name, const char *part,
		       const char *path, const uint8_t want[SHA256_LEN])
{
	file_sink_t fs;
	uint8_t got[SHA256_LEN];
	const char *err;
	bool ok;

	fs.buf = bnk0 + W_FILE;
	fs.fill = 0;
	fs.err = NULL;
	sha256_init(&fs.sha);
	if (f_open(&fs.f, part, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		printf("Cannot create %s\n", part);
		return false;
	}
	printf("Fetching %s... ", name);
	ok = fetch(tag, name, file_sink, &fs, &err) && file_flush(&fs);
	f_close(&fs.f);
	if (!ok) {
		printf("failed: %s\n", fs.err ? fs.err : err);
		f_unlink(part);
		return false;
	}
	sha256_final(&fs.sha, got);
	if (memcmp(got, want, SHA256_LEN)) {
		puts("failed: the checksum does not match");
		f_unlink(part);
		return false;
	}
	puts("ok, checksum verified");

	/* FatFs will not rename over an existing file */
	f_unlink(path);
	if (f_rename(part, path) != FR_OK) {
		printf("Cannot rename %s to %s\n", part, path);
		return false;
	}
	return true;
}

/* this board's firmware files in the apps directory, except keep */
static int list_firmware(const char *keep, char *names)
{
	DIR dp;
	FILINFO fno;
	int n = 0;

	if (f_findfirst(&dp, &fno, APPS_DIR, FW_PREFIX "*.uf2") != FR_OK)
		return 0;
	while (fno.fname[0] && n < NAMES_MAX) {
		if (strcasecmp(fno.fname, keep) && strlen(fno.fname) <= NAME_LEN)
			strcpy(names + n++ * (NAME_LEN + 1), fno.fname);
		if (f_findnext(&dp, &fno) != FR_OK)
			break;
	}
	f_closedir(&dp);
	return n;
}

/* the default keeps them, so a rollback is always one answer away */
static void prune_firmware(const char *keep)
{
	char *names = (char *) bnk0 + W_NAMES, *name;
	char running[NAME_LEN + 1], path[sizeof(APPS_DIR) + NAME_LEN + 1];
	char s[2];
	int n = list_firmware(keep, names), i;

	snprintf(running, sizeof(running), FW_PREFIX TAG_FMT ".uf2", USR_REL);
	for (i = 0; i < n; i++) {
		name = names + i * (NAME_LEN + 1);
		printf("Delete %s%s (y/N)? ", name,
		       strcasecmp(name, running) ? "" :
		       ", the version running now");
		get_cmdline(s, 2);
		putchar('\n');
		if (tolower((unsigned char) s[0]) != 'y')
			continue;
		snprintf(path, sizeof(path), APPS_DIR "/" NAME_FMT, name);
		if (f_unlink(path) == FR_OK)
			printf("%s deleted\n", name);
		else
			printf("Could not delete %s\n", name);
	}
}

/* the firmware goes next to the loader; path gets where it landed */
static bool firmware(const char *tag, char *path, size_t len)
{
	char name[NAME_LEN + 1];
	uint8_t want[SHA256_LEN];
	FRESULT fr;

	snprintf(name, sizeof(name), FW_PREFIX TAG_FMT ".uf2", tag);
	if (!sum_of(name, want)) {
		printf("The release has no %s\n", name);
		return false;
	}
	fr = f_mkdir(APPS_DIR);
	if (fr != FR_OK && fr != FR_EXIST) {
		puts("Cannot create " APPS_DIR);
		return false;
	}
	snprintf(path, len, APPS_DIR "/%s", name);
	if (!fetch_file(tag, name, APPS_DIR "/" PART_NAME, path, want))
		return false;
	prune_firmware(name);
	return true;
}

/* the archive is staged in the transfer directory so that it is checked
   whole before anything under Z80 is touched; it stays there on failure */
static bool sd_files(const char *tag)
{
	char name[NAME_LEN + 1], path[sizeof(XFER_DIR) + NAME_LEN];
	uint8_t want[SHA256_LEN];
	const char *err;
	FRESULT fr;

	snprintf(name, sizeof(name), SD_PREFIX TAG_FMT ".tar.gz", tag);
	if (!sum_of(name, want)) {
		printf("The release has no %s\n", name);
		return false;
	}
	fr = f_mkdir(XFER_DIR);
	if (fr != FR_OK && fr != FR_EXIST) {
		puts("Cannot create " XFER_DIR);
		return false;
	}
	snprintf(path, sizeof(path), XFER_DIR "%s", name);
	if (!fetch_file(tag, name, XFER_DIR PART_NAME, path, want))
		return false;
	puts("Extracting...");
	err = untar_gz(path, bnk1);
	if (err) {
		printf("Extraction failed: %s\n"
		       "The archive is kept as %s\n", err, path);
		return false;
	}
	f_unlink(path);
	puts("SD card files updated");
	return true;
}

bool update_run(const char *tag)
{
	char fw[sizeof(APPS_DIR) + NAME_LEN + 1];
	bool ok;

	if (!sd_mounted) {
		puts("No SD card\n");
		return false;
	}
	update_begin();
	if (!fetch_sums(tag) || !firmware(tag, fw, sizeof(fw))) {
		putchar('\n');
		return false;
	}
	ok = sd_files(tag);
	/* the firmware is good whatever happened to the card files */
	printf("\nFirmware saved as %s\n"
	       "Power on holding the Up Arrow and choose it in the loader.\n\n",
	       fw);
	return ok;
}
