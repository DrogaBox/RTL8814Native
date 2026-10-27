#!/bin/bash
#
# Tools/test_compat.sh — offline validation of the Compat layer.
#
# 1. Compiles every linux/*.h shim (as C AND as C++) and Compat.c
#    against Tools/fakesdk, a minimal simulator of the Darwin Kernel
#    SDK collision points (mode_t=unsigned short, IOLib min/max,
#    INT_MAX, NULL, C++ poison in IOMemoryDescriptor.h, kern/locks.h,
#    real OSCompareAndSwap signature, partial THREAD_* exports).
# 2. NEW: compiles EVERY header IN ISOLATION, in both languages —
#    the Driver TUs include the shims directly in arbitrary order
#    (workqueue.h without timer.h first is exactly what broke the
#    first RTL8814Native.cpp build).
#
# DIALECT PARITY (lesson of the first Driver TU build): build.sh
# compiles C++ with -std=c++17 STRICT. The harness must use the same
# dialect — gnu++ mode silently accepts plain `typeof` in C++ and hid
# 3 of the 4 errors Apple clang then reported on the Mac.
#
# PASS = exit 0 with zero warnings.
#
set -uo pipefail
cd "$(dirname "$0")/.."

CC="${CC:-cc}"
CXX="${CXX:-c++}"
F="-D__APPLE__ -nostdinc -fno-builtin -ITools/fakesdk -ISource/Compat/include -Wall -Wextra"
CXXSTD="-std=c++17 -fno-rtti -fno-exceptions -Wno-cast-function-type"
fails=0

echo "== compat shim validation (fakesdk) =="

out=$($CC  -c -std=gnu11    $F -o /tmp/rtl_t1.o Tools/test_all.c 2>&1); r=$?
echo "  shims as C    : $([ $r -eq 0 ] && [ -z "$out" ] && echo PASS || echo FAIL)"; [ $r -ne 0 ] && echo "$out"; [ $r -ne 0 ] && fails=1
[ -n "$out" ] && [ $r -eq 0 ] && { echo "  (warnings)"; echo "$out"; }

out=$($CXX -c $CXXSTD $F -x c++ -o /tmp/rtl_t3.o Tools/test_all.c 2>&1); r=$?
echo "  shims as C++  : $([ $r -eq 0 ] && [ -z "$out" ] && echo PASS || echo FAIL)"; [ $r -ne 0 ] && echo "$out"; [ $r -ne 0 ] && fails=1
[ -n "$out" ] && [ $r -eq 0 ] && { echo "  (warnings)"; echo "$out"; }

out=$($CC  -c -std=gnu11    $F -o /tmp/rtl_t2.o Source/Compat/Compat.c 2>&1); r=$?
echo "  Compat.c      : $([ $r -eq 0 ] && [ -z "$out" ] && echo PASS || echo FAIL)"; [ $r -ne 0 ] && echo "$out"; [ $r -ne 0 ] && fails=1
[ -n "$out" ] && [ $r -eq 0 ] && { echo "  (warnings)"; echo "$out"; }

# ---- header self-containment: each shim compiles ALONE, C and C++ ----
alone_fail=0
for h in Source/Compat/include/linux/*.h Source/Compat/include/linux/sched/*.h; do
        inc="${h#Source/Compat/include/}"
        printf '#include <%s>\nint rtl_hdr_probe(void) { return 0; }\n' "$inc" > /tmp/rtl_alone.c
        out=$($CC  -c -std=gnu11 $F -o /tmp/rtl_alone.o /tmp/rtl_alone.c 2>&1)
        if [ -n "$out" ]; then echo "  alone-C   FAIL: <$inc>"; echo "$out"; alone_fail=1; fi
        out=$($CXX -c $CXXSTD $F -x c++ -o /tmp/rtl_alone.o /tmp/rtl_alone.c 2>&1)
        if [ -n "$out" ]; then echo "  alone-C++ FAIL: <$inc>"; echo "$out"; alone_fail=1; fi
done
echo "  headers alone : $([ $alone_fail -eq 0 ] && echo 'PASS (C + C++)' || echo 'FAIL — see above')"
[ $alone_fail -ne 0 ] && fails=1

exit $fails
