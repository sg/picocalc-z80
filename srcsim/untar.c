/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Extract a gzipped ustar archive. See untar.h.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "ff.h"
#include "uzlib.h"

#include "disks.h"
#include "untar.h"

#define IN_SIZE		4096	/* compressed bytes read from the card at once */
#define OUT_SIZE	4096
#define WINDOW_SIZE	32768	/* deflate's longest back reference */
#define BLOCK		512
#define PATH_LEN	260	/* prefix, slash, name, and the leading slash */
#define TOP_DIR		"Z80"
#define IMAGE_TMP	DISKS_DIR "NEW.TMP"
#define BACKUPS		99

typedef struct {
	struct uzlib_uncomp d;
	FIL f;
	uint8_t in[IN_SIZE];
	bool eof;
	const char *err;
} gz_t;

typedef struct {
	char name[100], mode[8], uid[8], gid[8], size[12], mtime[12];
	char chksum[8], type, linkname[100], magic[6], version[2];
	char uname[32], gname[32], devmajor[8], devminor[8], prefix[155];
	char pad[12];
} tar_hdr_t;

_Static_assert(sizeof(tar_hdr_t) == BLOCK, "ustar header");
#define MSG_LEN		(PATH_LEN + 40)

_Static_assert(8 + sizeof(gz_t) + WINDOW_SIZE + 2 * OUT_SIZE + MSG_LEN <=
	       UNTAR_WORK_SIZE, "the untar work area");

static char *msg;	/* the failure text, in the lent work area */

/* ------------------------------------------------------------------ */
/* gzip stream                                                         */

static int gz_source(struct uzlib_uncomp *d)
{
	gz_t *gz = (gz_t *) ((char *) d - offsetof(gz_t, d));
	UINT n = 0;

	if (f_read(&gz->f, gz->in, IN_SIZE, &n) != FR_OK || !n)
		return -1;
	d->source = gz->in + 1;
	d->source_limit = gz->in + n;
	return gz->in[0];
}

static bool gz_open(gz_t *gz, const char *path, uint8_t *window)
{
	gz->eof = false;
	gz->err = NULL;
	if (f_open(&gz->f, path, FA_READ) != FR_OK) {
		gz->err = "cannot open the archive";
		return false;
	}
	uzlib_uncompress_init(&gz->d, window, WINDOW_SIZE);
	/* nothing buffered yet, so the first byte comes from the callback */
	gz->d.source = gz->d.source_limit = NULL;
	gz->d.source_read_cb = gz_source;
	if (uzlib_gzip_parse_header(&gz->d) != TINF_OK) {
		f_close(&gz->f);
		gz->err = "not a gzip file";
		return false;
	}
	return true;
}

/* n inflated bytes, fewer only at the end of the stream; -1 on damage */
static int gz_read(gz_t *gz, uint8_t *buf, int n)
{
	int r, got = 0;

	while (got < n && !gz->eof) {
		gz->d.dest_start = gz->d.dest = buf + got;
		gz->d.dest_limit = buf + n;
		r = uzlib_uncompress_chksum(&gz->d);
		if (r == TINF_DONE)
			gz->eof = true;
		else if (r != TINF_OK) {
			gz->err = (r == TINF_CHKSUM_ERROR) ?
				  "the archive's checksum does not match" :
				  "the archive is corrupt";
			return -1;
		}
		got = gz->d.dest - buf;
	}
	return got;
}

static bool gz_exact(gz_t *gz, uint8_t *buf, int n)
{
	int got = gz_read(gz, buf, n);

	if (got == n)
		return true;
	if (got >= 0)
		gz->err = "the archive ends early";
	return false;
}

/* ------------------------------------------------------------------ */
/* ustar                                                               */

static long octal(const char *s, int n)
{
	long v = 0;

	while (n-- && *s) {
		if (*s >= '0' && *s <= '7')
			v = v * 8 + (*s - '0');
		else if (*s != ' ')
			break;
		s++;
	}
	return v;
}

static bool header_ok(const uint8_t *h)
{
	unsigned sum = 0;
	int i;

	/* the sum is taken with the checksum field itself read as spaces */
	for (i = 0; i < BLOCK; i++)
		sum += (i >= 148 && i < 156) ? ' ' : h[i];
	return sum == (unsigned) octal((const char *) h + 148, 8);
}

static bool zero_block(const uint8_t *h)
{
	int i;

	for (i = 0; i < BLOCK; i++)
		if (h[i])
			return false;
	return true;
}

