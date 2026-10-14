/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2019 by Mike Douglas
 *
 * Host file I/O using a BDOS like interface. See hostbdos.c.
 */

#ifndef HOSTBDOS_INC
#define HOSTBDOS_INC

#include "sim.h"
#include "simdefs.h"

extern void host_bdos_out(BYTE outByte);

#endif /* !HOSTBDOS_INC */
