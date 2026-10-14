/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk & Thomas Eberhardt
 *
 * This module implements the disks drives and low level
 * access functions for MicroSD, needed by the FDC.
 *
 * Ported to use pico_fatfs/SPI
 * Copyright (C) 2026 Steve Gibson
 *
 * History:
 * 27-MAY-2024 implemented load file
 * 28-MAY-2024 implemented sector I/O to disk images
 * 03-JUN-2024 added directory list for code files and disk images
 * 29-JUN-2024 split of from memsim.c and picosim.c
 * 15-AUG-2026 ported from no-OS-FatFS/SDIO to pico_fatfs/SPI for the PicoCalc
 * 15-AUG-2026 keep disk images open across sector accesses
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"

#include "ff.h"
#include "tf_card.h"

#include "term.h"

#include "sd-fdc.h"
#include "disks.h"

/* PicoCalc MicroSD socket, wired to SPI0 */
#define SD_MISO		16
#define SD_CS		17
#define SD_SCK		18
#define SD_MOSI		19
#define SD_DETECT	22

FIL sd_file;	/* at any time we have only one file open */
FRESULT sd_res;	/* result code from FatFS */
char disks[NUMDRV][DISKLEN+1]; /* path name per drive, HD_SLOT last */

bool sd_mounted;	/* the card is removable, so this can go false at runtime */
volatile int disk_act = -1; /* drive last touched, bit 7 set for a write */

static FATFS fs; /* FatFs on MicroSD */

/*
 * The FDC transfers one 128 byte sector at a time. Reopening the image for
 * each of those means walking the disks directory from the root every time,
 * several sector reads of its own, so the images are held open instead. A
 * failed access closes the drive again, and the next one reopens it.
 */
static FIL dsk_fil[NUMDRV];
static bool dsk_open[NUMDRV];

/* the hard disk is drive 8 to the BIOS but lives at the end of the tables */
static int drive_slot(int drive)
{
	if ((drive >= 0) && (drive < NUMDISK))
		return drive;
	if (drive == HD_DRIVE)
		return HD_SLOT;
	return -1;
}

static int slot_tracks(int slot)
{
	return (slot == HD_SLOT) ? HD_TRK : TRK;
}

static int slot_sectors(int slot)
{
	return (slot == HD_SLOT) ? HD_SPT : SPT;
}

/* buffer for disk/memory transfers */
static unsigned char __aligned(4) dsk_buf[SEC_SZ];

static const char *fres_str(FRESULT res)
{
	static const char *const s[] = {
		"OK", "disk error", "internal error", "not ready",
		"no file", "no path", "invalid name", "denied", "exists",
		"invalid object", "write protected", "invalid drive",
		"not enabled", "no filesystem", "mkfs aborted", "timeout",
		"locked", "not enough core", "too many open files",
		"invalid parameter"
	};

	if ((unsigned) res >= sizeof(s) / sizeof(s[0]))
		return "unknown error";
	return s[res];
}

static void close_disk(int slot)
{
	if (dsk_open[slot]) {
		f_close(&dsk_fil[slot]);
		dsk_open[slot] = false;
	}
}

void init_disks(void)
{
	pico_fatfs_spi_config_t config = {
		.spi_inst = spi0,
		.clk_slow = CLK_SLOW_DEFAULT,
		.clk_fast = CLK_FAST_DEFAULT,
		.pin_miso = SD_MISO,
		.pin_cs   = SD_CS,
		.pin_sck  = SD_SCK,
		.pin_mosi = SD_MOSI,
		.pullup   = true,
	};

	gpio_init(SD_DETECT);
	gpio_set_dir(SD_DETECT, GPIO_IN);
	gpio_pull_up(SD_DETECT);

	if (!pico_fatfs_set_config(&config))
		puts("SD: hardware SPI unavailable, fell back to PIO SPI");

	/* a missing card must not be fatal, the config dialog still works */
	sd_res = f_mount(&fs, "", 1);
	sd_mounted = (sd_res == FR_OK);
	if (!sd_mounted) {
		if (gpio_get(SD_DETECT))
			puts("SD: no card inserted, no disks available");
		else
			printf("SD: mount failed (%s), no disks available\n",
			       fres_str(sd_res));
	}
}

