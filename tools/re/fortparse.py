import struct,re,glob,sys,pefile
TARC='/home/claude/work/tarc/d/'
F1={'carribleinvehicle':0x10000,'container':0x20000,'defaulthotspot':1,'shadow':0x40000,'notselectable':0x8000000,
'createsisland':0x400,'maydroponrim':4,'maydroponisle':2,'surface':0x800,'randframe':0x100000,'matchframe':0x200000,
'opaquecollide':0x800000,'dontsave':0x20000000,'saveframe':0x20,'saveqa':0x1000,'saveqb':0x2000}
F2={'notreal':0x2000000,'dropblocking':0x10,'blocking':0x18,'shotblocking':0x20,'fencepost':1,'fence':0x20000000,'buried':0x2000,
'nugget':0x8000,'bomb':0x100,'emplacement':0x40000,'bridge':4,'walker':0x10000,'balloon':0x20000,'island':2,
'islandthreebythree':0x1000000,'factory':0x4000,'vortex':0x200,'edgefarm':0x80000,'guy':0x400,'geyser':0x10000000,
'flyer':0x100000,'residence':0x40000000,'fringe':0x1000,'priest':0x200000,'dais':0x400000,'altar':0x80000000}
def h(name):
    v=0
    for j,c in enumerate(name.encode()): v=(v+(c<<((j&3)*8)))&0xffffffff
    return v
GROUPS={'archer':0,'cannon':1,'blocker':2,'aviary':3,'flyer':4,'battery':5,'fence':6,'walker':7,'balloon':8,'misc':9,'anti-air':10}
# type flags from scripts
types={}
for f in glob.glob(TARC+'*.type'):
    t=re.sub(r'//.*','',open(f,encoding='latin1').read())
    m=re.search(r'typename\s+(\S+)',t); fl=re.search(r'typeflags([^;]*);',t)
    words=[w.lower().replace('_','') for w in (fl.group(1).split() if fl else [])]
    f1=0;f2=0
    for w in words: f1|=F1.get(w,0); f2|=F2.get(w,0)
    g=re.search(r'group\s*=\s*"?([A-Za-z-]+)',t)
    grp=GROUPS.get(g.group(1).lower(),11) if g else 11
    if grp==5: f2|=0x800
    if grp in (0,1): f2|=0x8000000
    if grp==2: f2|=0x4000000
    if f2&0x34200: f1|=0x20000
    if f2&0x2000: f1|=0x1000
    types[m.group(1).lower()]=(f1,f2,words)
# candidate names: all strings from binary + type names
pe=pefile.PE('/home/claude/ns_extract/Netstorm/netstorm.game'); img=pe.get_memory_mapped_image()
cands=set(x.decode() for x in re.findall(rb'[A-Za-z][A-Za-z0-9_]{1,30}',img))|set(types)
H={}
for c in sorted(set(types)): H[h(c)]=c
for c in cands: H.setdefault(h(c),c)
def sections(b):
    p=2;out=[]
    while p+2<=len(b):
        L=struct.unpack_from('<H',b,p)[0]
        if L<2 or p+L>len(b): break
        out.append(b[p+2:p+L]); p+=L
    return out
class R:
    def __init__(s,d): s.d=d; s.p=0
    def u8(s): v=s.d[s.p]; s.p+=1; return v
    def u16(s): v=struct.unpack_from('<H',s.d,s.p)[0]; s.p+=2; return v
def parse(fn, verbose=False):
    b=open(fn,'rb').read(); S=sections(b)
    tn=S[7]; n=tn[0]; hashes=struct.unpack_from('<%dI'%n,tn,1)
    names=[H.get(x,'?%08x'%x) for x in hashes]
    def readobj(r,cx,cy,ver,out,depth=0):
        pos=r.u8(); t=r.u8()
        if t>=246:
            o={'type':'TERRAIN','player':255-t,'x':cx*16+(pos>>4),'y':cy*16+(pos&15),'v':r.u8()}
            out.append(o); return
        else:
            name=names[t] if t<len(names) else '?id%d'%t
            f1,f2,_=types.get(name.lower(),(0,0,[]))
        if t==0 or name in ('?',):
            r.p+=1; return
        o={'type':name,'x':cx*16+(pos>>4),'y':cy*16+(pos&15)}
        if f1&0x20: o['frame']=r.u8()
        if f1&0x1000: o['qa']=r.u8()
        if f1&0x2000: o['qb']=r.u16()
        if f2&4: o['bridge']=r.u8()
        if ver>=2 and f2&0x4000: o['fac']=r.u8()
        if ver>0 and f2&0x5d77cf00: o['owner']=r.u8()
        if f1&0x20000:
            k=r.u8(); o['contents']=[]
            for _ in range(k):
                t2=r.u8(); n2=names[t2] if t2<246 and t2<len(names) else 'P'
                g1,g2,_=types.get(n2.lower(),(0,0,[]))
                c={'type':n2}
                if g1&0x1000: c['qa']=r.u8()
                if g1&0x2000: c['qb']=r.u16()
                if not (g2&0x100): c['b']=r.u8()  # carried-object flags byte (not for bombs)
                o['contents'].append(c)
        out.append(o)
    def terr(sec):
        r=R(sec); ver=r.u8(); chunks=[]
        while r.p<len(sec):
            m=r.u8()
            if m!=99: chunks.append(('bad',m,r.p)); break
            cnt=r.u16(); objs=[]
            for _ in range(cnt): readobj(r,0,0,ver,objs)
            chunks.append(objs)
        return ver,chunks
    return S,names,terr
if __name__=='__main__':
    S,names,terr=parse(TARC+sys.argv[1])
    print('unknown names',[ (i,n) for i,n in enumerate(names) if n.startswith('?')][:20])
    for si in [4]+list(range(15,35)):
        ver,ch=terr(S[si])
        nobj=sum(len(c) for c in ch if isinstance(c,list))
        bad=[c for c in ch if not isinstance(c,list)]
        if nobj or bad: print('sec',si,'chunks',len(ch),'objs',nobj,'bad',bad[:1], [o['type'] for c in ch if isinstance(c,list) for o in c][:15])
