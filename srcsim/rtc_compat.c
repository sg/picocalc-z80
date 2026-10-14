/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Calendar clock for the PicoCalc. See rtc_compat.h.
 */

#include <time.h>
#include <sys/time.h>

#include "pico/stdlib.h"
#include "pico/aon_timer.h"

#include "rtc_compat.h"

/* the emulated machine's clock is meaningless before the user sets one */
#define DEFAULT_YEAR	2026
#define DEFAULT_MONTH	1
#define DEFAULT_DAY	1

#if !HAS_RP2040_RTC

static void dt_to_tm(const datetime_t *dt, struct tm *tm)
{
	tm->tm_year  = dt->year - 1900;
	tm->tm_mon   = dt->month - 1;
	tm->tm_mday  = dt->day;
	tm->tm_wday  = dt->dotw;
	tm->tm_hour  = dt->hour;
	tm->tm_min   = dt->min;
	tm->tm_sec   = dt->sec;
	tm->tm_isdst = 0;
}

static void tm_to_dt(const struct tm *tm, datetime_t *dt)
{
	dt->year  = tm->tm_year + 1900;
	dt->month = tm->tm_mon + 1;
	dt->day   = tm->tm_mday;
	dt->dotw  = tm->tm_wday;
	dt->hour  = tm->tm_hour;
	dt->min   = tm->tm_min;
	dt->sec   = tm->tm_sec;
}

bool rtc_set_datetime(const datetime_t *dt)
{
	struct tm tm;

	dt_to_tm(dt, &tm);
	if (aon_timer_is_running())
		return aon_timer_set_time_calendar(&tm);
	aon_timer_start_calendar(&tm);
	return true;
}

bool rtc_get_datetime(datetime_t *dt)
{
	struct tm tm;

	if (!aon_timer_is_running())
		return false;
	if (!aon_timer_get_time_calendar(&tm))
		return false;
	tm_to_dt(&tm, dt);
	return true;
}

#endif /* !HAS_RP2040_RTC */

/*
 * Strong override of the SDK's weak boot-relative implementation, so that
 * time() and localtime() read the same clock as rtc_get_datetime(). The
 * AON timer is the RTC itself on RP2040, so this serves both parts.
 */
int _gettimeofday(struct timeval *__restrict tv, void *__restrict tz)
{
	struct timespec ts;

	(void) tz;

	if (!tv)
		return 0;
	if (!aon_timer_is_running()) {
		tv->tv_sec = 0;
		tv->tv_usec = 0;
		return 0;
	}
	aon_timer_get_time(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
	return 0;
}

void rtc_compat_init(void)
{
#if HAS_RP2040_RTC
	datetime_t dt;

	rtc_init();
	dt.year = DEFAULT_YEAR; dt.month = DEFAULT_MONTH; dt.day = DEFAULT_DAY;
	dt.dotw = 0; dt.hour = 0; dt.min = 0; dt.sec = 0;
	rtc_set_datetime(&dt);
#else
	struct tm tm = {
		.tm_year  = DEFAULT_YEAR - 1900,
		.tm_mon   = DEFAULT_MONTH - 1,
		.tm_mday  = DEFAULT_DAY,
		.tm_hour  = 0,
		.tm_min   = 0,
		.tm_sec   = 0,
		.tm_isdst = 0,
	};

	if (!aon_timer_is_running())
		aon_timer_start_calendar(&tm);
#endif
}
