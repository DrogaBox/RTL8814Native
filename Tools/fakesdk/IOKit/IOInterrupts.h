#ifndef _FAKE_IOINTERRUPTS_H
#define _FAKE_IOINTERRUPTS_H
#include <stdbool.h>
typedef struct _IOSimpleLock IOSimpleLock;
/* IOSimpleLock KPI — spinlocks with the exported try-acquire variant */
void          IOSimpleLockInit(IOSimpleLock *);
IOSimpleLock *IOSimpleLockAlloc(void);
void          IOSimpleLockFree(IOSimpleLock *);
void          IOSimpleLockLock(IOSimpleLock *);
bool          IOSimpleLockTryLock(IOSimpleLock *);
void          IOSimpleLockUnlock(IOSimpleLock *);
#endif
