/* fakesdk IOMemoryDescriptor.h — C++ POISON by design: if any C
 * translation unit ever pulls this in again, the 'template' keyword
 * explodes exactly like on the real 26.5 SDK build. */
#ifndef _FAKE_IOMEMDESC_H
#define _FAKE_IOMEMDESC_H
template <typename T>
struct OSPtrFake { T *ptr; };
class OSObjectFake { public: virtual ~OSObjectFake(); };
#include <libkern/c++/OSPtrFakeNeverExists.h>
#endif
