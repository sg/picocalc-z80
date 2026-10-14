/* host build of the PicoCalc machine: platform hooks */
#ifndef SIMPORT_INC
#define SIMPORT_INC

#include <stdint.h>
#include <time.h>
#include <unistd.h>

static inline void sleep_for_us(long t) { usleep(t); }
static inline void sleep_for_ms(int t) { usleep((long) t * 1000); }

static inline uint64_t get_clock_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

extern int get_cmdline(char *buf, int len);

#endif
