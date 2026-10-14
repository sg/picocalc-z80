/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2019 by Mike Douglas
 *
 * Ported to FatFs for PicoCalc
 * Copyright (C) 2026 Steve Gibson
 *
 * Host file I/O through a BDOS-like interface, so that CP/M can move files
 * to and from the card. Ported from z80pack's iodevices/simbdos.c, which
 * works against stdio on a PC, to FatFs and the xfer directory here.
 *
 * The CP/M side is R.COM and W.COM from z80pack's cpmtools, unmodified.
 * Files are opened relative to /Z80/xfer, so CP/M cannot reach the rest of
 * the card - in particular it cannot open the disk images it is running on.
 *
 * Files written to the card are binary unless the extension appears in the
 * table below, in which case the copy stops at the first ctrl-Z, which is
 * how CP/M marks the end of a text file.
 *
 * History:
 * 03-OCT-2019 (Mike Douglas) Original
 * 23-JAN-2025 (Thomas Eberhardt) Use DMA memory access
 * 15-AUG-2026 ported to FatFs for the PicoCalc
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "ff.h"

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"

#include "disks.h"
#include "hostbdos.h"

/* BDOS Equates */

#define	OPENF	15		/* open file */
#define	CLOSEF	16		/* close file */
#define READF	20		/* read file */
#define WRITEF	21		/* write file */
#define MAKEF	22		/* make file */
#define SETDMA	26		/* set DMA address */

#define	SECLEN	128		/* logical sector length */
#define	CTRL_Z	0x1A		/* CP/M EOF character */

/* The following file types will be treated as text files */

static const char *textExts[] = {
	"ASM", "MAC", "SRC", "TXT", "PRN", "LST",
	"SYM", "LIB", "C",   "PAS", "FOR", "RAT",
	"CBL", "PLM", "PLI", "HEX", "SUB", NULL
};

static FIL fp;
static bool isOpen;
static WORD dmaAddr = 0x80;	/* buffer address in emulated space */
static bool textFile;		/* text file flag */

/*
 * host_bdos_out - an output to this I/O device is somewhat equivalent
 *    to calling the CP/M BDOS entry point, but the file I/O operation
 *    is performed on the card. As a bit of protection from inadvertent
 *    entry, the output value must be the compliment of the command in
 *    register C.
 */
void host_bdos_out(BYTE outByte)
{
	WORD fcbAddr;		/* address of FCB in simulator memory */
	char path[sizeof(XFER_DIR) + FNLEN + 4];
	char extension[8];
	BYTE buf[SECLEN];
	unsigned int xferLen, n;
	int i;

	outByte = ~outByte;	/* compiler requires assignment */
	if (outByte != C)	/*    for the compare to work */
		return;

	A = 0xff;		/* assume error status */
	fcbAddr = (D << 8) + E;	/* FCB address in simulator memory */

	if ((C == OPENF) || (C == MAKEF)) {
		/* CP/M holds names upper case and padded with blanks */
		strcpy(path, XFER_DIR);
		n = strlen(path);
		for (i = 0; i < 8; i++) {
			char c = toupper(dma_read(fcbAddr + 1 + i));

			if (c == ' ')
				break;
			path[n++] = c;
		}

		for (i = 0; i < 3; i++) {
			char c = toupper(dma_read(fcbAddr + 9 + i));

			if (c == ' ')
				break;
			extension[i] = c;
		}
		extension[i] = '\0';

		if (extension[0] != '\0') {
			path[n++] = '.';
			strcpy(path + n, extension);
		} else
			path[n] = '\0';

		if (isOpen) {		/* a stale handle would leak */
			f_close(&fp);
			isOpen = false;
		}

		if (C == MAKEF) {
			textFile = false;
			for (i = 0; textExts[i] != NULL; i++)
				if (strcmp(extension, textExts[i]) == 0) {
					textFile = true;
					break;
				}
			if (f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS)
			    == FR_OK) {
				isOpen = true;
				A = 0;
			}
		} else {
			if (f_open(&fp, path, FA_READ) == FR_OK) {
				isOpen = true;
				A = 0;
			}
		}
	}

	else if (C == CLOSEF) {
		if (isOpen) {
			f_close(&fp);
			isOpen = false;
		}
		A = 0;
	}

	else if (C == READF) {
		if (isOpen && (f_read(&fp, buf, SECLEN, &xferLen) == FR_OK)
		    && (xferLen != 0)) {
			for (n = 0; n < xferLen; n++)
				dma_write(dmaAddr + n, buf[n]);
			for (; n < SECLEN; n++)
				dma_write(dmaAddr + n, CTRL_Z);
			A = 0;
		}
	}

	else if (C == WRITEF) {
		if (isOpen) {
			for (xferLen = 0; xferLen < SECLEN; xferLen++) {
				buf[xferLen] = dma_read(dmaAddr + xferLen);
				if ((buf[xferLen] == CTRL_Z) && textFile)
					break;	/* ctrl-z (EOF) found */
			}
			if (xferLen == 0)	/* record is only ctrl-z */
				A = 0;
			else if ((f_write(&fp, buf, xferLen, &n) == FR_OK)
				 && (n == xferLen)) {
				/* the card can be pulled at any moment */
				f_sync(&fp);
				A = 0;
			}
		}
	}

	else if (C == SETDMA) {
		dmaAddr = fcbAddr;
		A = 0;
	}
}
