# Decompile the whole binary once, write per-function C into a dir
import sys,angr,logging,os,time,pickle
for n in ['angr','cle','pyvex','claripy']: logging.getLogger(n).setLevel(logging.CRITICAL)
f=sys.argv[1]; out=sys.argv[2]; os.makedirs(out,exist_ok=True)
p=angr.Project(f,auto_load_libs=False)
cfg=p.analyses.CFGFast(normalize=True,data_references=True,show_progressbar=False)
fns=sorted(cfg.kb.functions.values(),key=lambda x:x.addr)
# write call graph + string refs index
with open(out+'/_index.txt','w') as idx:
    for fn in fns:
        idx.write(f"{fn.addr:08x} {fn.name} size={fn.size} calls={[hex(c.addr) for c in fn.functions_called()][:40] if hasattr(fn,'functions_called') else ''}\n")
lo=int(sys.argv[3],16) if len(sys.argv)>3 else 0; hi=int(sys.argv[4],16) if len(sys.argv)>4 else 1<<32
t=time.time();n=0
for fn in fns:
    if not (lo<=fn.addr<hi) or fn.is_simprocedure or fn.is_plt: continue
    path=f"{out}/{fn.addr:08x}.c"
    if os.path.exists(path): continue
    try:
        d=p.analyses.Decompiler(fn,cfg=cfg.model)
        txt=d.codegen.text if d.codegen else '// no codegen'
    except Exception as e: txt='// ERR '+repr(e)[:200]
    open(path,'w').write(txt); n+=1
    if n%100==0: print(n,time.time()-t,flush=True)
print('done',n)