/* /prefix/name, cleaned up and confined to the Z80 directory */
static bool entry_path(const tar_hdr_t *h, char *path)
{
	char raw[sizeof(h->prefix) + 1 + sizeof(h->name) + 1];
	size_t pl = strnlen(h->prefix, sizeof(h->prefix));
	size_t nl = strnlen(h->name, sizeof(h->name)), len;
	const char *p, *end;
	char *q = path + 1;

	memcpy(raw, h->prefix, pl);
	if (pl)
		raw[pl++] = '/';
	memcpy(raw + pl, h->name, nl);
	raw[pl + nl] = '\0';

	/* rebuilt from its components, without empty ones, dots or climbs */
	path[0] = '/';
	for (p = raw; *p; p = *end ? end + 1 : end) {
		end = p + strcspn(p, "/");
		len = end - p;
		if (!len || (len == 1 && p[0] == '.'))
			continue;
		if (len == 2 && p[0] == '.' && p[1] == '.')
			return false;
		if (q > path + 1)
			*q++ = '/';
		memcpy(q, p, len);
		q += len;
	}
	*q = '\0';
	len = strcspn(path + 1, "/");
	return len == strlen(TOP_DIR) && !strncasecmp(path + 1, TOP_DIR, len);
}

static bool make_dir(const char *path)
{
	FRESULT fr = f_mkdir(path);

	return fr == FR_OK || fr == FR_EXIST;
}

static bool make_parents(char *path)
{
	char *p;

	for (p = path + 1; (p = strchr(p, '/')); p++) {
		*p = '\0';
		if (!make_dir(path)) {
			*p = '/';
			return false;
		}
		*p = '/';
	}
	return true;
}

static bool is_image(const char *path)
{
	const char *base = strrchr(path, '/') + 1, *ext = strrchr(base, '.');

	return !strncasecmp(path, DISKS_DIR, sizeof(DISKS_DIR) - 1) &&
	       base == path + sizeof(DISKS_DIR) - 1 && ext &&
	       (!strcasecmp(ext, ".DSK") || !strcasecmp(ext, ".HDD"));
}

static bool is_config(const char *path)
{
	return !strcasecmp(path, Z80_DIR "PCALC.DAT");
}

/* CPM22.DSK becomes CPM22-01.DSK, XYBASIC.DSK XYBAS-01.DSK, with the
   generation counting up past the backups already there */
static bool backup_name(const char *path, char *bak, size_t len)
{
	const char *base = strrchr(path, '/') + 1, *ext = strrchr(base, '.');
	int dir = base - path, stem = ext ? ext - base : (int) strlen(base);
	FILINFO fi;
	int g;

	if (stem > 5)
		stem = 5;
	for (g = 1; g <= BACKUPS; g++) {
		snprintf(bak, len, "%.*s%.*s-%02d%s", dir, path, stem, base, g,
			 ext ? ext : "");
		if (f_stat(bak, &fi) == FR_NO_FILE)
			return true;
	}
	return false;
}

/* copy size bytes and the padding after them into f; when old is given
   compare with it as well, so an image the user never wrote to is known
   without a second pass */
static bool copy_entry(gz_t *gz, FIL *f, FIL *old, long size, uint8_t *buf,
		       uint8_t *cmp, bool *differs, const char **err)
{
	UINT bw, br;
	int n;

	while (size > 0) {
		n = (size < OUT_SIZE) ? (int) size : OUT_SIZE;
		if (!gz_exact(gz, buf, n)) {
			*err = gz->err;
			return false;
		}
		if (f_write(f, buf, n, &bw) != FR_OK || (int) bw != n) {
			*err = "could not write to the SD card";
			return false;
		}
		if (old && !*differs) {
			if (f_read(old, cmp, n, &br) != FR_OK || (int) br != n ||
			    memcmp(buf, cmp, n))
				*differs = true;
		}
		size -= n;
	}
	return true;
}

static bool skip_entry(gz_t *gz, long size, uint8_t *buf, const char **err)
{
	int n;

	size += (BLOCK - (size & (BLOCK - 1))) & (BLOCK - 1);
	while (size > 0) {
		n = (size < OUT_SIZE) ? (int) size : OUT_SIZE;
		if (!gz_exact(gz, buf, n)) {
			*err = gz->err;
			return false;
		}
		size -= n;
	}
	return true;
}

