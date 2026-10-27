#!/usr/bin/env python3
"""
Generate a C++ header declaring the private IO80211 / Skywalk class chain of
one macOS build, slot-exact, from the vtable dumps made by vt.py.

  python3 gen_io80211.py <dump_dir> <out.h>

<dump_dir> holds vt_<Class>.txt for every class in CHAIN, their SDK parents
(IOService, IOEthernetController) and the Apple implementation classes used
to name pure-virtual slots. Each class declares every virtual its own vtable
owns (overrides of SDK virtuals included), in slot order, so a subclass of
ours binds each slot to the same function Apple's vtable has. Pure virtual
slots take their name and signature from Apple's implementation.

Also writes <out>_ctrl_decl.inc / _ctrl_stubs.inc and _infra_decl.inc /
_infra_stubs.inc: the pure virtuals a driver must implement, as class-body
declarations and kIOReturnUnsupported bodies (define STUB_CLASS first).

Return types are not in the mangling. Overrides of SDK virtuals take the
SDK's return type; get*/set* apple80211 handlers return IOReturn; anything
else is uintptr_t (register-wide, safe for bool/int/pointer) unless
RET_OVERRIDES says otherwise. Fix the ones you call or implement there.

Copyright (c) 2026 DrogaBox — GPL-2.0
"""
import glob
import re
import subprocess
import sys

# (class, parent, implementation class naming its pure slots)
CHAIN = [
    ("IOSkywalkInterface", "IOService", "AppleBCMWLANSkywalkInterface"),
    ("IOSkywalkNetworkInterface", "IOSkywalkInterface", "AppleBCMWLANSkywalkInterface"),
    ("IOSkywalkEthernetInterface", "IOSkywalkNetworkInterface", "AppleBCMWLANSkywalkInterface"),
    ("IO80211SkywalkInterface", "IOSkywalkEthernetInterface", "AppleBCMWLANSkywalkInterface"),
    ("IO80211InfraInterface", "IO80211SkywalkInterface", "AppleBCMWLANSkywalkInterface"),
    ("IO80211InfraProtocol", "IO80211InfraInterface", "AppleBCMWLANSkywalkInterface"),
    ("IO80211Controller", "IOEthernetController", "AppleBCMWLANCore"),
]
GENERATED = {c for c, _, _ in CHAIN} | {"IO80211WorkQueue"}

# Instance sizes from each class's OSMetaClass constructor in the 25.6.0 KC
# (OSMetaClass(name, super, size)). The private ivars are padding here: a
# subclass's members must start after them, and OSTypeAlloc must allocate the
# full object. SDK parents: IOService 0x88, IOEthernetController 0x118.
SIZES = {
    "IOService": 0x88, "IOEthernetController": 0x118,
    "IOSkywalkInterface": 0xB0, "IOSkywalkNetworkInterface": 0xE0,
    "IOSkywalkEthernetInterface": 0x120, "IO80211SkywalkInterface": 0x128,
    "IO80211InfraInterface": 0x130, "IO80211InfraProtocol": 0x130,
    "IO80211Controller": 0x128,
}
SDK_PARENT_OF = {"IO80211Controller": "IOEthernetController"}   # others: IOService

RET_OVERRIDES = {
    "isCommandProhibited": "bool",
    "getLogger": "void *",
    "getFaultReporterFromDriver": "void *",
    "getWorkQueue": "IO80211WorkQueue *",
    "getIO80211CommandGate": "IOCommandGate *",
    "getPrimarySkywalkInterface": "IO80211SkywalkInterface *",
}

BUILTIN = {"void", "bool", "char", "short", "int", "long", "unsigned", "signed", "const",
           "float", "double", "va_list"}
# by-value parameters that are structs, not enums (mangling is the same; the ABI is not)
BYVALUE_STRUCTS = {"apple80211_channel", "nicproxy_limits_info_s", "RegistrationInfo",
                   "IOSkywalkTSOOptions"}
# types the SDK headers already declare
SDK_TYPES = {"IOService", "IOEthernetController", "IOCommandGate", "IOWorkLoop", "OSString",
             "OSObject", "OSDictionary", "OSData", "OSSymbol", "OSArray", "IOReportChannelList",
             "IONetworkInterface", "IONetworkMedium", "IOMemoryDescriptor", "IOUserClient",
             "IOEthernetAddress", "IOKernelDebugger", "IOMbufQueue", "IOPacketBufferConstraints",
             "IOPMPowerState", "IOTimerEventSource", "IOEventSource", "IOReturn", "UInt32",
             "UInt64", "IOOptionBits", "OSMetaClassBase", "OSMetaClass", "OSNumber", "OSBoolean",
             "IORegistryPlane", "task", "IOPMPowerFlags", "IOPMrootDomain", "IONotifier",
             "IOInterruptEventSource", "IORegistryEntry", "OSIterator", "OSSet", "OSCollection",
             "IOInterruptController", "IOMapper", "IODeviceMemory", "IOMemoryMap"}


