"""Compare a foreign higher-priority monitor failure at return or unwind."""
import hashlib,json,sys
from pathlib import Path
from grid_comprehension_child import deny_archive
from grid_line_transport import LineProbe
from freeze_transport import profile
from native_declarations import tuples
from inspect_inputs import walk
import grid_getitem_locals_child as getter

DIRECTORY=Path(__file__).resolve().parent;ROOT=DIRECTORY.parents[1]


def run(probe,code,shells,mode,native,adapter=False,tracing=False):
    monitor=sys.monitoring;event=monitor.events.PY_RETURN if mode=='return' else monitor.events.PY_UNWIND
    unraisable=[];counts=[];root=[];events=[]
    def trace(frame,event,arg):
        if frame.f_code is not root[0]:return None
        events.append((event,frame.f_lineno,frame.f_lasti))
        return trace
    def foreign(current,*args):
        if current is root[0]:raise RuntimeError('foreign '+mode+' failure')
    def ignored(value):unraisable.append((type(value.exc_value).__name__,str(value.exc_value),value.err_msg))
    previous=sys.unraisablehook;sys.unraisablehook=ignored
    def setup(namespace):
        if mode=='unwind':
            def broken(*args,**kwargs):raise RuntimeError('body provider failure')
            namespace['_nx'].arange=broken
    def invoke(function,args):
        if native:
            wrapper=LineProbe(probe,opcodes=True,completion=True)
            function.__code__=wrapper.prepare_grid_function_probe(function,function.__globals__,shells['_grid_getitem'],
                tuples(profile(code)),tuples(profile(shells['_grid_getitem'].__code__)),0,
                shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__)))
        root.append(function.__code__)
        monitor.use_tool_id(5,'foreign error fixture')
        monitor.register_callback(5,event,foreign)
        if mode=='return':monitor.set_local_events(5,function.__code__,event)
        else:monitor.set_events(5,event)
        if native:
            probe.start_grid_code_monitor_probe(3)
            probe.set_grid_code_monitor_probe(function.__code__,True)
        if tracing:sys.settrace(trace)
        try:
            try:return wrapper.invoke(function,args) if native and adapter else function(*args)
            except BaseException as error:
                chain=[];seen=set();item=error
                while item is not None and id(item) not in seen:
                    seen.add(id(item));sites=[];tb=item.__traceback__
                    while tb:
                        if tb.tb_frame.f_code is function.__code__:sites.append((tb.tb_lineno,tb.tb_lasti))
                        tb=tb.tb_next
                    chain.append((type(item).__name__,str(item),sites));item=item.__context__
                return ('caught',chain)
        finally:
            sys.settrace(None)
            monitor.set_events(5,0);monitor.set_local_events(5,function.__code__,0)
            monitor.register_callback(5,event,None);monitor.free_tool_id(5)
            if native:
                counts.append(tuple(probe.grid_code_monitor_counts_probe()))
                probe.set_grid_code_monitor_probe(function.__code__,False)
                probe.stop_grid_code_monitor_probe()
    try:
        result=getter.run(probe,code,shells,'single',False,invoke=invoke,namespace_setup=setup)
    finally:sys.unraisablehook=previous
    return result,unraisable,counts,events


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    if sys.version_info<(3,12):
        print(json.dumps({'status':'OPEN_CP311_CODE_MONITOR','controls':0}));return 0
    mode=sys.argv[1] if len(sys.argv)>1 else 'unwind';assert mode in ('return','unwind')
    adapter='--adapter' in sys.argv;tracing='--trace' in sys.argv
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    def fresh():return next(c for c in walk(compile(data,str(path),'exec',dont_inherit=True)) if c.co_qualname==row['qualname'])
    shells={};shell=DIRECTORY/'grid_shells.py';exec(compile(shell.read_bytes(),str(shell),'exec'),shells)
    before=run(probe,fresh(),shells,mode,False,adapter,tracing)
    try:after=run(probe,fresh(),shells,mode,True,adapter,tracing)
    except BaseException as error:
        print(json.dumps({'status':'OPEN_FOREIGN_TOOL_SCOPE_CLEANUP','mode':mode,'controls':1,
            'error':(type(error).__name__,str(error)),'monitor_counts':tuple(probe.grid_code_monitor_counts_probe()),
            'source_outcome':before[0],'source_unraisable':before[1],'python':sys.version}));return 1
    equal=before[:2]==after[:2] and before[3]==after[3]
    assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    print(json.dumps({'status':'PASS_FINITE_FOREIGN_TOOL_ERROR' if equal else 'OPEN_FOREIGN_TOOL_ERROR_GAP',
        'mode':mode,'adapter':adapter,'tracing':tracing,'controls':1,'outcomes_equal':before[0]==after[0],'unraisable_equal':before[1]==after[1],
        'events_equal':before[3]==after[3],'source_events':before[3],'native_events':after[3],
        'counts_before_teardown':after[2],'source_unraisable':before[1],'native_unraisable':after[1],
        'source_outcome':before[0],'native_outcome':after[0],
        'tool_and_scopes_released':True,'python':sys.version,'complete_tool_failure_abi_accepted':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
