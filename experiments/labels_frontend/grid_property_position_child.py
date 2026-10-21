"""Compare Python property callback positions through native grid policies."""
import hashlib
import json
from pathlib import Path
import sys
import types

from grid_comprehension_child import deny_archive
import grid_getitem_locals_child as getter
from grid_line_transport import LineProbe
from inspect_inputs import walk
from freeze_transport import profile
from native_declarations import tuples

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]
MODES=('cold','hot','same-code','replace-property','has-getattr',
       'runtime-cold','runtime-hot','typeerror-hot')


def run(probe,code,shells,mode,native):
    wrapper=LineProbe(probe,opcodes=True);positions=[];ready=False
    owner=(code.co_filename,code.co_qualname)
    def raised(error):
        chain=[];seen=set()
        while error is not None and id(error) not in seen:
            seen.add(id(error));trace=[];tb=error.__traceback__
            while tb:
                if (tb.tb_frame.f_code.co_filename,tb.tb_frame.f_code.co_qualname)==owner:
                    trace.append((tb.tb_lineno,tb.tb_lasti))
                tb=tb.tb_next
            chain.append((type(error).__name__,str(error),tuple(trace)));error=error.__context__
        return ('raised',tuple(chain))
    def observe(name,frame):
        if ready:positions.append((name,frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti))
    def invoke(function,args):
        nonlocal ready
        kind=type(args[1][0]);old=vars(kind)['__getattribute__']
        cells=dict(zip(old.__code__.co_freevars,(cell.cell_contents for cell in old.__closure__)))
        callback=cells['callback']
        def make_property(name):
            def read(self):
                callback('slice-'+name,sys._getframe(1))
                if ready and name=='step' and mode in ('runtime-cold','runtime-hot','typeerror-hot'):
                    raise (TypeError if mode=='typeerror-hot' else RuntimeError)('property callback failure')
                return object.__getattribute__(self,'__dict__')[name]
            return property(read)
        kind.__getattribute__=object.__getattribute__
        for name in ('step','start','stop'):setattr(kind,name,make_property(name))
        if mode=='has-getattr':
            def missing(self,name):raise AttributeError(name)
            kind.__getattr__=missing
        if mode not in ('cold','runtime-cold'):
            for _ in range(8):function(*args)
        if mode=='same-code':
            function_getter=vars(kind)['step'].fget;function_getter.__code__=function_getter.__code__
        elif mode=='replace-property':
            old_getter=vars(kind)['step'].fget
            replacement=types.FunctionType(old_getter.__code__,old_getter.__globals__,old_getter.__name__,
                                           old_getter.__defaults__,old_getter.__closure__)
            kind.step=property(replacement)
        if native:
            function.__code__=wrapper.prepare_grid_function_probe(function,function.__globals__,shells['_grid_getitem'],
                tuples(profile(code)),tuples(profile(shells['_grid_getitem'].__code__)),0,
                shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__)))
            originals=set(walk(code))
            def guard(frame,event,arg):
                if event=='call' and frame.f_code in originals:raise AssertionError('original grid body executed after cutover')
            sys.setprofile(guard)
        ready=True;positions.clear();outcomes=[]
        for _ in range(40):
            try:result=wrapper.invoke(function,args) if native else function(*args)
            except BaseException as error:result=raised(error)
            outcomes.append(result)
        return outcomes
    result=getter.run(probe,code,shells,'sparse',False,invoke=invoke,observe=observe)
    assert positions and sys.gettrace() is None
    return result[0],positions


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    source=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=source.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    def fresh():return next(item for item in walk(compile(data,str(source),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    rows=[]
    for mode in MODES:
        before=run(probe,fresh(),shells,mode,False);after=run(probe,fresh(),shells,mode,True)
        rows.append({'mode':mode,'policy_values_equal':before[0]==after[0],'positions_equal':before[1]==after[1],
                     'source_outcome':before[0],'native_outcome':after[0],
                     'source':before[1],'native':after[1]})
    equal=all(row['policy_values_equal'] and row['positions_equal'] for row in rows)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_PROPERTY_POSITIONS' if equal else 'OPEN_PROPERTY_POSITION_GAP',
        'controls':len(rows),'post_cutover_calls':40,'cases':rows,'python':sys.version,
        'complete_descriptor_abi_accepted':False,'default_application_enabled':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
