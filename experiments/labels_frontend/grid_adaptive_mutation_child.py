"""Compare adaptive callback positions after function/type cache invalidation."""
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
MODES=('same-getattribute-code','replace-getattribute','same-getitem-code')


def run(probe,code,shells,mode,native):
    wrapper=LineProbe(probe,opcodes=True);positions=[];seen=0;changed=False
    event='array-slice' if mode=='same-getitem-code' else 'lookup-complexfloating'
    def observe(name,frame):
        nonlocal seen,changed
        positions.append((name,frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti))
        if name!=event or changed:return
        seen+=1
        if seen!=3:return
        owner=type(frame.f_locals['nn'][frame.f_locals['k']]) if mode=='same-getitem-code' else type(frame.f_globals['_nx'])
        field='__getitem__' if mode=='same-getitem-code' else '__getattribute__'
        function=vars(owner)[field]
        if mode=='replace-getattribute':
            replacement=types.FunctionType(function.__code__,function.__globals__,function.__name__,
                                           function.__defaults__,function.__closure__)
            setattr(owner,field,replacement)
        else:function.__code__=function.__code__
        changed=True
    def invoke(function,args):
        for _ in range(40):result=wrapper.invoke(function,args) if native else function(*args)
        return result
    result=getter.run(wrapper if native else probe,code,shells,'sparse',native,invoke=invoke,observe=observe)
    assert changed and sys.gettrace() is None
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
    for mode in MODES:
        before=run(probe,fresh(),shells,mode,False);after=run(probe,fresh(),shells,mode,True)
        rows.append({'mode':mode,'policy_values_equal':before[0]==after[0],'positions_equal':before[1]==after[1],
                     'source':before[1],'native':after[1]})
    equal=all(row['policy_values_equal'] and row['positions_equal'] for row in rows)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_ADAPTIVE_MUTATION' if equal else 'OPEN_ADAPTIVE_MUTATION_GAP',
        'controls':len(rows),'invocations_per_control':40,'cases':rows,'python':sys.version,
        'complete_adaptive_observer_abi_accepted':False,'default_application_enabled':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
