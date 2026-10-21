"""Compare source positions visible inside foreign provider callbacks."""
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
MODES=('sparse','dense','complex-grid','single','complex-single','mutate-result','mutate-key','iterator-forward')


def run(probe,code,shells,mode,native,tracing,warm=False):
    wrapper=LineProbe(probe,opcodes=True);positions=[]
    owners={(item.co_filename,item.co_qualname) for item in walk(code)}
    def trace(frame,event,arg):
        if (frame.f_code.co_filename,frame.f_code.co_qualname) in owners:
            if event=='call':frame.f_trace=trace;frame.f_trace_opcodes=True
            return trace
        return None
    def observe(event,frame):
        positions.append((event,frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti))
    def invoke(function,args):
        for _ in range(8 if warm else 1):
            result=wrapper.invoke(function,args) if native else function(*args)
        return result
    controller=sys._getframe();previous=controller.f_trace_opcodes;controller.f_trace_opcodes=tracing
    if tracing:sys.settrace(trace)
    try:
        result=getter.run(wrapper if native else probe,code,shells,mode,native,
                          invoke=invoke,observe=observe)
        assert sys.gettrace() is (trace if tracing else None)
    finally:sys.settrace(None);controller.f_trace_opcodes=previous
    assert positions
    return result,positions


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    source_path=path
    def fresh():return next(item for item in walk(compile(data,str(source_path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    rows=[]
    for tracing in ((True,) if '--traced-only' in sys.argv else (False,True)):
        for warm in (False,True):
            for mode in MODES:
                before=run(probe,fresh(),shells,mode,False,tracing,warm);after=run(probe,fresh(),shells,mode,True,tracing,warm)
                rows.append({'mode':mode,'opcode_tracing':tracing,'invocations':8 if warm else 1,
                             'policy_values_equal':before[0]==after[0],
                             'positions_equal':before[1]==after[1],'source':before[1],'native':after[1]})
    equal=all(row['positions_equal'] and row['policy_values_equal'] for row in rows)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_PROVIDER_POSITIONS' if equal else 'OPEN_PROVIDER_POSITION_GAP',
        'controls':len(rows),'python':sys.version,'cases':rows,
        'complete_frame_and_observer_abi_accepted':False,'default_application_enabled':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
