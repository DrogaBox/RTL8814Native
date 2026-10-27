#ifndef _FAKE_STDBOOL_H
#define _FAKE_STDBOOL_H
#ifdef __cplusplus
/* bool/true/false are builtins in C++ */
#else
#define bool  _Bool
#define true  1
#define false 0
#endif
#endif
