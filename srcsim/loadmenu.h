/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * The program loader of the config dialog: a box listing the .BIN files in
 * /Z80/code, the chosen one loaded at address 0 for R to run.
 */

#ifndef LOADMENU_INC
#define LOADMENU_INC

#include <stdbool.h>
#include <stddef.h>

/* true with the program's name and size when one was loaded */
extern bool load_menu(char *name, size_t len, int *bytes);

#endif /* !LOADMENU_INC */
