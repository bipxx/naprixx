"""Finite source-line controls for native grid getter and comprehension frames."""
import ctypes
import hashlib
import json
from pathlib import Path
import sys

from grid_comprehension_child import deny_archive
import grid_getitem_locals_child as getter
from grid_line_transport import LineProbe
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]
MODES=('sparse','dense','complex-grid','empty-sparse','empty-dense','mutate-result',
       'mutate-slice','mutate-key','mutate-inline','trace-mutation','trace-lines-off','trace-disable','iterator-forward')
if sys.version_info>=(3,12): MODES+=('restore',)


def run(probe,code,shells,mode,native):
    events=[]; changed=False; wrapper=LineProbe(probe)
    owners={(item.co_filename,item.co_qualname) for item in walk(code)}
    def trace(frame,event,arg):
        nonlocal changed
        if (frame.f_code.co_filename,frame.f_code.co_qualname) not in owners: return None
        assert sys.gettrace() is trace
        events.append((event,frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti,tuple(sorted(frame.f_locals))))
        if event=='line' and not changed:
            if mode=='trace-disable': changed=True;sys.settrace(None)
            elif mode=='trace-lines-off': changed=True;frame.f_trace_lines=False
            elif mode=='trace-mutation' and frame.f_lineno==167:
                values=frame.f_locals; values['size']=[3,4];values['num_list']=[9]
                if sys.version_info<(3,13):
                    sync=ctypes.pythonapi.PyFrame_LocalsToFast
                    sync.argtypes,sync.restype=[ctypes.py_object,ctypes.c_int],None
                    sync(frame,1)
                changed=True
        return trace
    sys.settrace(trace)
    try:
        result=getter.run(wrapper if native else probe,code,shells,
            'sparse' if mode.startswith('trace-') else mode,native,invoke=wrapper.invoke if native else None)
        assert sys.gettrace() is (None if mode=='trace-disable' else trace)
    finally: sys.settrace(None)
    if mode.startswith('trace-'): assert changed,mode
    return result,events


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    for mode in MODES:
        try:
            before=run(probe,code,shells,mode,False);after=run(probe,code,shells,mode,True)
        except BaseException:
            print('CONTROL_FAILED',mode,flush=True);raise
        assert before==after,(mode,before,after)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_GETTER_LINES','controls':len(MODES),'python':sys.version,
        'handled_exception_opcode_and_full_frame_parity_accepted':False,'default_application_enabled':False}))


if __name__=='__main__': main()
