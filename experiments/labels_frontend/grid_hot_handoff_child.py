"""Compare handoff from a genuinely warmed source generation to native code."""
import hashlib
import json
from pathlib import Path
import sys
import types

from grid_comprehension_child import deny_archive
import grid_getitem_locals_child as getter
from grid_line_transport import LineProbe
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]
MODES=('sparse','dense','complex-grid','single','complex-single','same-code','replace-getter')


def run(probe,code,shells,mode,native):
    wrapper=LineProbe(probe,opcodes=True);positions=[];warming=True;original_calls=0
    def observe(name,frame):
        if not warming:positions.append((name,frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti))
    def invoke(function,args):
        # Preparation is delayed until invocation so warming uses exactly the
        # same source function, defining globals, argument objects and aliases.
        nonlocal warming,original_calls
        for _ in range(8):function(*args);original_calls+=1
        if mode in ('same-code','replace-getter'):
            owner=type(function.__globals__['_nx']);old=vars(owner)['__getattribute__']
            if mode=='same-code':old.__code__=old.__code__
            else:setattr(owner,'__getattribute__',types.FunctionType(old.__code__,old.__globals__,old.__name__,old.__defaults__,old.__closure__))
        if native:
            from freeze_transport import profile
            from native_declarations import tuples
            function.__code__=wrapper.prepare_grid_function_probe(function,function.__globals__,shells['_grid_getitem'],
                tuples(profile(code)),tuples(profile(shells['_grid_getitem'].__code__)),0,
                shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__)))
            originals=set(walk(code))
            def guard(frame,event,arg):
                if event=='call' and frame.f_code in originals:raise AssertionError('original body executed after cutover')
            sys.setprofile(guard)
        warming=False;positions.clear()
        for _ in range(4):result=wrapper.invoke(function,args) if native else function(*args)
        return result
    # This harness performs the handoff itself after its declared warm-up.
    result=getter.run(probe,code,shells,mode if mode in ('sparse','dense','complex-grid','single','complex-single') else 'sparse',
                      False,invoke=invoke,observe=observe)
    assert original_calls==8 and positions
    return result[0],positions


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
    for mode in MODES:
        before=run(probe,fresh(),shells,mode,False);after=run(probe,fresh(),shells,mode,True)
        rows.append({'mode':mode,'policy_values_equal':before[0]==after[0],'positions_equal':before[1]==after[1],
                     'source':before[1],'native':after[1]})
    equal=all(row['policy_values_equal'] and row['positions_equal'] for row in rows)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_HOT_HANDOFF' if equal else 'OPEN_HOT_HANDOFF_GAP',
        'controls':len(rows),'pre_cutover_source_calls':8,'post_cutover_calls':4,'cases':rows,'python':sys.version,
        'complete_hot_handoff_abi_accepted':False,'default_application_enabled':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
