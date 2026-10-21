"""Process-isolated original/native NumPy grid controls; no default enablement."""
import gc
import json
import sys
from types import FunctionType,SimpleNamespace
import weakref

from run_tests import deny_archive
sys.addaudithook(deny_archive)
import numpy as np
from numpy.lib import _index_tricks_impl as tricks
from numpy._core import numeric
import _frontend_policy_probe as probe
from grid_controls import prepare_grid


def freeze(value):
    if isinstance(value,np.ndarray):return ('array',str(value.dtype),value.shape,value.tolist())
    if isinstance(value,(tuple,list)):return (type(value).__name__,tuple(freeze(item) for item in value))
    return value


def outcome(function):
    try:return ('return',freeze(function()))
    except Exception as error:
        return ('raise',type(error).__name__,str(error),getattr(error,'name',None),
                None if error.__context__ is None else type(error.__context__).__name__)


def main():
    scenario=sys.argv[1]
    plan=prepare_grid(probe)
    functions=plan.functions
    before=tuple(fn.__code__ for fn in functions)
    originals=tuple(FunctionType(fn.__code__,fn.__globals__,fn.__name__,fn.__defaults__,fn.__closure__) for fn in functions)
    codes=plan.prepared_codes
    old_aliases=np.indices,np.ogrid.__getitem__,np.mgrid.__getitem__
    assert old_aliases[0] is functions[1] and all(method.__func__ is functions[0] for method in old_aliases[1:])
    tests=[]
    if scenario.startswith('indices-'):
        sparse=scenario=='indices-sparse'
        for dims in ((),(1,),(2,3),(0,3),(2,1,3)):
            for dtype in (int,np.float32,np.int16,object):
                tests.append(lambda d=dims,t=dtype:np.indices(d,dtype=t,sparse=sparse))
    elif scenario in ('ogrid','mgrid','complex','single-slice','invalid'):
        grid=np.mgrid if scenario=='mgrid' else np.ogrid
        keys={
            'ogrid':[(slice(0,3),slice(0,4)),(slice(-2,3,2),),(slice(0,0),slice(0,2))],
            'mgrid':[(slice(0,3),slice(0,4)),(slice(3,-2,-2),),(slice(0,0),slice(0,2))],
            'complex':[(slice(0,1,5j),slice(-1,1,3j)),slice(0,1,1j),slice(0,1,0j),slice(0,1,-4j)],
            'single-slice':[slice(0,3),slice(-3,2,2),slice(0,1,.25),slice(3,-1,-1)],
            'invalid':[None,3,'bad',(slice(0,3),4),slice(0,3,0),(slice(0,3,0),),slice(None,None)],
        }[scenario]
        tests=[lambda key=key:grid[key] for key in keys]
    elif scenario=='alias-protocol':
        tests=[lambda:old_aliases[0]((2,3)),lambda:old_aliases[1]((slice(0,2),slice(0,3))),
               lambda:old_aliases[2]((slice(0,2),slice(0,3)))]
    elif scenario=='invalid-dimensions':
        tests=[lambda dims=dims:np.indices(dims) for dims in ((-1,2),(1.5,3),('x',2),None)]
    elif scenario=='brush':
        from native_declarations import prepare_table
        from capture_imports import import_captured
        table=prepare_table(probe)
        scopes=import_captured(probe,table)
        from napari.layers import Labels
        oracle=Labels(np.zeros((6,6),dtype=np.int16));oracle.brush_size=2;oracle.paint((2,3),9)
        expected_brush=oracle.data.copy()
    elif scenario not in ('foreign-exceptions','foreign-numeric','gc','audit-reject'):
        raise AssertionError(scenario)
    expected=[outcome(test) for test in tests]
    if scenario=='audit-reject':
        events=[]
        def reject(event,args):
            if event=='object.__setattr__' and args[1]=='__code__' and args[0] in functions:
                events.append(args[0])
                assert all(fn.__code__ is code for fn,code in zip(functions,before,strict=True))
                if len(events)==2:raise ValueError('reject second provider')
        sys.addaudithook(reject)
        try:plan.publish_experiment()
        except ValueError:pass
        else:raise AssertionError('provider rejection ignored')
        assert len(events)==2 and all(fn.__code__ is code for fn,code in zip(functions,before,strict=True))
    elif scenario in ('foreign-exceptions','foreign-numeric'):
        def clone(code,namespace):return FunctionType(code,namespace,functions[0].__name__,functions[0].__defaults__)
        if scenario=='foreign-exceptions':
            for first,second in ((KeyError,ValueError),((),TypeError),(IndexError,()),(IndexError,TypeError)):
                namespace=dict(functions[0].__globals__,IndexError=first,TypeError=second)
                args=tricks.nd_grid(sparse=True),slice(0,3)
                assert outcome(lambda:clone(codes[0],namespace)(*args))==outcome(lambda:clone(before[0],namespace)(*args))
        else:
            base=originals[1]((2,3))
            def run(code):
                log=[]
                def foreign(size,typ):
                    log.append((tuple(size),str(typ)))
                    return base.copy()
                proxy=SimpleNamespace(complexfloating=np.complexfloating,result_type=np.result_type,
                    arange=np.arange,indices=foreign,newaxis=None)
                namespace=dict(functions[0].__globals__,_nx=proxy)
                result=clone(code,namespace)(tricks.nd_grid(sparse=False),(slice(0,2),slice(0,3)))
                return freeze(result),log
            assert run(codes[0])==run(before[0])
    else:
        metadata=tuple((fn.__globals__,fn.__builtins__,fn.__defaults__,fn.__closure__) for fn in functions)
        plan.publish_experiment();plan.validate()
        assert all(fn.__code__ is code for fn,code in zip(functions,codes,strict=True))
        assert all(left is right for old,new in zip(metadata,
            ((fn.__globals__,fn.__builtins__,fn.__defaults__,fn.__closure__) for fn in functions),strict=True)
            for left,right in zip(old,new,strict=True))
        entered=[]
        def trace(frame,event,arg):
            if event=='call' and any(frame.f_code is code for code in before):
                entered.append(frame.f_code.co_qualname)
                raise AssertionError('original grid policy executed')
        prior=sys.getprofile();sys.setprofile(trace)
        try:
            actual=[outcome(test) for test in tests]
            assert actual==expected,(scenario,actual,expected)
            if scenario=='brush':
                labels=Labels(np.zeros((6,6),dtype=np.int16));labels.brush_size=2;labels.paint((2,3),9)
                np.testing.assert_array_equal(labels.data,expected_brush)
            if scenario=='gc':
                reference=weakref.ref(plan)
                del plan
                gc.collect();gc.collect()
                assert reference() is None
                assert np.ogrid[0:3,0:4][0].shape==(3,1)
            assert entered==[]
        finally:sys.setprofile(prior)
    print(json.dumps(dict(scenario=scenario,status='PASS',value_controls=len(tests))))


if __name__=='__main__':main()
