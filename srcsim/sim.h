/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024-2025 by Udo Munk
 *
 * This is the configuration for a ClockworkPi PicoCalc
 */

#ifndef SIM_INC
#define SIM_INC

#define DEF_CPU Z80	/* default CPU (Z80 or I8080) */
//#define EXCLUDE_I8080	/* we want both CPU's */
#define CPU_SPEED 4	/* CPU speed 0=unlimited */
/*#define ALT_I8080*/	/* use alt. 8080 sim. primarily optimized for size */
/*#define ALT_Z80*/	/* use alt. Z80 sim. primarily optimized for size */
#define UNDOC_INST	/* compile undocumented instructions */
#ifndef EXCLUDE_Z80
/*#define FAST_BLOCK*/	/* much faster but not accurate Z80 block instr. */
#endif

/*#define WANT_ICE*/	/* attach ICE to headless machine */
#ifdef WANT_ICE
#define BAREMETAL	/* set up the simulator core for bare metal use */
#define WANT_TIM	/* count t-states */
#define HISIZE	100	/* number of entries in history */
#define SBSIZE	4	/* number of software breakpoints */
#define WANT_HB		/* hardware breakpoint */
#endif

#define USR_COM "PicoCalc-Z80"
#ifndef USR_REL		/* CMakeLists.txt passes git describe */
#define USR_REL "unknown"
#endif
#ifndef USR_CPR_YEARS	/* CMakeLists.txt passes 2026 to the build year */
#define USR_CPR_YEARS "2026"
#endif
#define USR_CPR "Copyright (C) " USR_CPR_YEARS " by Steve Gibson"

#ifndef PRIu64
#define PRIu64 "llu"
#endif

#endif
