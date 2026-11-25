#!/bin/bash
#
# Tools/run_tests.sh: build and run every host unit test, one regression
# command. Binaries go to Build/tests/. No sudo, no network, no kext.
#
#   Tools/run_tests.sh            run all tests
#   Tools/run_tests.sh -v         also print each test's full output
#
# Prints PASS/FAIL per test and a summary; exits 0 only if all pass.
# Compile lines follow the comments at the top of each test file.
#
set -u
cd "$(dirname "$0")/.."

VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

CC="${CC:-clang}"
CXX="${CXX:-clang++}"
OUT=Build/tests
LOG=$OUT/logs
mkdir -p "$OUT" "$LOG"

SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
CFLAGS="-std=c11 -Wall -Wextra -Werror -O1"
CXXFLAGS="-std=c++17 -Wall -Wextra"

pass=0
fail=0
skip=0
failed=()

result() {	# name status
	printf '  %-18s %s\n' "$1" "$2"
}

# t name -- compile command... ; runs Build/tests/<name> after a good build
t() {
	local name=$1; shift
	local blog=$LOG/$name.build.log rlog=$LOG/$name.run.log
	if ! "$@" -o "$OUT/$name" >"$blog" 2>&1; then
		result "$name" "FAIL (build, see $blog)"
		[ $VERBOSE -eq 1 ] && cat "$blog"
		fail=$((fail + 1)); failed+=("$name"); return
	fi
	if "./$OUT/$name" >"$rlog" 2>&1; then
		result "$name" PASS
		pass=$((pass + 1))
	else
		result "$name" "FAIL (run, see $rlog)"
		tail -20 "$rlog"
		fail=$((fail + 1)); failed+=("$name")
	fi
	[ $VERBOSE -eq 1 ] && cat "$rlog"
}

# s name reason: test skipped because a file it needs is missing
s() {
	result "$1" "SKIP ($2)"
	skip=$((skip + 1))
}

echo "== host unit tests =="

# Driver security / parsing modules (plain C)
t sae_test          $CC -DRTL_SAE_TESTING $CFLAGS -ISource/Driver \
                        Source/Driver/SAE.c Tools/sae_test.c
t pmf_test          $CC $CFLAGS $SAN -ITools -ISource/Driver \
                        Tools/pmf_test.c Source/Driver/PMF.c Source/Driver/SAE.c
t csa_test          $CC $CFLAGS $SAN -ISource/Driver \
                        Tools/csa_test.c Source/Driver/CSA.c
t replay_test       $CC $CFLAGS $SAN -ISource/Driver \
                        Tools/replay_test.c Source/Driver/ReplayCheck.c
t security_ie_test  $CC $CFLAGS $SAN -ISource/Driver \
                        Tools/security_ie_test.c Source/Driver/SecurityIE.c
# same line as Tools/run_eapol_flow_test.sh, output in Build/tests
t eapol_flow_test   $CC $CFLAGS $SAN -ITools -ISource/Driver \
                        Tools/eapol_flow_test.c Source/Driver/SAE.c Source/Driver/SecurityIE.c \
                        Source/Driver/PMF.c Source/Driver/ReplayCheck.c
t statusled_test    $CC $CFLAGS $SAN -ISource/Driver Tools/statusled_test.c
t rxwd_test         $CC $CFLAGS $SAN -ISource/Driver Tools/rxwd_test.c

# AirLayer pure helpers (C++)
t baorig_test       $CXX $CXXFLAGS $SAN Tools/baorig_test.cpp
t share_test        $CXX $CXXFLAGS $SAN Tools/share_test.cpp
if [ -f Source/AirLayer/AWDLServiceMerge.h ] && [ -f Tools/svcmerge_test.cpp ]; then
	t svcmerge_test $CXX $CXXFLAGS $SAN Tools/svcmerge_test.cpp
else
	s svcmerge_test "AWDLServiceMerge.h or svcmerge_test.cpp missing"
fi
if [ -f Source/AirLayer/GateLogic.h ] && [ -f Source/AirLayer/KnownBuilds.inc ]; then
	t gate_test     $CXX -std=c++17 -ISource/AirLayer Tools/gate_test.cpp
else
	s gate_test "GateLogic.h or KnownBuilds.inc missing"
fi

# Compat layer against Tools/fakesdk (compile-only harness, includes test_all.c)
clog=$LOG/test_compat.log
if bash Tools/test_compat.sh >"$clog" 2>&1; then
	result test_compat PASS
	pass=$((pass + 1))
else
	result test_compat "FAIL (see $clog)"
	cat "$clog"
	fail=$((fail + 1)); failed+=(test_compat)
fi
[ $VERBOSE -eq 1 ] && cat "$clog"

echo "== $pass passed, $fail failed, $skip skipped =="
if [ $fail -ne 0 ]; then
	echo "failed: ${failed[*]}"
	exit 1
fi
exit 0
