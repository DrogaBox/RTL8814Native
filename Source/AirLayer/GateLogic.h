/*
 * RTL8814AirLayer version gate: the decision, with no kernel calls, so the
 * same code runs in the kext and in the host test (Tools/gate_test.cpp).
 *
 * The layer is built against IO80211 / Skywalk class layouts measured on one
 * macOS build. If another build changed them, calling into the layer can
 * panic at boot, so the layer only starts when the running system matches:
 *
 *   structure  Darwin major, and for each class the layer subclasses or uses:
 *              it exists, its instance size and its superclass name match the
 *              reference the header was generated from.
 *   build      the macOS build string (kern.osversion, e.g. 25G241) is in the
 *              validated list, or the boot-arg rtlairtrust=<build> names it.
 *
 * The structure must always match. A build that is not validated stays
 * closed unless rtlairtrust names it, so a macOS update never starts the
 * layer by surprise. rtlairnogate=1 skips every check (unsafe, for bring-up).
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef RTL_GATE_LOGIC_H
#define RTL_GATE_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef KERNEL
#include <stdio.h>
#endif

#define GATE_NCLS     7
#define GATE_NAMELEN  40
#define GATE_BUILDLEN 24

struct GateClassRef {
	const char *name;
	const char *super;
	uint32_t size;
};

struct GateReference {
	int darwinMajor;
	GateClassRef cls[GATE_NCLS];
};

struct GateMeasured {
	int darwinMajor;
	char osversion[GATE_BUILDLEN];
	uint32_t size[GATE_NCLS];              /* 0 = class not found */
	char super[GATE_NCLS][GATE_NAMELEN];   /* "" = no superclass found */
};

enum GateVerdict {
	GateOpenValidated,     /* build is in the validated list */
	GateOpenTrusted,       /* build named by rtlairtrust, structure matches */
	GateOpenForced,        /* rtlairnogate=1 */
	GateClosedStructure,   /* a class differs from the reference */
	GateClosedUnknownBuild /* structure matches, build never validated */
};

static inline int gateIsOpen(GateVerdict v)
{
	return v <= GateOpenForced;
}

static inline const char *gateVerdictName(GateVerdict v)
{
	switch (v) {
	case GateOpenValidated:     return "open-validated";
	case GateOpenTrusted:       return "open-trusted";
	case GateOpenForced:        return "open-forced";
	case GateClosedStructure:   return "closed-structure";
	case GateClosedUnknownBuild:return "closed-unknown-build";
	}
	return "closed";
}

/* 0 when the running system has the reference structure, else 1 with the
 * first difference written to why. */
static inline int gateStructureMismatch(const GateMeasured *m, const GateReference *r,
                                        char *why, size_t whyLen)
{
	if (m->darwinMajor != r->darwinMajor) {
		snprintf(why, whyLen, "Darwin %d, reference is %d", m->darwinMajor, r->darwinMajor);
		return 1;
	}
	for (int i = 0; i < GATE_NCLS; i++) {
		const GateClassRef *c = &r->cls[i];
		if (m->size[i] == 0) {
			snprintf(why, whyLen, "class %s not found", c->name);
			return 1;
		}
		if (m->size[i] != c->size) {
			snprintf(why, whyLen, "%s size 0x%x, reference is 0x%x", c->name,
			         (unsigned)m->size[i], (unsigned)c->size);
			return 1;
		}
		if (strcmp(m->super[i], c->super) != 0) {
			snprintf(why, whyLen, "%s superclass '%s', reference is '%s'", c->name,
			         m->super[i][0] ? m->super[i] : "none", c->super);
			return 1;
		}
	}
	return 0;
}

static inline GateVerdict gateDecide(const GateMeasured *m, const GateReference *r,
                                     const char *const *validated, size_t nValidated,
                                     const char *trust, int force, char *why, size_t whyLen)
{
	if (force) {
		snprintf(why, whyLen, "all checks skipped by rtlairnogate");
		return GateOpenForced;
	}
	if (gateStructureMismatch(m, r, why, whyLen))
		return GateClosedStructure;
	for (size_t i = 0; i < nValidated; i++) {
		if (strcmp(m->osversion, validated[i]) == 0) {
			snprintf(why, whyLen, "build %s validated, structure matches", m->osversion);
			return GateOpenValidated;
		}
	}
	if (trust && trust[0] && strcmp(trust, m->osversion) == 0) {
		snprintf(why, whyLen, "build %s accepted by rtlairtrust, not validated, structure matches",
		         m->osversion);
		return GateOpenTrusted;
	}
	snprintf(why, whyLen, "build %s was never validated, structure matches; "
	         "boot-arg rtlairtrust=%s starts the layer on this build",
	         m->osversion, m->osversion);
	return GateClosedUnknownBuild;
}

#endif /* RTL_GATE_LOGIC_H */
