/* fakesdk OSAtomic.h — SIGNATURE-CRITICAL: the KERNEL framework prototype
 * is  bool OSCompareAndSwap(UInt32, UInt32, volatile UInt32 *)
 * (the int/int/volatile-int* form is the USERLAND libSystem one — do not
 * confuse them). None of our shims use it anymore (__sync_* builtins
 * instead); kept here so the simulator stays faithful to the SDK. */
#ifndef _FAKE_OSATOMIC_H
#define _FAKE_OSATOMIC_H
#include <stdbool.h>
#include <stdint.h>
#include <libkern/OSTypes.h>
int  OSAddAtomic(int delta, volatile int *address);
int  OSIncrementAtomic(volatile int *address);
int  OSDecrementAtomic(volatile int *address);
int  OSIncrementAtomic64(volatile int64_t *address);
int  OSDecrementAtomic64(volatile int64_t *address);
bool OSCompareAndSwap(UInt32 oldValue, UInt32 newValue, volatile UInt32 *address);
void OSMemoryBarrier(void);
#endif