static bool extract_file(gz_t *gz, char *path, long size, uint8_t *buf,
			 uint8_t *cmp, const char **err)
{
	FIL f, old;
	FILINFO fi;
	bool image, have_old = false, differs = false, ok;
	char bak[PATH_LEN];
	const char *dest;
	long pad;

	if (!make_parents(path)) {
		snprintf(msg, MSG_LEN, "cannot create the directory for %s",
			 path + 1);
		*err = msg;
		return false;
	}
	/* an image already on the card is only replaced once it is known
	   to differ, and then it survives under the backup name */
	image = is_image(path) && f_stat(path, &fi) == FR_OK;
	dest = image ? IMAGE_TMP : path;
	if (image) {
		have_old = f_open(&old, path, FA_READ) == FR_OK;
		differs = !have_old || (long) f_size(&old) != size;
	}
	if (f_open(&f, dest, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		if (have_old)
			f_close(&old);
		snprintf(msg, MSG_LEN, "cannot create %s", dest + 1);
		*err = msg;
		return false;
	}
	ok = copy_entry(gz, &f, (have_old && !differs) ? &old : NULL, size,
			buf, cmp, &differs, err);
	f_close(&f);
	if (have_old)
		f_close(&old);
	if (ok) {
		pad = (BLOCK - (size & (BLOCK - 1))) & (BLOCK - 1);
		ok = !pad || gz_exact(gz, buf, (int) pad);
		if (!ok)
			*err = gz->err;
	}
	if (!ok) {
		f_unlink(dest);
		return false;
	}

	if (!image) {
		printf("  %s\n", path + 1);
		return true;
	}
	if (!differs) {
		f_unlink(IMAGE_TMP);
		printf("  %s unchanged\n", path + 1);
		return true;
	}
	if (!backup_name(path, bak, sizeof(bak))) {
		f_unlink(IMAGE_TMP);
		snprintf(msg, MSG_LEN, "no backup name left for %s",
			 path + 1);
		*err = msg;
		return false;
	}
	if (f_rename(path, bak) != FR_OK) {
		f_unlink(IMAGE_TMP);
		snprintf(msg, MSG_LEN, "cannot rename %s", path + 1);
		*err = msg;
		return false;
	}
	if (f_rename(IMAGE_TMP, path) != FR_OK) {
		snprintf(msg, MSG_LEN, "cannot rename " IMAGE_TMP " to %s",
			 path + 1);
		*err = msg;
		return false;
	}
	printf("  %s differs, saved as %s\n", path + 1, strrchr(bak, '/') + 1);
	return true;
}

const char *untar_gz(const char *archive, uint8_t *work)
{
	gz_t *gz;
	uint8_t *window, *buf, *cmp, hdr[BLOCK];
	const tar_hdr_t *h = (const tar_hdr_t *) hdr;
	const char *err = NULL;
	char path[PATH_LEN];
	long size;
	int n;

	/* the inflate state holds pointers, so it wants alignment */
	gz = (gz_t *) (((uintptr_t) work + 7) & ~(uintptr_t) 7);
	window = (uint8_t *) (gz + 1);
	buf = window + WINDOW_SIZE;
	cmp = buf + OUT_SIZE;
	msg = (char *) cmp + OUT_SIZE;

	uzlib_init();
	if (!gz_open(gz, archive, window))
		return gz->err;
	for (;;) {
		n = gz_read(gz, hdr, BLOCK);
		if (n < 0) {
			err = gz->err;
			break;
		}
		if (n == 0 || (n == BLOCK && zero_block(hdr)))
			break;
		if (n < BLOCK) {
			err = "the archive ends early";
			break;
		}
		if (!header_ok(hdr)) {
			err = "a damaged entry in the archive";
			break;
		}
		size = octal(h->size, sizeof(h->size));
		/* links, pax and GNU extension records have nothing to put
		   on a FAT card, whatever they are called */
		if (h->type != '5' && h->type != '0' && h->type != '\0') {
			if (!skip_entry(gz, size, buf, &err))
				break;
			continue;
		}
		if (!entry_path(h, path)) {
			err = "an entry outside the Z80 directory";
			break;
		}
		if (h->type == '5') {
			if (!make_dir(path)) {
				err = "cannot create a directory";
				break;
			}
			printf("  %s/\n", path + 1);
		} else if (is_config(path)) {
			printf("  %s skipped\n", path + 1);
			if (!skip_entry(gz, size, buf, &err))
				break;
		} else if (!extract_file(gz, path, size, buf, cmp, &err))
			break;
	}
	f_close(&gz->f);
	return err;
}
