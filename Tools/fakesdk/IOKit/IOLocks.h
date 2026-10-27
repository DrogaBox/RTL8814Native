/* fakesdk IOLocks.h — wait results like the real include chain */
#ifndef _FAKE_IOLOCKS_H
#define _FAKE_IOLOCKS_H
#include <stdint.h>
#include <stdbool.h>
#include <kern/locks.h>
#include <IOKit/IOInterrupts.h>
typedef struct _IOLock IOLock;
typedef int wait_result_t;
#define THREAD_AWAKENED     0
#define THREAD_TIMED_OUT    1
#define THREAD_INTERRUPTED  2
#define THREAD_RESTART      3
/* NO THREAD_TERMINATED — kernel-private: mirrors the partial export
 * of the real SDK that bit us on Compat.c's single-#ifndef block */
typedef unsigned int wait_interrupt_t;
#define THREAD_UNINTERRUPTIBLE 0
#define THREAD_INTERRUPTIBLE   1
IOLock *IOLockAlloc(void);
void    IOLockFree(IOLock *);
void    IOLockLock(IOLock *);
void    IOLockUnlock(IOLock *);
bool    IOLockTryLock(IOLock *);
int     IOLockSleep(IOLock *, void *event, wait_interrupt_t);
int     IOLockSleepDeadline(IOLock *, void *event, uint64_t, wait_interrupt_t);
void    IOLockWakeup(IOLock *, void *event, bool oneThread);
#endif
