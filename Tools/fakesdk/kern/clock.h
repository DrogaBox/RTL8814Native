/* fakesdk kern/clock.h — kext-exported clock KPI (no mach_timebase_info!) */
#ifndef _FAKE_KERN_CLOCK_H
#define _FAKE_KERN_CLOCK_H
#include <stdint.h>
void clock_get_uptime(uint64_t *uptime);
void absolutetime_to_nanoseconds(uint64_t abstime, uint64_t *ns);
void nanoseconds_to_absolutetime(uint64_t ns, uint64_t *abstime);
#endif