def load(dump, cls):
    rows = []
    for line in open("%s/vt_%s.txt" % (dump, cls)):
        idx, _off, name = line.rstrip("\n").split(None, 2)
        rows.append((int(idx), name))
    if rows and rows[-1][1] == "0":
        rows.pop()                      # vtable terminator
    return rows


def split_sig(name):
    """'Cls::meth(args) const' -> (owner, meth, args, const)"""
    m = re.match(r"([A-Za-z0-9_]+)::(~?[A-Za-z0-9_]+)\((.*)\)( const)?$", name)
    if not m:
        return None
    return m.group(1), m.group(2), m.group(3).replace("__va_list_tag*", "va_list"), bool(m.group(4))


SDK_RET = {}


def load_sdk_returns():
    """virtual <ret> <name>( ... ) from the Kernel.framework headers we build with"""
    sdk = subprocess.run(["xcrun", "--sdk", "macosx", "--show-sdk-path"],
                         capture_output=True, text=True).stdout.strip()
    k = sdk + "/System/Library/Frameworks/Kernel.framework/Headers"
    files = [k + "/IOKit/IOService.h", k + "/IOKit/IORegistryEntry.h",
             k + "/libkern/c++/OSObject.h", k + "/libkern/c++/OSMetaClass.h"]
    files += glob.glob(k + "/IOKit/network/*.h")
    pat = re.compile(r"virtual\s+([A-Za-z_][\w:]*(?:\s+[A-Za-z_][\w:]*)*?)\s*(\**)\s*([A-Za-z_]\w*)\s*\(")
    for f in files:
        try:
            txt = open(f).read()
        except OSError:
            continue
        for m in pat.finditer(txt):
            ret = re.sub(r"\s+", " ", m.group(1) + " " + m.group(2)).strip()
            SDK_RET.setdefault(m.group(3), ret)


def ret_type(meth, sdk_origin):
    if sdk_origin:
        if meth not in SDK_RET:
            raise SystemExit("no SDK return type for %s" % meth)
        return SDK_RET[meth]
    if meth in RET_OVERRIDES:
        return RET_OVERRIDES[meth]
    if re.match(r"(get|set)[A-Z0-9_]+$", meth):
        return "IOReturn"
    return "uintptr_t"


class Types:
    def __init__(self):
        self.top = {}       # name -> by value
        self.nested = {}    # outer -> {inner: by value}

    def scan(self, args):
        for param in [p.strip() for p in args.split(",") if p.strip()]:
            byval = "*" not in param and "&" not in param
            for tok in re.findall(r"[A-Za-z_][A-Za-z0-9_:]*", param):
                if tok in BUILTIN:
                    continue
                if "::" in tok:
                    outer, inner = tok.split("::", 1)
                    if outer in GENERATED:
                        self.nested.setdefault(outer, {})[inner] = byval
                    continue
                if tok in SDK_TYPES or tok in GENERATED:
                    continue
                self.top[tok] = self.top.get(tok, False) or byval


def decl_type(name, byval):
    if byval and name not in BYVALUE_STRUCTS:
        return "enum %s : uint32_t {};  /* by value: assumed a 32-bit enum */" % name
    return "struct %s;" % name


