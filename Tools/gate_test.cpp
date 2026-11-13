// Host test for the RTL8814AirLayer version gate.
//   clang++ -std=c++17 -ISource/AirLayer Tools/gate_test.cpp -o Tools/gate_test && ./Tools/gate_test
#include "GateLogic.h"
#include "KnownBuilds.inc"
#include <stdio.h>

static int failures, total;

#define CHECK(cond, ...) do { total++; if (!(cond)) { failures++; printf("FAIL line %d: %s  ", __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

static GateMeasured reference(const char *build)
{
	GateMeasured m = {};
	m.darwinMajor = kGateRef.darwinMajor;
	snprintf(m.osversion, sizeof(m.osversion), "%s", build);
	for (int i = 0; i < GATE_NCLS; i++) {
		m.size[i] = kGateRef.cls[i].size;
		snprintf(m.super[i], GATE_NAMELEN, "%s", kGateRef.cls[i].super);
	}
	return m;
}

static GateVerdict run(const GateMeasured &m, const char *trust, int force, char *why)
{
	return gateDecide(&m, &kGateRef, kGateValidated, kGateValidatedCount, trust, force, why, 200);
}

int main()
{
	char why[200];
	GateMeasured m;

	m = reference("25G241");
	CHECK(run(m, "", 0, why) == GateOpenValidated, "%s", why);

	m = reference("25G332");
	CHECK(run(m, "", 0, why) == GateClosedUnknownBuild, "%s", why);
	CHECK(strstr(why, "rtlairtrust=25G332") != nullptr, "hint missing: %s", why);
	CHECK(run(m, nullptr, 0, why) == GateClosedUnknownBuild, "null trust: %s", why);
	CHECK(run(m, "25G332", 0, why) == GateOpenTrusted, "%s", why);
	CHECK(run(m, "25G241", 0, why) == GateClosedUnknownBuild, "trust for another build must not open: %s", why);
	CHECK(run(m, "25G33", 0, why) == GateClosedUnknownBuild, "prefix must not match: %s", why);
	CHECK(run(m, "25G3321", 0, why) == GateClosedUnknownBuild, "longer string must not match: %s", why);

	// a validated build whose structure changed is still closed
	m = reference("25G241");
	m.size[3] += 8;
	CHECK(run(m, "", 0, why) == GateClosedStructure, "%s", why);
	CHECK(strstr(why, "IO80211SkywalkInterface") != nullptr, "%s", why);
	CHECK(run(m, "25G241", 0, why) == GateClosedStructure, "trust must not override structure: %s", why);

	m = reference("25G241");
	m.size[6] = 0;
	CHECK(run(m, "", 0, why) == GateClosedStructure && strstr(why, "not found"), "%s", why);

	m = reference("25G241");
	snprintf(m.super[5], GATE_NAMELEN, "%s", "IOService");
	CHECK(run(m, "", 0, why) == GateClosedStructure && strstr(why, "superclass"), "%s", why);

	m = reference("25G241");
	m.super[0][0] = 0;
	CHECK(run(m, "", 0, why) == GateClosedStructure && strstr(why, "none"), "%s", why);

	m = reference("25G241");
	m.darwinMajor = 26;
	CHECK(run(m, "", 0, why) == GateClosedStructure && strstr(why, "Darwin"), "%s", why);

	// the escape hatch opens on anything, including an empty measurement
	m = {};
	CHECK(run(m, "", 1, why) == GateOpenForced, "%s", why);

	// an empty measurement without the hatch stays closed
	CHECK(run(m, "", 0, why) == GateClosedStructure, "%s", why);

	CHECK(gateIsOpen(GateOpenValidated) && gateIsOpen(GateOpenTrusted) && gateIsOpen(GateOpenForced), "open set");
	CHECK(!gateIsOpen(GateClosedStructure) && !gateIsOpen(GateClosedUnknownBuild), "closed set");

	printf("%d checks, %d failed\n", total, failures);
	return failures ? 1 : 0;
}
