/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The Pimoroni Pico Plus 2 W's 8 MB QSPI PSRAM, on the RP2350's QMI chip
 * select 1. Once set up it is plain memory at PSRAM_BASE, behind the 16 KB
 * XIP cache the flash code shares: sequential copies run at tens of MB/s, a
 * stray access costs about 24 QSPI clocks, and a byte-at-a-time walk mixed
 * with code from flash thrashes the cache, so it is used for bulk copies.
 * Boards without the chip get the stubs and a size of 0.
 */

#ifndef PSRAM_INC
#define PSRAM_INC

#include <stddef.h>
#include <stdint.h>

#define PSRAM_BASE	0x11000000u

/* set the QMI up and size the chip; first thing on core 0, before anything
   runs code with interrupts or touches the second core; 0 without a chip */
extern size_t psram_init(void);
extern size_t psram_size(void);

/* sequential throughput in KB/s through a scratch buffer of len bytes,
   measured once so the figures mean this board and this clock */
extern void psram_measure(void *scratch, size_t len, unsigned *read_kbs,
			  unsigned *write_kbs);

#endif /* !PSRAM_INC */
