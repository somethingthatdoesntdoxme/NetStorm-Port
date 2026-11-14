import sys,struct,pefile,re
f=sys.argv[1]; pe=pefile.PE(f); base=pe.OPTIONAL_HEADER.ImageBase
b=open(f,'rb').read()
def va(off):
    for s in pe.sections:
        if s.PointerToRawData<=off<s.PointerToRawData+s.SizeOfRawData: return base+s.VirtualAddress+off-s.PointerToRawData
for a in sys.argv[2:]:
    t=struct.pack('<I',int(a,16))
    print(a,[hex(va(m.start())) for m in re.finditer(re.escape(t),b)][:20])
