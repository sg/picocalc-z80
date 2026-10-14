/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024-2025 by Udo Munk
 *
 * This module configures the machine appropriate for the
 * Z80/8080 software we want to run on it.
 *
 * History:
 * 23-APR-2024 dummy, no configuration implemented yet
 * 12-MAY-2024 implemented configuration dialog
 * 28-MAY-2024 implemented mount/unmount of disk images
 * 24-MAY-2025 separate read/save config file from config and add network config
 */

#ifndef SIMCFG_INC
#define SIMCFG_INC

/* empty means the built-in 6x8 font, giving 53x40 instead of 80x40 */
extern char font_file[];

extern void apply_font(void);
extern void read_config(void);
extern void save_config(void);
extern void config(void);
#ifdef PICOCALC_WIFI
extern void net_autoconnect(void);
#endif

#endif /* !SIMCFG_INC */