void exit_disks(void)
{
	int i;

	for (i = 0; i < NUMDRV; i++)
		close_disk(i);

	/* unmount SD card */
	if (sd_mounted) {
		f_unmount("");
		sd_mounted = false;
	}
}

/*
 * list files with pattern 'ext' in directory 'dir'
 */
void list_files(const char *dir, const char *ext)
{
	DIR dp;
	FILINFO fno;
	FRESULT res;
	int cols = term_get_width() / (FNLEN + 8) - 1;
	register int i = 0;

	/* convert to string */
	#define STR_(X) #X
	/* this makes sure the argument is expanded before converting to string */
	#define STR(X) STR_(X)

	res = f_findfirst(&dp, &fno, dir, ext);
	if (res == FR_OK) {
		while (1) {
			for (char *q = fno.fname; *q; q++)
				*q = toupper((unsigned char) *q);
			printf("%-" STR(FNLEN) "s\t", fno.fname);
			if (i > cols) {
				putchar('\n');
				i = 0;
			}
			res = f_findnext(&dp, &fno);
			if (!strlen(fno.fname)) {
				if (i > 0)
					putchar('\n');
				break;
			}
		}
	}
}

/*
 * load a file 'name' into memory
 * returns true on success, false on error
 */
bool load_file(const char *name)
{
	int i = 0;
	bool res;
	register unsigned int j;
	unsigned int br;
	char SFN[DISKLEN+1];

	strcpy(SFN, CODE_DIR);
	strcat(SFN, name);
	strcat(SFN, ".BIN");

	/* try to open file */
	sd_res = f_open(&sd_file, SFN, FA_READ);
	if (sd_res != FR_OK) {
		puts("File not found");
		return false;
	}

	/* read file into memory */
	while ((sd_res = f_read(&sd_file, dsk_buf, SEC_SZ, &br)) == FR_OK) {
		for (j = 0; j < br; j++)
			dma_write(i + j, dsk_buf[j]);
		if (br < SEC_SZ)	/* last record reached */
			break;
		i += SEC_SZ;
	}
	if (sd_res != FR_OK) {
		printf("f_read error: %s (%d)\n", fres_str(sd_res), sd_res);
		res = false;
	} else {
		printf("loaded file \"%s\" (%d bytes)\n", SFN, i + br);
		res = true;
	}

	f_close(&sd_file);
	return res;
}

/*
 * check that all disks refer to existing files
 */
void check_disks(void)
{
	int i, n = 0;

	for (i = 0; i < NUMDRV; i++) {
		if (disks[i][0]) {
			/* try to open file */
			sd_res = f_open(&sd_file, disks[i], FA_READ);
			if (sd_res != FR_OK) {
				printf("Disk image \"%s\" no longer exists.\n",
				       disks[i]);
				close_disk(i);
				disks[i][0] = '\0';
				n++;
			} else
				f_close(&sd_file);
		}
	}
	if (n > 0)
		putchar('\n');
}

/*
 * mount a disk image 'name' on disk 'drive'
 */
void mount_disk(int drive, const char *name)
{
	char SFN[DISKLEN+1];
	int i;

	strcpy(SFN, DISKS_DIR);
	strcat(SFN, name);
	strcat(SFN, (drive == HD_SLOT) ? ".HDD" : ".DSK");

	for (i = 0; i < NUMDRV; i++) {
		if (i != drive && strcmp(disks[i], SFN) == 0) {
			puts("Disk already mounted\n");
			return;
		}
	}

	/* try to open file */
	sd_res = f_open(&sd_file, SFN, FA_READ);
	if (sd_res != FR_OK) {
		puts("File not found\n");
		return;
	}

	f_close(&sd_file);
	close_disk(drive);
	strcpy(disks[drive], SFN);
	putchar('\n');
}

