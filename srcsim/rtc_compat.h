/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Calendar clock for the PicoCalc.
 *
 * RP2040 has a calendar RTC block; RP2350 dropped it in favour of the AON
 * timer, so pico-sdk gates hardware_rtc behind HAS_RP2040_RTC. This module
 * presents the RP2040 rtc_*_datetime() API on both parts, so simcfg.c needs
 * no conditional code.
 *
 * It also overrides the SDK's weak _gettimeofday(), which otherwise counts
 * from boot rather than from a wall clock. That matters because
 * iodevices/rtc80.c serves the emulated machine's clock out of time() and
 * localtime(), and it has to agree with what the config dialog displays.
 *
 * On RP2350 the datetime_t type only exists when the build defines
 * PICO_INCLUDE_RTC_DATETIME=1, which CMakeLists.txt does.
 */

#ifndef RTC_COMPAT_INC
#define RTC_COMPAT_INC

#include <stdbool.h>
#include "pico/types.h"

/* start the clock if nothing has set it yet, so time() is always sane */
extern void rtc_compat_init(void);

#if HAS_RP2040_RTC
#include "hardware/rtc.h"
#else
extern bool rtc_set_datetime(const datetime_t *dt);
extern bool rtc_get_datetime(datetime_t *dt);
#endif

#endif /* !RTC_COMPAT_INC */
