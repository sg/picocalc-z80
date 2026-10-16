/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Release version strings: "v0.1.7" from a tag, "v0.1.6-3-g93260a6+" from
 * git describe on a later commit. Plain C so it can be tested on a PC.
 */

#ifndef VERSION_INC
#define VERSION_INC

#include <stdbool.h>

typedef struct {
	int major, minor, patch;
	int commits;	/* past the tag, 0 for the tag itself */
} version_t;

/* false when s does not start with [v]N.N.N */
extern bool version_parse(const char *s, version_t *v);

/* <0, 0, >0 like strcmp; anything that is not a version is older than
   anything that is */
extern int version_cmp(const char *a, const char *b);

#endif /* !VERSION_INC */
