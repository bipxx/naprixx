"""Per-function direct tracing compared to the unhooked original source."""
import hashlib,json,sys,weakref,gc
from pathlib import Path
from grid_comprehension_child import deny_archive
from grid_line_transport import LineProbe
import grid_retained_frame_child as retained
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    assert probe.grid_function_scope_count_probe()==0
    if sys.version_info<(3,12):
        try:probe.set_grid_function_scope_probe(lambda:None,True)
        except NotImplementedError:pass
        else:raise AssertionError('unsupported per-function scope admitted')
        print(json.dumps({'status':'OPEN_CP311_FUNCTION_CALL_BYPASS','controls':0,'python':sys.version,'default_application_enabled':False}))
        return 0
    sources=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources']
    def fresh(policy):
        row=sources[policy=='indices'];path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
        assert hashlib.sha256(data).hexdigest()==row['sha256']
        return next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    # Installing one prepared function must leave unrelated Python calls
    # unchanged, and must not give C++ a persistent owning function reference.
    references=[];holder={};active_rejected=[]
    def unrelated_callee():return sys._getframe(1).f_lasti
    def unrelated_caller():return unrelated_callee()
    def observe(event,frame):
        if active_rejected:return
        try:probe.set_grid_function_scope_probe(holder['function'],False)
        except ValueError:active_rejected.append(True)
        else:raise AssertionError('active function hook removed')
    def invoke(function,args):
        before=[unrelated_caller() for _ in range(64)]
        code=function.__code__;namespace=function.__globals__;dictionary=function.__dict__
        alias=function
        probe.set_grid_function_scope_probe(function,True)
        assert probe.grid_function_scope_count_probe()==1
        try:probe.set_grid_function_scope_probe(function,True)
        except ValueError:pass
        else:raise AssertionError('duplicate function hook admitted')
        assert function is alias and function.__code__ is code
        assert function.__globals__ is namespace and function.__dict__ is dictionary
        assert before==[unrelated_caller() for _ in range(64)]
        holder['function']=function
        try:return function(*args)
        finally:
            holder.clear()
            # Leave the hook installed and give the function a self-cycle.
            function._scope_cycle=function
            references.extend((weakref.ref(function),weakref.ref(code)))
    retained.getter.run(LineProbe(probe,opcodes=True,completion=True),fresh('getitem'),shells,'single',True,invoke=invoke,observe=observe)
    gc.collect()
    assert active_rejected==[True] and len(references)==2 and all(ref() is None for ref in references)
    assert probe.grid_function_scope_count_probe()==0
    rows=[]
    for tracing in (None,'line','opcode'):
        for policy,mode in retained.MODES:
            before=retained.run(probe,fresh(policy),shells,mode,False,tracing,policy,completion=True,direct=True)
            after=retained.run(probe,fresh(policy),shells,mode,True,tracing,policy,completion=True,direct=True,function_scope=True)
            rows.append({'policy':policy,'mode':mode,'tracing':tracing,
                'values_equal':before[0]==after[0],'events_equal':before[1]==after[1],
                'retained_equal':before[2]==after[2],'errors_equal':before[3]==after[3],
                'first_event_difference':next(((i,a,b) for i,(a,b) in enumerate(zip(before[1],after[1])) if a!=b),None)})
    equal=all(all(c[k] for k in ('values_equal','events_equal','retained_equal','errors_equal')) for c in rows)
    assert probe.grid_function_scope_count_probe()==0
    print(json.dumps({'status':'PASS_FINITE_FUNCTION_SCOPE' if equal else 'OPEN_FUNCTION_SCOPE_GAP',
        'controls':len(rows),'cases':rows,'python':sys.version,'interpreter_evaluator_changed':False,
        'unselected_source_positions_unchanged':True,'function_alias_and_metadata_identity_preserved':True,
        'duplicate_enable_and_active_disable_rejected':True,'enabled_cyclic_function_and_code_collected':True,
        'unhooked_original_source_oracle':True,'complete_call_and_lifetime_abi_accepted':False,'default_application_enabled':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
