/* fakesdk kern/thread_call.h — timer callout KPI (C) */
#ifndef _FAKE_KERN_THREAD_CALL_H
#define _FAKE_KERN_THREAD_CALL_H
#include <stdint.h>
#include <stdbool.h>
typedef int boolean_t;
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE  1
#endif
typedef struct _thread_call *thread_call_t;
typedef void *thread_call_param_t;
typedef void (*thread_call_func_t)(thread_call_param_t, thread_call_param_t);
thread_call_t thread_call_allocate(thread_call_func_t, thread_call_param_t);
int  thread_call_enter_delayed(thread_call_t, uint64_t);
boolean_t thread_call_cancel(thread_call_t);
void thread_call_free(thread_call_t);
#endif
