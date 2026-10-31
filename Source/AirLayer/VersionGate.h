/*
 * RTL8814AirLayer version gate, kernel side: measures the running system and
 * hands it to GateLogic.h (policy and boot-args are described there).
 *
 * Included by RTLAirLayer.cpp after LOG, gLogTarget and gLogLock exist. Only
 * stable KPIs are used (version_major, osversion, OSMetaClass), so measuring
 * cannot depend on the Apple layouts it is there to check.
 *
 * The result goes to the RTL8814Native node, next to RTLAirLayerLog:
 *   RTLAirGate         "<verdict>: <reason>"
 *   RTLAirGateBuild    the macOS build string
 *   RTLAirFingerprint  darwin, and "<class>" size / "<class>.super" per class
 * Tools/airlayer_gate.py reads them.
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
#ifndef RTL_VERSION_GATE_H
#define RTL_VERSION_GATE_H

#include "GateLogic.h"
#include "KnownBuilds.inc"
#include <libkern/version.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSDictionary.h>
#include <pexpert/pexpert.h>

static bool gGateDone;
static bool gGateOpen;

static void gateMeasure(GateMeasured *m)
{
	memset(m, 0, sizeof(*m));
	m->darwinMajor = version_major;
	strlcpy(m->osversion, osversion, sizeof(m->osversion));
	for (int i = 0; i < GATE_NCLS; i++) {
		const OSSymbol *name = OSSymbol::withCString(kGateRef.cls[i].name);
		if (!name)
			continue;
		const OSMetaClass *mc = OSMetaClass::getMetaClassWithName(name);
		name->release();
		if (!mc)
			continue;
		m->size[i] = (uint32_t)mc->getClassSize();
		const OSMetaClass *sup = mc->getSuperClass();
		if (sup && sup->getClassName())
			strlcpy(m->super[i], sup->getClassName(), GATE_NAMELEN);
	}
}

static void gatePublish(const GateMeasured *m, GateVerdict v, const char *why)
{
	if (!gLogTarget || !gLogLock)
		return;
	char line[224];
	snprintf(line, sizeof(line), "%s: %s", gateVerdictName(v), why);
	OSDictionary *fp = OSDictionary::withCapacity(2 * GATE_NCLS + 1);
	if (fp) {
		OSNumber *darwin = OSNumber::withNumber((uint64_t)m->darwinMajor, 32);
		if (darwin) {
			fp->setObject("darwin", darwin);
			darwin->release();
		}
		for (int i = 0; i < GATE_NCLS; i++) {
			char key[GATE_NAMELEN + 8];
			OSNumber *size = OSNumber::withNumber((uint64_t)m->size[i], 32);
			if (size) {
				fp->setObject(kGateRef.cls[i].name, size);
				size->release();
			}
			OSString *super = OSString::withCString(m->super[i]);
			if (super) {
				snprintf(key, sizeof(key), "%s.super", kGateRef.cls[i].name);
				fp->setObject(key, super);
				super->release();
			}
		}
	}
	IOLockLock(gLogLock);
	gLogTarget->setProperty("RTLAirGate", line);
	gLogTarget->setProperty("RTLAirGateBuild", m->osversion);
	if (fp)
		gLogTarget->setProperty("RTLAirFingerprint", fp);
	IOLockUnlock(gLogLock);
	if (fp)
		fp->release();
}

/* Decides once; later calls return the same answer. */
static bool airGateOpen()
{
	if (gGateDone)
		return gGateOpen;
	gGateDone = true;

	GateMeasured m;
	gateMeasure(&m);
	char trust[GATE_BUILDLEN] = {};
	PE_parse_boot_arg_str("rtlairtrust", trust, sizeof(trust));
	uint32_t force = 0;
	PE_parse_boot_argn("rtlairnogate", &force, sizeof(force));

	char why[160];
	GateVerdict v = gateDecide(&m, &kGateRef, kGateValidated, kGateValidatedCount,
	                           trust, force != 0, why, sizeof(why));
	gGateOpen = gateIsOpen(v) != 0;
	LOG("gate %s: %s", gateVerdictName(v), why);
	gatePublish(&m, v, why);
	return gGateOpen;
}

#endif /* RTL_VERSION_GATE_H */
