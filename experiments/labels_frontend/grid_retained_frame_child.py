"""Compare positions of genuinely retained grid frames after completion."""
import hashlib,json,sys
from pathlib import Path
from grid_comprehension_child import deny_archive
from inspect_inputs import walk
from grid_line_transport import LineProbe
import grid_getitem_locals_child as getter
import grid_indices_locals_child as indices
DIRECTORY=Path(__file__).resolve().parent;ROOT=DIRECTORY.parents[1]
MODES=tuple(('getitem',mode) for mode in ('sparse','dense','single','complex-grid','callback-error'))+tuple(('indices',mode) for mode in ('dense','sparse','callback-error'))

def run(probe,code,shells,mode,native,tracing,policy,completion=False,direct=False,ambient=False,function_scope=False):
    wrapper=LineProbe(probe,opcodes=True,completion=completion);frames=[];events=[];errors=[];traced_errors=set()
    owners={(c.co_filename,c.co_qualname) for c in walk(code)}
    def observe(name,frame):
        if not any(item is frame for item in frames):frames.append(frame)
    def trace(frame,event,arg):
        if (frame.f_code.co_filename,frame.f_code.co_qualname) not in owners:return None
        if event=='call':frame.f_trace=trace;frame.f_trace_opcodes=tracing=='opcode'
        if event=='exception':traced_errors.add(id(arg[1]))
        events.append((event,frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti))
        return trace
    def invoke(function,args):
        if native and function_scope:probe.set_grid_function_scope_probe(function,True)
        try:return wrapper.invoke(function,args) if native and not direct else function(*args)
        except BaseException as error:
            if tracing:assert id(error) in traced_errors
            chain=[];seen=set();item=error
            while item is not None and id(item) not in seen:
                seen.add(id(item));positions=[];tb=item.__traceback__
                while tb:
                    if (tb.tb_frame.f_code.co_filename,tb.tb_frame.f_code.co_qualname) in owners:
                        positions.append((tb.tb_frame.f_code.co_qualname,tb.tb_lineno,tb.tb_lasti))
                    tb=tb.tb_next
                chain.append((type(item).__name__,str(item),positions));item=item.__context__
            errors.append(chain)
            raise
        finally:
            if native and function_scope:probe.set_grid_function_scope_probe(function,False)
    controller=sys._getframe();previous=controller.f_trace_opcodes
    controller.f_trace_opcodes=tracing=='opcode'
    if tracing:sys.settrace(trace)
    try:
        def call():
            if policy=='getitem':
                return getter.run(wrapper if native else probe,code,shells,mode,native,invoke=invoke,observe=observe)
            return indices.run(wrapper if native else probe,code,shells['_grid_indices'],mode,native,invoke=invoke,observe=observe)
        if ambient:
            try:raise LookupError('retained-frame caller context')
            except LookupError as caller:
                result=call();assert sys.exception() is caller
        else:result=call()
        assert sys.gettrace() is (trace if tracing else None)
    finally:sys.settrace(None);controller.f_trace_opcodes=previous
    assert frames
    final=[(frame.f_code.co_qualname,frame.f_lineno,frame.f_lasti,tuple(sorted(frame.f_locals))) for frame in frames]
    return result,events,final,errors

def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    sources=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources']
    def fresh(policy):
        row=sources[policy=='indices'];source=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=source.read_bytes()
        assert hashlib.sha256(data).hexdigest()==row['sha256']
        return next(c for c in walk(compile(data,str(source),'exec',dont_inherit=True)) if c.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    rows=[]
    tracings=(None,'line','opcode')
    if '--traced-only' in sys.argv:tracings=('line','opcode')
    if '--direct-call' in sys.argv and '--direct-traces' not in sys.argv:tracings=(None,)
    for tracing in tracings:
        for policy,mode in MODES:
            ambient='--ambient' in sys.argv
            before=run(probe,fresh(policy),shells,mode,False,tracing,policy,ambient=ambient)
            after=run(probe,fresh(policy),shells,mode,True,tracing,policy,'--completion-transport' in sys.argv,'--direct-call' in sys.argv,ambient)
            rows.append({'policy':policy,'mode':mode,'tracing':tracing,'policy_values_equal':before[0]==after[0],
                'events_equal':before[1]==after[1],'retained_equal':before[2]==after[2],
                'errors_equal':before[3]==after[3],'source_errors':before[3],'native_errors':after[3],
                'first_event_difference':next(((i,a,b) for i,(a,b) in enumerate(zip(before[1],after[1])) if a!=b),None),
                'source_event_tail':before[1][-4:],'native_event_tail':after[1][-4:],
                'source':before[2],'native':after[2]})
    equal=all(c['policy_values_equal'] and c['events_equal'] and c['retained_equal'] and c['errors_equal'] for c in rows)
    print(json.dumps({'status':'PASS_FINITE_RETAINED_FRAMES' if equal else 'OPEN_RETAINED_FRAME_GAP','controls':len(rows),'cases':rows,'python':sys.version,
        'completion_transport':'--completion-transport' in sys.argv,
        'direct_call':'--direct-call' in sys.argv,
        'ambient_caller_context':'--ambient' in sys.argv,
        'complete_frame_abi_accepted':False,'default_application_enabled':False}))
    return 0 if equal else 1
if __name__=='__main__':raise SystemExit(main())
