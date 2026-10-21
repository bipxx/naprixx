"""Compare cold/warm call sites and caller positions for per-function scopes."""
import hashlib,json,sys,itertools
from pathlib import Path
from grid_comprehension_child import deny_archive
from grid_line_transport import LineProbe
from freeze_transport import profile
from native_declarations import tuples
from inspect_inputs import walk
import grid_getitem_locals_child as getter
import grid_indices_locals_child as indices

DIRECTORY=Path(__file__).resolve().parent;ROOT=DIRECTORY.parents[1]
STYLES=('star','direct','bound','keywords','descriptor','c-forwarded')


def run(probe,code,shells,policy,style,tracing,native,failing=False,prime_opcodes=True,code_monitor=False):
    wrapper=LineProbe(probe,opcodes=True,completion=True)
    caller_positions=[];events=[];errors=[];states=[];resumed=[];number=-1
    owners={(c.co_filename,c.co_qualname) for c in walk(code)}
    originals=set(walk(code))
    def observe(event,frame):
        if not states:states.append(dict(probe.grid_trace_state_probe(frame)))
        parent=frame.f_back
        assert parent.f_code.co_name=='entry'
        caller_positions.append((number,event,parent.f_lineno,parent.f_lasti))
    def trace(frame,event,arg):
        if (frame.f_code.co_filename,frame.f_code.co_qualname) not in owners:return None
        if native:assert frame.f_code not in originals,'original application body executed'
        if event=='call':frame.f_trace=trace;frame.f_trace_opcodes=tracing=='opcode'
        events.append((number,event,frame.f_lineno,frame.f_lasti))
        return trace
    def invoke(function,args):
        nonlocal number
        if native:
            shell=shells['_grid_getitem' if policy=='getitem' else '_grid_indices']
            extra=(shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__))) if policy=='getitem' else ()
            function.__code__=wrapper.prepare_grid_function_probe(function,function.__globals__,shell,
                tuples(profile(code)),tuples(profile(shell.__code__)),int(policy=='indices'),*extra)
            assert function.__code__ is not code and function.__code__.co_consts[-2][0]=='naparixx.grid.return.v2'
            if code_monitor:
                probe.start_grid_code_monitor_probe(3)
                probe.set_grid_code_monitor_probe(function.__code__,True)
            else:probe.set_grid_function_scope_probe(function,True)
        count=2 if policy=='getitem' else 3
        operands=','.join(f'args[{i}]' for i in range(count))
        if style=='star':body='return fn(*args)'
        elif style=='direct':body=f'return fn({operands})'
        elif style=='bound':body='bound=fn.__get__(args[0],type(args[0])); return bound('+','.join(f'args[{i}]' for i in range(1,count))+')'
        elif style=='keywords':
            names=('self','key') if policy=='getitem' else ('dimensions','dtype','sparse')
            body='return fn('+','.join(f'{name}=args[{i}]' for i,name in enumerate(names))+')'
        elif style=='c-forwarded':body='return next(starmap(fn,(args,)))'
        else:
            if policy=='getitem':
                receiver=type('EntryGrid',(),{'__getitem__':function})();receiver.sparse=args[0].sparse
                args=(receiver,args[1]);body='return args[0][args[1]]'
            else:
                function=type('EntryGrid',(),{'run':staticmethod(function)})();body=f'return fn.run({operands})'
        if failing:body='try:\n    '+body+'\nexcept RuntimeError:\n    resumed.append(sys._getframe().f_lineno)\n    raise'
        namespace={'sys':sys,'resumed':resumed,'starmap':itertools.starmap}
        declaration='def entry(fn,args):\n'+'\n'.join('    '+line for line in body.splitlines())+'\n'
        exec(compile(declaration,f'<grid-caller-{policy}-{style}>','exec'),namespace)
        target=namespace['entry']
        original_function=function if policy=='getitem' or style!='descriptor' else function.run
        controller=sys._getframe();previous=controller.f_trace_opcodes
        if prime_opcodes:controller.f_trace_opcodes=tracing=='opcode'
        if tracing:sys.settrace(trace)
        try:
            for number in range(1 if failing else 40):result=target(function,args)
            return result
        except RuntimeError as error:
            tb=error.__traceback__;positions=[]
            while tb:
                if tb.tb_frame.f_code is target.__code__:positions.append((tb.tb_lineno,tb.tb_lasti))
                tb=tb.tb_next
            assert positions;errors.extend(positions);raise
        finally:
            sys.settrace(None)
            controller.f_trace_opcodes=previous
            if native:
                if code_monitor:
                    probe.set_grid_code_monitor_probe(original_function.__code__,False)
                    probe.stop_grid_code_monitor_probe()
                else:probe.set_grid_function_scope_probe(original_function,False)
    if policy=='getitem':result=getter.run(probe,code,shells,'callback-error' if failing else 'single',False,invoke=invoke,observe=observe)
    else:result=indices.run(probe,code,shells['_grid_indices'],'callback-error' if failing else 'dense',False,invoke=invoke,observe=observe)
    assert {row[0] for row in caller_positions}==set(range(1 if failing else 40))
    assert probe.grid_function_scope_count_probe()==0
    if code_monitor:assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    return result,events,caller_positions,errors,states,resumed


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    if sys.version_info<(3,12):
        print(json.dumps({'status':'OPEN_CP311_FUNCTION_CALL_BYPASS','controls':0,'python':sys.version}));return 0
    sources=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources']
    def fresh(policy):
        row=sources[policy=='indices'];path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
        assert hashlib.sha256(data).hexdigest()==row['sha256']
        return next(c for c in walk(compile(data,str(path),'exec',dont_inherit=True)) if c.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    rows=[]
    for failing in (False,True):
     for tracing in (None,'line','opcode'):
        for policy in ('getitem','indices'):
            for style in STYLES:
                before=run(probe,fresh(policy),shells,policy,style,tracing,False,failing)
                after=run(probe,fresh(policy),shells,policy,style,tracing,True,failing)
                rows.append({'policy':policy,'style':style,'tracing':tracing,'failing':failing,'values_equal':before[0]==after[0],
                    'events_equal':before[1]==after[1],'caller_positions_equal':before[2]==after[2],
                    'first_event_difference':next(((a,b) for a,b in zip(before[1],after[1]) if a!=b),None),
                    'event_counts':[len(before[1]),len(after[1])],
                    'source_first_provider_flags':before[4],'native_first_provider_flags':after[4],
                    'caller_tracebacks_equal':before[3]==after[3],'source_tracebacks':before[3],'native_tracebacks':after[3],
                    'caller_resume_lines_equal':before[5]==after[5],'source_resume_lines':before[5],'native_resume_lines':after[5],
                    'first_caller_difference':next(((a,b) for a,b in zip(before[2],after[2]) if a!=b),None),
                    'source_caller_positions':sorted(set(r[3] for r in before[2])),
                    'native_caller_positions':sorted(set(r[3] for r in after[2]))})
    equal=all(all(c[k] for k in ('values_equal','events_equal','caller_positions_equal','caller_tracebacks_equal','caller_resume_lines_equal')) for c in rows)
    print(json.dumps({'status':'PASS_FINITE_FUNCTION_CALLERS' if equal else 'OPEN_FUNCTION_CALLER_GAP',
        'controls':len(rows),'calls_per_case':40,'cases':rows,'python':sys.version,
        'runtime_original_body_guard_effective_untraced':False,'static_converted_admission_checked':True,
        'complete_caller_specialization_abi_accepted':False,'default_application_enabled':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
