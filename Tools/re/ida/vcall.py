import sys, idapro
idapro.open_database(sys.argv[1], False)
import idautils, idc, ida_funcs, ida_name, ida_ua, ida_segment
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
disps = [int(x, 16) for x in sys.argv[4].split(",")]
seen = {}
for f in idautils.Functions(lo, hi):
    for ea in idautils.FuncItems(f):
        if idc.print_insn_mnem(ea) not in ("call", "jmp"): continue
        if idc.get_operand_type(ea, 0) != idc.o_displ: continue
        d = idc.get_operand_value(ea, 0)
        if d in disps:
            seen.setdefault((d, ida_name.get_name(f)), []).append(ea)
for (d, n), eas in sorted(seen.items()):
    print("%x %s %s" % (d, n, " ".join("%x" % e for e in eas)))
idapro.close_database(False)
