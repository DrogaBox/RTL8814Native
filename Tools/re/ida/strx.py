import sys, idapro
idapro.open_database(sys.argv[1], False)
import idautils, idc, ida_funcs, ida_name, ida_bytes
lo, hi = 0xffffff8002100000, 0xffffff8002600000
pat = sys.argv[2].encode() + b"\0"
ea = lo
while True:
    ea = ida_bytes.find_bytes(pat, ea, range_end=hi)
    if ea == idc.BADADDR: break
    if ida_bytes.get_byte(ea - 1) == 0:
        print("str at %x" % ea)
        for x in idautils.XrefsTo(ea):
            f = ida_funcs.get_func(x.frm)
            print("   xref %x %s" % (x.frm, ida_name.get_name(f.start_ea) if f else "?"))
    ea += 1
idapro.close_database(False)
