import sys, time, os
import idapro
OUT = sys.argv[2]
names = [l.strip() for l in open(sys.argv[3]) if l.strip()]
rc = idapro.open_database(sys.argv[1], False)
import ida_name, ida_hexrays, ida_funcs, idaapi, idautils, idc
ida_hexrays.init_hexrays_plugin()
os.makedirs(OUT, exist_ok=True)
for n in names:
    xr = n.startswith("XREF:")
    if xr: n = n[5:]
    ea = ida_name.get_name_ea(idaapi.BADADDR, n)
    if ea == idaapi.BADADDR:
        print("noname", n, flush=True); continue
    if xr:
        callers = sorted({ida_funcs.get_func(x.frm).start_ea for x in idautils.XrefsTo(ea) if ida_funcs.get_func(x.frm)})
        lines = ["%x %s" % (c, ida_name.get_name(c)) for c in callers]
        open(os.path.join(OUT, "xref_" + n[:110] + ".txt"), "w").write("\n".join(lines) + "\n")
        print("xref", n, len(callers), flush=True); continue
    if not ida_funcs.get_func(ea):
        ida_funcs.add_func(ea)
    try:
        txt = str(ida_hexrays.decompile(ea))
    except Exception as e:
        txt = "ERR %s" % e
    open(os.path.join(OUT, n[:120] + ".c"), "w").write(txt)
    print("ok", n, len(txt), flush=True)
idapro.close_database(True)
print("done", flush=True)