/*
 * prepare I/O for sector read and write routines
 */
static BYTE prep_io(int drive, int track, int sector, WORD addr)
{
	FSIZE_t pos;
	int slot, spt;

	/* check if drive in range */
	slot = drive_slot(drive);
	if (slot < 0)
		return FDC_STAT_DISK;
	spt = slot_sectors(slot);

	/* check if track and sector in range */
	if (track >= slot_tracks(slot))
		return FDC_STAT_TRACK;
	if ((sector < 1) || (sector > spt))
		return FDC_STAT_SEC;

	/* check if DMA address in range */
	if (addr > 0xff7f)
		return FDC_STAT_DMAADR;

	/* check if disk in drive */
	if (!strlen(disks[slot])) {
		return FDC_STAT_NODISK;
	}

	/* open the disk image, unless it is open already */
	if (!dsk_open[slot]) {
		sd_res = f_open(&dsk_fil[slot], disks[slot],
				FA_READ | FA_WRITE);
		if (sd_res != FR_OK)
			return FDC_STAT_NODISK;
		dsk_open[slot] = true;
	}

	/* seek to track/sector */
	pos = (((FSIZE_t) track * (FSIZE_t) spt) + sector - 1) * SEC_SZ;
	if (f_lseek(&dsk_fil[slot], pos) != FR_OK) {
		close_disk(slot);
		return FDC_STAT_SEEK;
	}
	return FDC_STAT_OK;
}

/*
 * read from drive a sector on track into memory @ addr
 */
BYTE read_sec(int drive, int track, int sector, WORD addr)
{
	BYTE stat;
	unsigned int br;
	register int i;
	int slot;

	/* prepare for sector read */
	if ((stat = prep_io(drive, track, sector, addr)) != FDC_STAT_OK)
		return stat;

	slot = drive_slot(drive);
	disk_act = slot;

	/* read sector into memory */
	sd_res = f_read(&dsk_fil[slot], &dsk_buf[0], SEC_SZ, &br);
	if ((sd_res == FR_OK) && (br == SEC_SZ)) {
		for (i = 0; i < SEC_SZ; i++)
			dma_write(addr + i, dsk_buf[i]);
		stat = FDC_STAT_OK;
	} else {
		close_disk(slot);	/* force a reopen next time */
		stat = FDC_STAT_READ;
	}

	return stat;
}

/*
 * write to drive a sector on track from memory @ addr
 */
BYTE write_sec(int drive, int track, int sector, WORD addr)
{
	BYTE stat;
	unsigned int br;
	register int i;
	int slot;

	/* prepare for sector write */
	if ((stat = prep_io(drive, track, sector, addr)) != FDC_STAT_OK)
		return stat;

	slot = drive_slot(drive);
	disk_act = slot | 0x80;

	/* write sector to disk image */
	for (i = 0; i < SEC_SZ; i++)
		dsk_buf[i] = dma_read(addr + i);
	sd_res = f_write(&dsk_fil[slot], &dsk_buf[0], SEC_SZ, &br);
	if ((sd_res == FR_OK) && (br == SEC_SZ)) {
		/* the image is never extended, so only the cached window has
		   to reach the card for a surprise removal to be survivable */
		if (f_sync(&dsk_fil[slot]) == FR_OK) {
			stat = FDC_STAT_OK;
		} else {
			close_disk(slot);
			stat = FDC_STAT_WRITE;
		}
	} else {
		close_disk(slot);	/* force a reopen next time */
		stat = FDC_STAT_WRITE;
	}

	return stat;
}

/*
 * get FDC command from CPU memory
 */
void get_fdccmd(BYTE *cmd, WORD addr)
{
	register int i;

	for (i = 0; i < 4; i++)
		cmd[i] = dma_read(addr + i);
}
