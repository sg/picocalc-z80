/*
 * Disk images for the host build. Same geometry rules as srcsim/disks.c,
 * against stdio instead of FatFs, so a BIOS change can be booted and tried
 * on a PC before it goes anywhere near a card.
 */

#include <stdio.h>
#include <string.h>

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"
#include "sd-fdc.h"
#include "hostdisks.h"

char disks[NUMDRV][256];
static FILE *fp[NUMDRV];

static unsigned char dsk_buf[SEC_SZ];

static int drive_slot(int drive)
{
	if ((drive >= 0) && (drive < NUMDISK))
		return drive;
	if (drive == HD_DRIVE)
		return HD_SLOT;
	return -1;
}

static int slot_tracks(int slot) { return (slot == HD_SLOT) ? HD_TRK : TRK; }
static int slot_sectors(int slot) { return (slot == HD_SLOT) ? HD_SPT : SPT; }

static BYTE prep_io(int drive, int track, int sector, WORD addr, int *slotp)
{
	long pos;
	int slot, spt;

	slot = drive_slot(drive);
	if (slot < 0)
		return FDC_STAT_DISK;
	spt = slot_sectors(slot);

	if (track >= slot_tracks(slot))
		return FDC_STAT_TRACK;
	if ((sector < 1) || (sector > spt))
		return FDC_STAT_SEC;
	if (addr > 0xff7f)
		return FDC_STAT_DMAADR;
	if (!disks[slot][0])
		return FDC_STAT_NODISK;

	if (fp[slot] == NULL) {
		fp[slot] = fopen(disks[slot], "r+b");
		if (fp[slot] == NULL)
			return FDC_STAT_NODISK;
	}

	pos = ((long) track * spt + sector - 1) * SEC_SZ;
	if (fseek(fp[slot], pos, SEEK_SET) != 0)
		return FDC_STAT_SEEK;
	*slotp = slot;
	return FDC_STAT_OK;
}

BYTE read_sec(int drive, int track, int sector, WORD addr)
{
	BYTE stat;
	int slot = 0, i;

	if ((stat = prep_io(drive, track, sector, addr, &slot)) != FDC_STAT_OK)
		return stat;
	if (fread(dsk_buf, 1, SEC_SZ, fp[slot]) != SEC_SZ)
		return FDC_STAT_READ;
	for (i = 0; i < SEC_SZ; i++)
		dma_write(addr + i, dsk_buf[i]);
	return FDC_STAT_OK;
}

BYTE write_sec(int drive, int track, int sector, WORD addr)
{
	BYTE stat;
	int slot = 0, i;

	if ((stat = prep_io(drive, track, sector, addr, &slot)) != FDC_STAT_OK)
		return stat;
	for (i = 0; i < SEC_SZ; i++)
		dsk_buf[i] = dma_read(addr + i);
	if (fwrite(dsk_buf, 1, SEC_SZ, fp[slot]) != SEC_SZ)
		return FDC_STAT_WRITE;
	fflush(fp[slot]);
	return FDC_STAT_OK;
}

void get_fdccmd(BYTE *cmd, WORD addr)
{
	int i;

	for (i = 0; i < 4; i++)
		cmd[i] = dma_read(addr + i);
}
