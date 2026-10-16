/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Extract a release's SD card archive, a gzipped ustar tar, onto the card.
 *
 * Every entry has to be under Z80/. Files are overwritten, except that a
 * disk image already on the card is kept under a backup name when it
 * differs from the release's copy, since a difference means the user wrote
 * to it. The first five characters of the name, a dash and a two digit
 * generation make a backup name the drive prompt can still load:
 * CPM22-01.DSK, XYBAS-01.DSK.
 */

#ifndef UNTAR_INC
#define UNTAR_INC

#include <stdint.h>

/* the caller lends this much: the inflate window and the buffers */
#define UNTAR_WORK_SIZE	49152

/* one line per entry on the console; NULL, or what went wrong */
extern const char *untar_gz(const char *archive, uint8_t *work);

#endif /* !UNTAR_INC */
