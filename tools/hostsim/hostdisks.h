#ifndef HOSTDISKS_INC
#define HOSTDISKS_INC

#include "sim.h"
#include "simdefs.h"

#define NUMDISK	4
#define HD_DRIVE 8
#define HD_SLOT	NUMDISK
#define NUMDRV	(NUMDISK + 1)
#define HD_TRK	256
#define HD_SPT	128

extern char disks[NUMDRV][256];

extern BYTE read_sec(int drive, int track, int sector, WORD addr);
extern BYTE write_sec(int drive, int track, int sector, WORD addr);
extern void get_fdccmd(BYTE *cmd, WORD addr);

#endif
