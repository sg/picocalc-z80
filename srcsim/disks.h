/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk & Thomas Eberhardt
 *
 * This module implements the disks drives and low level
 * access functions for MicroSD, needed by the FDC.
 *
 * History:
 * 29-JUN-2024 split of from memsim.c and picosim.c
 */

#ifndef DISKS_INC
#define DISKS_INC

#include "ff.h"

#include "sim.h"
#include "simdefs.h"

#define NUMDISK	4		/* number of floppy drives, A: to D: */

/*
 * A 4 MB hard disk as drive I:. The FDC protocol already carries a 4 bit
 * drive number and a byte of track and sector, so this needs no new
 * hardware - drive 8 just has a different geometry and its own DPB in the
 * BIOS. 256 tracks of 128 sectors is exactly 4 MB and keeps both track and
 * sector inside a byte.
 */
#define HD_DRIVE 8		/* drive number the BIOS uses for I: */
#define HD_SLOT	NUMDISK		/* its slot in the tables below */
#define NUMDRV	(NUMDISK + 1)
#define HD_TRK	256		/* tracks on the hard disk */
#define HD_SPT	128		/* sectors per track */
#define FNLEN	8		/* length of filename without extension */

/*
 * Everything this machine uses lives under one directory on the card.
 * FatFs matches names without regard to case, so /z80/disks finds /Z80/DISKS
 * and cpm22.dsk finds CPM22.DSK; names are shown in upper case regardless,
 * which is how they appear on the CP/M side.
 */
#define Z80_DIR		"/Z80/"
#define DISKS_DIR	Z80_DIR "disks/"
#define CODE_DIR	Z80_DIR "code/"
#define XFER_DIR	Z80_DIR "xfer/"
#define FONTS_DIR	Z80_DIR "fonts/"

/* longest of the above, plus 8.3 name */
#define DISKLEN	(sizeof(DISKS_DIR) - 1 + FNLEN + 4)

extern FIL sd_file;
extern FRESULT sd_res;
extern char disks[NUMDRV][DISKLEN+1];
extern bool sd_mounted;
extern volatile int disk_act;

extern void init_disks(void), exit_disks(void);
extern void list_files(const char *dir, const char *ext);
extern bool load_file(const char *name);
extern void check_disks(void);
extern void mount_disk(int drive, const char *name);
/* put image name, a stem with no directory or extension, in a slot; false
   with *err set when it is not on the card or is in another drive */
extern bool mount_image(int slot, const char *name, const char **err);
extern void eject_disk(int slot);
extern char drive_letter(int slot);

extern BYTE read_sec(int drive, int track, int sector, WORD addr);
extern BYTE write_sec(int drive, int track, int sector, WORD addr);
extern void get_fdccmd(BYTE *cmd, WORD addr);

#endif /* !DISK_INC */
