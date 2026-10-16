/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Release version strings. See version.h.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "version.h"

static bool number(const char **p, int *out)
{
	if (!isdigit((unsigned char) **p))
		return false;
	*out = (int) strtol(*p, (char **) p, 10);
	return true;
}

bool version_parse(const char *s, version_t *v)
{
	const char *p = s;

	memset(v, 0, sizeof(*v));
	if (*p == 'v' || *p == 'V')
		p++;
	if (!number(&p, &v->major) || *p != '.')
		return false;
	p++;
	if (!number(&p, &v->minor) || *p != '.')
		return false;
	p++;
	if (!number(&p, &v->patch))
		return false;
	/* git describe appends -<commits>-g<hash>, and + for local changes */
	if (*p == '-') {
		p++;
		if (!number(&p, &v->commits))
			v->commits = 0;
	}
	return true;
}

int version_cmp(const char *a, const char *b)
{
	version_t va, vb;
	bool oka = version_parse(a, &va), okb = version_parse(b, &vb);

	if (!oka || !okb)
		return (int) oka - (int) okb;
	if (va.major != vb.major)
		return va.major - vb.major;
	if (va.minor != vb.minor)
		return va.minor - vb.minor;
	if (va.patch != vb.patch)
		return va.patch - vb.patch;
	return va.commits - vb.commits;
}
