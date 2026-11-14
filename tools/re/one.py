import sys,angr,logging
for n in ['angr','cle','pyvex','claripy']: logging.getLogger(n).setLevel(logging.CRITICAL)
p=angr.Project(sys.argv[1],auto_load_libs=False)
cfg=p.analyses.CFGFast(normalize=True,data_references=True,show_progressbar=False)
import code
todo=[int(a,16) for a in sys.argv[2:]]
for a in todo:
    fn=cfg.kb.functions.get(a)
    if fn is None: print('nofunc',hex(a)); continue
    print('=====',hex(a),'calls',[hex(c.addr) for c in fn.functions_called()] if hasattr(fn,'functions_called') else '')
    try:
        d=p.analyses.Decompiler(fn,cfg=cfg.model); print(d.codegen.text)
    except Exception as e: print('ERR',e)
