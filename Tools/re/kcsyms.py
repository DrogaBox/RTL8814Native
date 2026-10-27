import struct,sys
kc=open(sys.argv[1],'rb').read()
entry=sys.argv[2]
def lcs(off):
    magic,cpu,sub,ft,ncmds,sz,flags,res=struct.unpack_from('<IiiIIIII',kc,off)
    p=off+32
    for i in range(ncmds):
        cmd,cs=struct.unpack_from('<II',kc,p); yield cmd,p,cs; p+=cs
# find entry
for cmd,p,cs in lcs(0):
    if cmd==0x80000035:
        vm,fo,eo=struct.unpack_from('<QQI',kc,p+8)
        name=kc[p+eo:p+cs].split(b'\0')[0].decode()
        if name==entry: base=fo; break
segs=[]
for cmd,p,cs in lcs(base):
    if cmd==0x19:
        segname=kc[p+8:p+24].rstrip(b'\0').decode(); vm,vs,fo,fs=struct.unpack_from('<QQQQ',kc,p+24)
        segs.append((segname,vm,vs,fo,fs)); print('SEG',segname,hex(vm),hex(vs),hex(fo),hex(fs),file=sys.stderr)
    if cmd==2:
        symoff,nsyms,stroff,strsize=struct.unpack_from('<IIII',kc,p+8)
        print('SYMTAB',nsyms,file=sys.stderr)
        for i in range(nsyms):
            sx,ty,sect,desc,val=struct.unpack_from('<IBBHQ',kc,symoff+16*i)
            n=kc[stroff+sx:kc.index(b'\0',stroff+sx)].decode('latin1')
            print('%016x %02x %s'%(val,ty,n))
