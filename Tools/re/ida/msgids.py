import sys, idapro
idapro.open_database(sys.argv[1], False)
import idautils, idc, ida_funcs, ida_name
targets = {"__ZN17IO80211Controller11postMessageEP23IO80211SkywalkInterfacejPvmb": "edx",
           "__ZN23IO80211SkywalkInterface11postMessageEjPvmb": "esi",
           "__ZN21IO80211InfraInterface11postMessageEjPvmb": "esi"}
out = {}
for t, reg in targets.items():
    ea = ida_name.get_name_ea(idc.BADADDR, t)
    for x in idautils.XrefsTo(ea):
        f = ida_funcs.get_func(x.frm)
        fn = ida_name.get_name(f.start_ea) if f else "?"
        # walk back up to 12 insns for mov reg, imm
        p, val, regs = x.frm, None, {}
        for _ in range(14):
            p = idc.prev_head(p)
            if p == idc.BADADDR: break
            if idc.print_insn_mnem(p) == "mov" and idc.print_operand(p, 0) in (reg, "r8d", "r8", "r9d") and idc.get_operand_type(p, 1) == idc.o_imm:
                regs.setdefault(idc.print_operand(p, 0), idc.get_operand_value(p, 1))
            if False:
                val = idc.get_operand_value(p, 1); break
        val = regs.get(reg); out.setdefault(val, set()).add("%s len=%s b=%s" % (fn, regs.get("r8d", regs.get("r8")), regs.get("r9d")))
for v in sorted(out, key=lambda v: -1 if v is None else v):
    names = sorted(out[v])
    print(v, len(names), " | ".join(n[:70] for n in names[:4]))
idapro.close_database(False)
