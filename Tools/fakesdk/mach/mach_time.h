#ifndef _FAKE_MACH_TIME_H
#define _FAKE_MACH_TIME_H
#include <stdint.h>
#include <stdbool.h>
#include <mach/kern_return.h>
typedef struct { uint32_t numer, denom; } mach_timebase_info_data_t;
typedef mach_timebase_info_data_t *mach_timebase_info_t;
uint64_t mach_absolute_time(void);
/* NO mach_timebase_info — userland API, absent from the Kernel SDK */
#endif
