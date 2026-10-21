"""Collect and reenter at a live completion carrier, before selector unwrap."""
import gc
import hashlib
import json
from pathlib import Path
import sys
import weakref

from grid_comprehension_child import deny_archive
from grid_line_transport import LineProbe
from freeze_transport import profile
from native_declarations import tuples
from inspect_inputs import walk
import grid_getitem_locals_child as getter
import grid_indices_locals_child as indices
from grid_retained_frame_child import MODES

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]


def run(probe,code,shells,policy,mode,native):
    wrapper=LineProbe(probe,opcodes=True,completion=True)
    refs=[];checks=[];nested=[];entered=False
    def invoke(function,args):
        nonlocal entered
        if native:
            shell=shells['_grid_getitem' if policy=='getitem' else '_grid_indices']
            extra=(shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__))) if policy=='getitem' else ()
            function.__code__=wrapper.prepare_grid_function_probe(function,function.__globals__,shell,
                tuples(profile(code)),tuples(profile(shell.__code__)),int(policy=='indices'),*extra)
            assert function.__code__ is not code
            assert function.__code__.co_consts[-2][0]=='naparixx.grid.return.v2'
        selected=function.__code__
        def callback(frame,event,arg):
            nonlocal entered
            if entered or frame.f_code is not selected:return
            if native:
                if event!='c_call' or getattr(arg,'__name__',None)!='_labels_frontend_grid_return_at':return
            elif event!='return':return
            entered=True
            def identity():
                carrier=probe.grid_completion_carrier_probe(frame)
                return id(carrier),id(carrier[0]),carrier[1],carrier[2]
            before=identity() if native else None
            if native:
                carrier=probe.grid_completion_carrier_probe(frame)
                try:refs.append(weakref.ref(carrier[0]))
                except TypeError:pass
                del carrier
            position=(frame.f_lasti,frame.f_lineno,tuple(sorted(frame.f_locals)))
            handled=sys.exception()
            gc.collect()
            if native:
                assert identity()==before
                assert all(ref() is not None for ref in refs)
            try:
                value=function(*args)
                nested.append(('return',type(value).__name__))
                del value
            except BaseException as error:
                nested.append(('raise',type(error).__name__,str(error)))
            gc.collect()
            assert sys.exception() is handled
            assert position==(frame.f_lasti,frame.f_lineno,tuple(sorted(frame.f_locals)))
            if native:
                assert identity()==before
                assert all(ref() is not None for ref in refs)
            checks.append(True)
        previous=sys.getprofile()
        sys.setprofile(callback)
        try:return function(*args)
        finally:sys.setprofile(previous)
    if policy=='getitem':result=getter.run(probe,code,shells,mode,False,invoke=invoke)
    else:result=indices.run(probe,code,shells['_grid_indices'],mode,False,invoke=invoke)
    assert entered and checks==[True] and len(nested)==1
    gc.collect()
    assert all(ref() is None for ref in refs)
    return result,nested,len(refs)


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    sources=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources']
    def fresh(policy):
        row=sources[policy=='indices'];path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
        assert hashlib.sha256(data).hexdigest()==row['sha256']
        return next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    rows=[]
    for policy,mode in (*MODES,('getitem','complex-single')):
        before=run(probe,fresh(policy),shells,policy,mode,False)
        after=run(probe,fresh(policy),shells,policy,mode,True)
        rows.append({'policy':policy,'mode':mode,'values_and_callbacks_equal':before[:2]==after[:2],
            'weak_result_release_checks':after[2],'carrier_identity_preserved':True,
            'suspended_frame_preserved':True,'handled_exception_restored':True})
    equal=all(row['values_and_callbacks_equal'] for row in rows)
    print(json.dumps({'status':'PASS_FINITE_ACTIVE_CARRIER_GC' if equal else 'OPEN_ACTIVE_CARRIER_GC_GAP',
        'python':sys.version,'controls':len(rows),'cases':rows,
        'native_boundary':'selector c_call before unwrap','source_boundary':'source return',
        'profile_event_parity_claimed':False,'runtime_profile_guard_effective_inside_callback':False,
        'static_converted_admission_checked':True,'complete_gc_reentry_abi_accepted':False,
        'default_application_enabled':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
