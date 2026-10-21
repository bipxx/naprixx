"""Required tracing diagnostic: exit 1 until native/source event parity holds.

This is deliberately separate from the finite passing policy suite. A mismatch
remains an application-acceptance gap; it is not an expected-pass fixture.
"""
import hashlib
import json
from pathlib import Path
import sys

from grid_comprehension_child import deny_archive
import grid_getitem_locals_child as getitem
import grid_indices_locals_child as indices
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    pins=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())
    codes=[]; buffers=[]
    for row in pins['sources']:
        path=ROOT/'.venv/Lib/site-packages'/row['package_path']; source=path.read_bytes()
        assert hashlib.sha256(source).hexdigest()==row['sha256']
        buffers.append(source)
        codes.append(next(item for item in walk(compile(source,str(path),'exec',dont_inherit=True))
                          if item.co_qualname==row['qualname']))
    path=DIRECTORY/'grid_shells.py'; shells={}
    exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    owners={(code.co_filename,code.co_qualname) for parent in codes for code in walk(parent)}
    rows=[]
    for index,(policy,original) in enumerate(zip(('getitem','indices'),codes)):
        for opcodes in (False,True):
            observed=[]
            for native in (False,True):
                # Give each observation a fresh compiler generation so prior
                # monitoring/quickening state cannot affect event collection.
                code=next(item for item in walk(compile(buffers[index],original.co_filename,'exec',dont_inherit=True))
                          if item.co_qualname==original.co_qualname)
                events=[]
                def trace(frame,event,arg):
                    key=(frame.f_code.co_filename,frame.f_code.co_qualname)
                    if key not in owners: return None
                    if event=='call':
                        # 3.13 enables local opcode monitoring only when the
                        # frame already has a trace callback installed.
                        frame.f_trace=trace
                        frame.f_trace_opcodes=opcodes
                    events.append((event,frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti))
                    return trace
                # CPython's monitoring-backed tracer chooses opcode events
                # when settrace is installed; request them on an active frame
                # first (cpython#114480), not only on a later call event.
                controller=sys._getframe()
                previous_opcodes=controller.f_trace_opcodes
                controller.f_trace_opcodes=opcodes
                sys.settrace(trace)
                try:
                    if policy=='getitem': result=getitem.run(probe,code,shells,'sparse',native)
                    else: result=indices.run(probe,code,shells['_grid_indices'],'dense',native)
                finally:
                    sys.settrace(None)
                    controller.f_trace_opcodes=previous_opcodes
                observed.append({'events':events,'policy_result':result[0]})
            source,native=observed
            observation_complete=not opcodes or any(event[0]=='opcode' for event in source['events'])
            rows.append({'policy':policy,'opcode_tracing':opcodes,
                'policy_values_equal':source['policy_result']==native['policy_result'],
                'observation_complete':observation_complete,
                'trace_equal':source['events']==native['events'],'source':source,'native':native})
    equal=all(row['trace_equal'] and row['policy_values_equal'] and row['observation_complete'] for row in rows)
    print(json.dumps({'status':'PASS_TRACE_PARITY' if equal else 'OPEN_TRACE_PARITY_GAP',
        'python':sys.version,'cases':rows,'production_installed':False}))
    return 0 if equal else 1


if __name__=='__main__': raise SystemExit(main())
