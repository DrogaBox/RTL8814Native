/* fakesdk kern/thread.h
 *
 * SIGNATURE-CRITICAL: kernel_thread_start is 3-arg in the kext SDK
 * (continuation, parameter, new_thread) — NOT the 4-arg private form. */
#ifndef _FAKE_KERN_THREAD_H
#define _FAKE_KERN_THREAD_H
#include <mach/mach_types.h>
typedef int integer_t;
typedef void (*thread_continue_t)(void);
thread_t current_thread(void);
kern_return_t thread_terminate(thread_t);
kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
#endif
