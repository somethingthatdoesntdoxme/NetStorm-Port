import sys,pefile,capstone,struct
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
img=pe.get_memory_mapped_image()
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
lo=base+text.VirtualAddress; code=img[text.VirtualAddress:text.VirtualAddress+text.Misc_VirtualSize]
targets=[int(a,16) for a in sys.argv[2:]]
for i in range(len(code)-5):
    if code[i]==0xE8:
        t=(lo+i+5+struct.unpack_from('<i',code,i+1)[0])&0xffffffff
        if t in targets: print(hex(t),'called from',hex(lo+i))
