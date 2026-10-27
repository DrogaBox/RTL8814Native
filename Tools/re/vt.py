import struct,sys,glob,bisect,subprocess
kc=open('/System/Library/KernelCollections/BootKernelExtensions.kc','rb').read()
BASE=0xffffff8000100000
def lcs(off):
    ncmds=struct.unpack_from('<I',kc,off+16)[0]; p=off+32
    for i in range(ncmds):
        cmd,cs=struct.unpack_from('<II',kc,p); yield cmd,p,cs; p+=cs
segs=[]
def addsegs(off):
    for cmd,p,cs in lcs(off):
        if cmd==0x19:
            vm,vs,fo,fs=struct.unpack_from('<QQQQ',kc,p+24)
            if fs: segs.append((vm,vm+fs,fo))
addsegs(0)
for cmd,p,cs in lcs(0):
    if cmd==0x80000035:
        vm,fo,eo=struct.unpack_from('<QQI',kc,p+8); addsegs(fo)
def v2f(v):
    for a,b,fo in segs:
        if a<=v<b: return fo+(v-a)
syms={}
for f in glob.glob('syms_*.txt')+['syms.txt']:
    for l in open(f):
        a,t,n=l.split(None,2); syms[int(a,16)]=n.strip()
addrs=sorted(syms)
def name(v): return syms.get(v)
def dem(names):
    out=subprocess.run(['c++filt','-_'],input='\n'.join(names),capture_output=True,text=True).stdout.split('\n')
    return out
def vtable(cls):
    mang='__ZTV%d%s'%(len(cls),cls)
    va=[a for a,n in syms.items() if n==mang][0]
    i=bisect.bisect_right(addrs,va); end=addrs[i] if i<len(addrs) else va+0x4000
    fo=v2f(va); res=[]
    for k in range(2,(end-va)//8):
        q=struct.unpack_from('<Q',kc,fo+8*k)[0]
        if q==0: res.append((k-2,'0')); continue
        tgt=(q&0x3fffffff)+BASE if (q>>63)==0 and (q & ~((1<<51)-1) & ((1<<63)-1))==0 or True else q
        tgt=(q & 0x3fffffff)+BASE
        res.append((k-2,name(tgt) or hex(tgt)))
    return res
cls=sys.argv[1]
r=vtable(cls)
d=dem([n for _,n in r])
for (i,n),dn in zip(r,d): print('%3d 0x%03x %s'%(i,i*8,dn))
