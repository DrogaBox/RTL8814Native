import sys, time, os
import idapro
OUT = sys.argv[2]
names = [l.strip() for l in open(sys.argv[3]) if l.strip()]
t = time.time()
rc = idapro.open_database(sys.argv[1], True)
print("open rc", rc, "t", time.time() - t, flush=True)
import ida_auto, ida_name, ida_hexrays, ida_funcs, idc, idaapi
ida_auto.auto_wait()
print("analysis done t", time.time() - t, flush=True)
ok = ida_hexrays.init_hexrays_plugin()
print("hexrays", ok, flush=True)
os.makedirs(OUT, exist_ok=True)
for n in names:
    ea = ida_name.get_name_ea(idaapi.BADADDR, n)
    if ea == idaapi.BADADDR:
        print("noname", n, flush=True); continue
    f = ida_funcs.get_func(ea)
    if not f:
        ida_funcs.add_func(ea)
    try:
        c = ida_hexrays.decompile(ea)
        txt = str(c)
    except Exception as e:
        txt = "ERR %s" % e
    open(os.path.join(OUT, n[:120] + ".c"), "w").write(txt)
    print("ok", n, len(txt), flush=True)
idapro.close_database(True)
print("done t", time.time() - t, flush=True)
