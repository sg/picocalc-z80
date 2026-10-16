/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk & Thomas Eberhardt
 */

#ifndef PICOSIM_INC
#define PICOSIM_INC

extern int speed;

#ifdef PICOCALC_STACK_WATERMARK
extern unsigned stack_headroom(void);	/* core 0 stack never touched, bytes */
#endif

#endif /* !PICOSIM_INC */
