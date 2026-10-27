/* Compile-only probe for Source/IO80211/IO80211_25_6_0.h: one subclass of each
 * class a driver derives from, every pure virtual stubbed. check_io80211.sh
 * compares the emitted vtables with the KC dumps slot by slot. */
#include "IO80211_25_6_0.h"

class LayoutInfra : public IO80211InfraProtocol {
	OSDeclareDefaultStructors(LayoutInfra)
public:
#include "IO80211_25_6_0_infra_decl.inc"
};
OSDefineMetaClassAndStructors(LayoutInfra, IO80211InfraProtocol)
#define STUB_CLASS LayoutInfra
#include "IO80211_25_6_0_infra_stubs.inc"
#undef STUB_CLASS

class LayoutCtrl : public IO80211Controller {
	OSDeclareDefaultStructors(LayoutCtrl)
public:
#include "IO80211_25_6_0_ctrl_decl.inc"
};
OSDefineMetaClassAndStructors(LayoutCtrl, IO80211Controller)
#define STUB_CLASS LayoutCtrl
#include "IO80211_25_6_0_ctrl_stubs.inc"
#undef STUB_CLASS