def main():
    dump, out = sys.argv[1], sys.argv[2]
    load_sdk_returns()
    sdk_slots = {"IOService": len(load(dump, "IOService")),
                 "IOEthernetController": len(load(dump, "IOEthernetController"))}
    types = Types()
    pure = {"ctrl": [], "infra": []}
    classes = []

    for cls, parent, impl in CHAIN:
        rows = load(dump, cls)
        npar = len(load(dump, parent))
        nsdk = sdk_slots[SDK_PARENT_OF.get(cls, "IOService")]
        impl_rows = dict(load(dump, impl))
        decls = []
        for idx, name in rows:
            if "pure_virtual" in name:
                if idx < npar:
                    continue            # still pure from the parent, declared there
                src = impl_rows.get(idx)
                if not src or "pure_virtual" in src:
                    raise SystemExit("%s slot %d: pure virtual with no implementation name" % (cls, idx))
                _, meth, args, const = split_sig(src)
                types.scan(args)
                rt = ret_type(meth, False)
                decls.append((idx, "virtual %s %s(%s)%s = 0;" % (rt, meth, args, " const" if const else "")))
                pure["ctrl" if cls == "IO80211Controller" else "infra"].append((idx, rt, meth, args, const))
                continue
            sig = split_sig(name)
            if not sig:
                continue
            owner, meth, args, const = sig
            if owner != cls or meth.startswith("~") or meth == "getMetaClass":
                continue
            # Overrides are redeclared here, or a subclass of ours would bind
            # the slot to the parent's version instead of Apple's.
            types.scan(args)
            rt = ret_type(meth, idx < nsdk)
            kw = " override" if idx < npar else ""
            decls.append((idx, "virtual %s %s(%s)%s%s;" % (rt, meth, args, " const" if const else "", kw)))
        classes.append((cls, parent, decls))

    hdr = ["/* AUTO-GENERATED by Tools/re/gen_io80211.py from the vtable dumps in",
           " * %s. Do not edit by hand: fix the generator and regenerate." % dump,
           " * Slot layout is build-specific; verify with Tools/re/check_io80211.sh. */",
           "#pragma once",
           "#include <IOKit/IOService.h>",
           "#include <IOKit/IOCommandGate.h>",
           "#include <IOKit/IOWorkLoop.h>",
           "#include <IOKit/network/IOEthernetController.h>",
           "#include <stdint.h>",
           "#include <stdarg.h>",
           ""]
    for c, _, _ in CHAIN:
        hdr.append("class %s;" % c)
    hdr.append("class IO80211WorkQueue;")
    for t in sorted(types.top):
        hdr.append(decl_type(t, types.top[t]))
    hdr.append("")
    for cls, parent, decls in classes:
        hdr.append("class %s : public %s {" % (cls, parent))
        hdr.append("    OSDeclareAbstractStructors(%s)" % cls)
        hdr.append("public:")
        for inner, byval in sorted(types.nested.get(cls, {}).items()):
            hdr.append("    " + decl_type(inner, byval))
        for idx, d in decls:
            hdr.append("    %s  // slot %d" % (d, idx))
        pad = SIZES[cls] - SIZES[parent]
        if pad:
            hdr.append("private:")
            hdr.append("    uint8_t _rtlPad%s[0x%x];  // KC size 0x%x" % (cls, pad, SIZES[cls]))
        hdr.append("};")
        hdr.append("static_assert(sizeof(%s) == 0x%x, \"%s size\");" % (cls, SIZES[cls], cls))
        hdr.append("")
    open(out, "w").write("\n".join(hdr))

    # Only slots still pure in the class a driver derives from need a body;
    # a slot pure in an ancestor but implemented further down keeps Apple's code.
    for key, final in (("infra", "IO80211InfraProtocol"), ("ctrl", "IO80211Controller")):
        still = {i for i, n in load(dump, final) if "pure_virtual" in n}
        dropped = [m for i, _, m, _, _ in pure[key] if i not in still]
        if dropped:
            print("%s: implemented below the declaring class, not stubbed: %s" % (key, ", ".join(dropped)))
        pure[key] = [p for p in pure[key] if p[0] in still]

    base = out.rsplit(".", 1)[0]
    for key, items in pure.items():
        decl = ["/* AUTO-GENERATED by gen_io80211.py: pure virtuals to implement (%s) */" % key]
        defs = ["/* AUTO-GENERATED by gen_io80211.py: stub bodies; define STUB_CLASS and STUB_ENTER() first */"]
        for idx, rt, meth, args, const in items:
            c = " const" if const else ""
            decl.append("virtual %s %s(%s)%s override;  // slot %d" % (rt, meth, args, c, idx))
            body = {"IOReturn": "return kIOReturnUnsupported;", "bool": "return false;"}.get(rt, "return 0;")
            defs.append("%s STUB_CLASS::%s(%s)%s { STUB_ENTER(); %s }" % (rt, meth, args, c, body))
        open("%s_%s_decl.inc" % (base, key), "w").write("\n".join(decl) + "\n")
        open("%s_%s_stubs.inc" % (base, key), "w").write("\n".join(defs) + "\n")
    print("wrote %s: %d classes, %d top-level types, pure: ctrl %d infra %d" %
          (out, len(classes), len(types.top), len(pure["ctrl"]), len(pure["infra"])))


if __name__ == "__main__":
    main()
