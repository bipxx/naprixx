"""Compare nested grid calls made while Python suppresses trace callbacks."""
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


def nested(probe,code,shells,policy,mode,native,adapter,code_refs):
    wrapper=LineProbe(probe,opcodes=True,completion=True)
    frames=[];positions=[];errors=[]
    owners={(item.co_filename,item.co_qualname) for item in walk(code)}
    def observe(name,frame):
        if not any(item is frame for item in frames):frames.append(frame)
        positions.append((name,frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti))
    def invoke(function,args):
        if native:
            if policy=='getitem':
                function.__code__=wrapper.prepare_grid_function_probe(function,function.__globals__,shells['_grid_getitem'],
                    tuples(profile(code)),tuples(profile(shells['_grid_getitem'].__code__)),0,
                    shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__)))
            else:
                function.__code__=wrapper.prepare_grid_function_probe(function,function.__globals__,shells['_grid_indices'],
                    tuples(profile(code)),tuples(profile(shells['_grid_indices'].__code__)),1)
            assert function.__code__ is not code
            assert function.__code__.co_consts[-2][0]=='naparixx.grid.return.v2'
            code_refs.append(weakref.ref(function.__code__))
        try:return wrapper.invoke(function,args) if native and adapter else function(*args)
        except BaseException as error:
            chain=[];seen=set();item=error
            while item is not None and id(item) not in seen:
                seen.add(id(item));sites=[];tb=item.__traceback__
                while tb:
                    if (tb.tb_frame.f_code.co_filename,tb.tb_frame.f_code.co_qualname) in owners:
                        sites.append((tb.tb_frame.f_code.co_qualname,tb.tb_lineno,tb.tb_lasti))
                    tb=tb.tb_next
                chain.append((type(item).__name__,str(item),sites));item=item.__context__
            errors.append(chain);raise
    previous=sys.getprofile()
    try:
        # Profiling is suppressed in this callback. Installing the fixture's
        # usual guard here changes instrumentation on the suspended caller.
        if policy=='getitem':result=getter.run(probe,code,shells,mode,False,invoke=invoke,observe=observe)
        else:result=indices.run(probe,code,shells['_grid_indices'],mode,False,invoke=invoke,observe=observe)
    finally:sys.setprofile(previous)
    gc.collect()
    final=[(frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti,tuple(sorted(frame.f_locals))) for frame in frames]
    return result,positions,final,errors


def run(probe,outer,inner,shells,policy,mode,native,adapter,trigger,outer_native):
    wrapper=LineProbe(probe,opcodes=True,completion=True)
    owners={(item.co_filename,item.co_qualname) for item in walk(outer)}
    events=[];result=[];entered=False;code_refs=[];parent_flags=[]
    def trace(frame,event,arg):
        nonlocal entered
        if (frame.f_code.co_filename,frame.f_code.co_qualname) not in owners:return None
        if event=='call':frame.f_trace=trace;frame.f_trace_opcodes=trigger=='opcode'
        events.append((event,frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti))
        if not entered and event==trigger:
            entered=True
            before=(frame.f_lineno,frame.f_lasti,tuple(sorted(frame.f_locals)))
            def flags():return (frame.f_trace_lines,frame.f_trace_opcodes,dict(probe.grid_trace_state_probe(frame)))
            original_flags=flags()
            try:raise LookupError('reentrant tracer caller')
            except LookupError as caller:
                result.append(nested(probe,inner,shells,policy,mode,native,adapter,code_refs))
                gc.collect()
                if native:assert len(code_refs)==1 and code_refs[0]() is None
                assert sys.exception() is caller
            assert sys.gettrace() is trace
            current_flags=flags();parent_flags.append((original_flags,current_flags))
            assert original_flags[:2]==current_flags[:2]
            assert all(value==current_flags[2][key] for key,value in original_flags[2].items() if not key.endswith('version'))
            assert before==(frame.f_lineno,frame.f_lasti,tuple(sorted(frame.f_locals)))
        return trace
    def invoke(function,args):
        controller=sys._getframe();old=controller.f_trace_opcodes
        controller.f_trace_opcodes=trigger=='opcode';sys.settrace(trace)
        try:return wrapper.invoke(function,args) if native and outer_native else function(*args)
        finally:sys.settrace(None);controller.f_trace_opcodes=old
    selected=native and outer_native
    outer_result=getter.run(wrapper if selected else probe,outer,shells,'sparse',selected,invoke=invoke)
    assert entered and len(result)==1
    return outer_result,events,result[0],parent_flags


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
    for trigger in ('line','opcode'):
        for adapter in (False,True):
            for outer_native in (False,True):
                for policy,mode in MODES:
                    before=run(probe,fresh('getitem'),fresh(policy),shells,policy,mode,False,adapter,trigger,outer_native)
                    after=run(probe,fresh('getitem'),fresh(policy),shells,policy,mode,True,adapter,trigger,outer_native)
                    rows.append({'policy':policy,'mode':mode,'adapter':adapter,'trigger':trigger,'outer_native':outer_native,
                        'outer_equal':before[:2]==after[:2],
                        'outer_values_equal':before[0]==after[0],'outer_events_equal':before[1]==after[1],
                        'outer_first_difference':next(((i,a,b) for i,(a,b) in enumerate(zip(before[1],after[1])) if a!=b),None),
                        'source_parent_flags':before[3],'native_parent_flags':after[3],
                        'values_equal':before[2][0]==after[2][0],'positions_equal':before[2][1]==after[2][1],
                        'retained_equal':before[2][2]==after[2][2],'errors_equal':before[2][3]==after[2][3],
                        'source_nested':before[2],'native_nested':after[2]})
    equal=all(all(row[key] for key in ('outer_equal','values_equal','positions_equal','retained_equal','errors_equal')) for row in rows)
    def encode(value):
        if isinstance(value,complex):return {'complex':[value.real,value.imag]}
        raise TypeError(type(value).__name__)
    print(json.dumps({'status':'PASS_FINITE_TRACE_REENTRY' if equal else 'OPEN_TRACE_REENTRY_GAP','controls':len(rows),'cases':rows,'python':sys.version,
        'trace_callback_gc_collection':True,'nested_prepared_code_release_checked':True,
        'runtime_profile_guard_effective_inside_callback':False,'static_converted_admission_checked':True,
        'complete_reentry_gc_and_observer_abi_accepted':False,'default_application_enabled':False},default=encode))
    return 0 if equal else 1
if __name__=='__main__':raise SystemExit(main())
