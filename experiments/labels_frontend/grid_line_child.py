"""Finite source-line comparisons for opt-in native indices transport."""
import ctypes
import dis
import hashlib
import json
from pathlib import Path
import sys

from grid_comprehension_child import deny_archive
import grid_indices_locals_child as indices
from grid_line_transport import LineProbe
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]
MODES=('dense','sparse','empty-dense','empty-sparse','mutate-len','mutate-arange',
       'reshape-lookup','mutate-reshape','trace-mutation','trace-lines-off',
       'trace-disable','trace-error')


def run(probe,code,shell,mode,native,opcodes=False):
    events=[]; changed=False
    wrapper=LineProbe(probe,opcodes=opcodes)
    operations={item.offset:item for item in dis.get_instructions(code)}
    first=next(line for _,_,line in code.co_lines() if line and line>code.co_firstlineno)
    def trace(frame,event,arg):
        nonlocal changed
        if frame.f_code.co_filename!=code.co_filename or frame.f_code.co_qualname!=code.co_qualname:
            return None
        assert sys.gettrace() is (None if mode=='trace-disable' and changed else trace)
        if event=='call':
            frame.f_trace=trace
            frame.f_trace_opcodes=opcodes
        events.append((event,frame.f_lineno,frame.f_lasti,tuple(sorted(frame.f_locals))))
        if event=='line' and not changed:
            if mode=='trace-error' and frame.f_lineno==first+1:
                changed=True
                raise RuntimeError('trace callback failure')
            if mode=='trace-disable':
                sys.settrace(None); changed=True
            elif mode=='trace-lines-off':
                frame.f_trace_lines=False; changed=True
            elif mode=='trace-mutation' and frame.f_lineno==first+7:
                values=frame.f_locals; values['dimensions']=(4,); values['dtype']=float
                if sys.version_info<(3,13):
                    sync=ctypes.pythonapi.PyFrame_LocalsToFast
                    sync.argtypes,sync.restype=[ctypes.py_object,ctypes.c_int],None
                    sync(frame,1)
                changed=True
        if event=='opcode' and not changed and mode in ('opcode-load','opcode-call','opcode-store','opcode-fused-load'):
            operation=operations[frame.f_lasti]
            selected=(mode=='opcode-load' and (
                operation.opname in ('LOAD_FAST','LOAD_FAST_CHECK') and operation.argval=='dtype' or
                operation.opname=='LOAD_FAST_LOAD_FAST' and 'dtype' in operation.argrepr.split(', '))) or (
                mode=='opcode-call' and operation.opname in ('CALL','CALL_KW') and frame.f_lineno==first+1) or (
                mode=='opcode-store' and operation.opname=='STORE_FAST' and operation.argval=='N') or (
                mode=='opcode-fused-load' and operation.opname=='LOAD_FAST_LOAD_FAST' and 'dtype' in operation.argrepr.split(', '))
            if selected:
                values=frame.f_locals
                if mode in ('opcode-load','opcode-fused-load'): values['dtype']=float
                elif mode=='opcode-call': values['dimensions']=(4,)
                else: values['N']=9
                if sys.version_info<(3,13):
                    sync=ctypes.pythonapi.PyFrame_LocalsToFast
                    sync.argtypes,sync.restype=[ctypes.py_object,ctypes.c_int],None
                    sync(frame,1)
                changed=True
        return trace
    controller=sys._getframe(); previous_opcodes=controller.f_trace_opcodes
    controller.f_trace_opcodes=opcodes
    sys.settrace(trace)
    try:
        try:
            result=indices.run(wrapper if native else probe,code,shell,
                'dense' if mode.startswith(('trace-','opcode-')) else mode,native,
                invoke=wrapper.invoke if native else None)
        except RuntimeError as error:
            assert mode=='trace-error'
            result=('raise',type(error).__name__,str(error))
        if mode in ('trace-disable','trace-error'): assert sys.gettrace() is None
        else: assert sys.gettrace() is trace
    finally:
        sys.settrace(None)
        controller.f_trace_opcodes=previous_opcodes
    if mode.startswith(('trace-','opcode-')): assert changed,mode
    return result,events


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][1]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path']; data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={}; path=DIRECTORY/'grid_shells.py'
    exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    for mode in MODES:
        before=run(probe,code,shells['_grid_indices'],mode,False)
        after=run(probe,code,shells['_grid_indices'],mode,True)
        assert before==after,(mode,before,after)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_INDICES_LINES','controls':len(MODES),'python':sys.version,
        'opcode_and_jump_parity_accepted':False,'default_application_enabled':False}))


if __name__=='__main__': main()
